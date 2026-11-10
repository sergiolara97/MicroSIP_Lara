#include "TeamsPresence.h"

#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <tlhelp32.h>
#include <pj/log.h>
#include <pj/os.h>

#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Margen minimo entre dos comandos de estado de Teams (ms). Lanzados casi a la
// vez, el segundo se pierde.
constexpr DWORD kMinGapMs = 2000;
// Tras restaurar se comprueba que Teams no se quedo en Ocupado y se reintenta.
constexpr DWORD kVerifyDelayMs = 1500;
constexpr int kVerifyRetries = 2;

std::mutex g_lock;
HANDLE g_thread = nullptr;
HANDLE g_wake = nullptr;
bool g_quit = false;
bool g_desired = false;    // estado que pide la llamada
bool g_applied = false;    // ultimo estado aplicado
bool g_busyOwned = false;  // el Ocupado visible lo pusimos nosotros
DWORD g_lastRunTick = 0;   // fin del ultimo ms-teams.exe

// Estado de Teams que habia antes de la llamada, como argumento de ms-teams.exe.
// Vacio = no se pudo leer; al colgar se usa --reset-presence.
wchar_t g_previous[64] = {};

// ms-teams.exe se expone como alias de ejecucion del paquete MSIX.
std::wstring AliasPath()
{
	wchar_t local[MAX_PATH] = {};
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		return std::wstring();
	}
	return std::wstring(local) + L"\\Microsoft\\WindowsApps\\ms-teams.exe";
}

bool ProcessRunning(const wchar_t* name)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return false;
	}
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	bool found = false;
	if (Process32FirstW(snap, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, name) == 0) {
				found = true;
				break;
			}
		} while (Process32NextW(snap, &entry));
	}
	CloseHandle(snap);
	return found;
}

// Lanza "ms-teams.exe <arg>". El lanzador reenvia la activacion al Teams
// abierto y termina; se espera aqui (hilo propio, no bloquea la UI) para no
// solapar dos comandos.
void Run(const wchar_t* arg)
{
	std::wstring exe = AliasPath();
	if (exe.empty() || GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
		PJ_LOG(2, ("TeamsPresence", "ms-teams.exe no encontrado"));
		return;
	}
	// Con Teams cerrado el comando lo arrancaria: no se toca nada.
	if (!ProcessRunning(L"ms-teams.exe")) {
		PJ_LOG(3, ("TeamsPresence", "%ls omitido: Teams cerrado", arg));
		return;
	}

	std::wstring command = L"\"" + exe + L"\" " + arg;
	std::vector<wchar_t> buffer(command.begin(), command.end());
	buffer.push_back(L'\0');

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	DWORD start = GetTickCount();
	if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		PJ_LOG(2, ("TeamsPresence", "fallo %ls (err=%lu)", arg, GetLastError()));
		return;
	}
	DWORD wait = WaitForSingleObject(pi.hProcess, 4000);
	PJ_LOG(3, ("TeamsPresence", "%ls (%lu ms%s)", arg, GetTickCount() - start,
		wait == WAIT_OBJECT_0 ? "" : ", sigue vivo"));
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
}

// FindWindowW("TeamsWebView") devuelve otra ventana de Teams (hay varias con la
// misma clase), asi que se enumeran y se prueban todas hasta dar con la que
// tiene el boton de perfil.
BOOL CALLBACK CollectTeamsWindows(HWND hwnd, LPARAM param)
{
	wchar_t cls[64] = {};
	if (GetClassNameW(hwnd, cls, 64) == 0 || wcscmp(cls, L"TeamsWebView") != 0) {
		return TRUE;
	}
	if (GetWindowTextLengthW(hwnd) == 0) {
		return TRUE;
	}
	reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
	return TRUE;
}

bool ReadStatusFrom(IUIAutomation* uia, HWND teams, std::wstring& out)
{
	IUIAutomationElement* root = nullptr;
	if (FAILED(uia->ElementFromHandle(teams, &root)) || !root) {
		return false;
	}

	bool ok = false;
	VARIANT value;
	VariantInit(&value);
	value.vt = VT_BSTR;
	value.bstrVal = SysAllocString(L"idna-me-control-avatar-trigger");
	IUIAutomationCondition* cond = nullptr;
	if (SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &cond)) && cond) {
		IUIAutomationElement* element = nullptr;
		if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &element)) && element) {
			BSTR name = nullptr;
			if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
				out.assign(name, SysStringLen(name));
				SysFreeString(name);
				ok = !out.empty();
			}
			element->Release();
		}
		cond->Release();
	}
	VariantClear(&value);
	root->Release();
	return ok;
}

// Nombre accesible del boton de perfil: "Tu perfil, estado Disponible".
// Es la unica forma de leer el estado sin Microsoft Graph.
bool ReadStatusName(std::wstring& out)
{
	std::vector<HWND> windows;
	EnumWindows(&CollectTeamsWindows, reinterpret_cast<LPARAM>(&windows));
	if (windows.empty()) {
		return false;
	}

	bool uninit = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr)) {
		uninit = true;
	}
	else if (hr != RPC_E_CHANGED_MODE) {
		return false;
	}

	bool ok = false;
	IUIAutomation* uia = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
		IID_IUIAutomation, reinterpret_cast<void**>(&uia))) && uia) {
		for (HWND window : windows) {
			if (ReadStatusFrom(uia, window, out)) {
				ok = true;
				break;
			}
		}
		uia->Release();
	}

	if (uninit) {
		CoUninitialize();
	}
	return ok;
}

struct StatusArg {
	const wchar_t* needle;   // en minusculas
	const wchar_t* arg;
};

// Orden importante: primero las cadenas mas especificas. Los estados
// automaticos ("en una reunion", "en una llamada", "presentando") no aparecen
// aqui a proposito: al colgar se usa --reset-presence y Teams los recalcula.
const StatusArg kStatusMap[] = {
	{ L"no molestar",      L"--set-presence-to-dnd" },
	{ L"do not disturb",   L"--set-presence-to-dnd" },
	{ L"nicht st",         L"--set-presence-to-dnd" },
	{ L"ne pas d",         L"--set-presence-to-dnd" },
	{ L"vuelvo enseguida", L"--set-presence-to-be-right-back" },
	{ L"be right back",    L"--set-presence-to-be-right-back" },
	{ L"gleich zur",       L"--set-presence-to-be-right-back" },
	{ L"de retour",        L"--set-presence-to-be-right-back" },
	{ L"desconectado",     L"--set-presence-to-offline" },
	{ L"offline",          L"--set-presence-to-offline" },
	{ L"ausente",          L"--set-presence-to-away" },
	{ L"abwesend",         L"--set-presence-to-away" },
	{ L"away",             L"--set-presence-to-away" },
	{ L"ocupado",          L"--set-presence-to-busy" },
	{ L"besch",            L"--set-presence-to-busy" },
	{ L"occup",            L"--set-presence-to-busy" },
	{ L"busy",             L"--set-presence-to-busy" },
	{ L"disponible",       L"--set-presence-to-available" },
	{ L"verf",             L"--set-presence-to-available" },
	{ L"available",        L"--set-presence-to-available" },
};

// Traduce el nombre accesible del boton de perfil al argumento de ms-teams.exe.
bool MapStatus(const std::wstring& name, wchar_t out[64])
{
	if (name.empty()) {
		return false;
	}
	std::wstring lower = name;
	CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
	for (const StatusArg& entry : kStatusMap) {
		if (lower.find(entry.needle) != std::wstring::npos) {
			wcscpy_s(out, 64, entry.arg);
			return true;
		}
	}
	return false;
}

bool IsBusyName(const std::wstring& name)
{
	wchar_t arg[64] = {};
	return MapStatus(name, arg) && wcscmp(arg, L"--set-presence-to-busy") == 0;
}

// Lee el estado actual de Teams (UIA tarda ~1-2 s: solo se usa en el hilo de
// trabajo).
bool IsBusyNow()
{
	std::wstring name;
	if (!ReadStatusName(name)) {
		return false;
	}
	return IsBusyName(name);
}

// Espera ms o hasta que cambie el estado pedido. false = hay que abortar.
bool WaitWorker(DWORD ms)
{
	WaitForSingleObject(g_wake, ms);
	std::lock_guard<std::mutex> lock(g_lock);
	return !g_quit && !g_desired;
}

// true si sigue sin haber llamada y no hay que abortar.
bool CanTouch()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return !g_quit && !g_desired && !g_busyOwned;
}

// Tras restaurar, Teams a veces se queda en Ocupado (el comando se pierde).
// Se comprueba y se reintenta un par de veces.
void VerifyRestore(const std::wstring& arg)
{
	if (!WaitWorker(kVerifyDelayMs)) {
		return;
	}
	for (int i = 0; i < kVerifyRetries; i++) {
		if (!CanTouch()) {
			return;
		}
		if (!IsBusyNow()) {
			return;
		}
		if (!CanTouch()) {
			return;
		}
		PJ_LOG(2, ("TeamsPresence", "sigue Ocupado: se reintenta %ls", arg.c_str()));
		Run(arg.c_str());
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_lastRunTick = GetTickCount();
		}
		if (!WaitWorker(kVerifyDelayMs)) {
			return;
		}
	}
}

DWORD WINAPI WorkerProc(LPVOID)
{
	// pjlib exige registrar los hilos ajenos antes de usar PJ_LOG.
	pj_thread_desc desc;
	pj_thread_t* thread = nullptr;
	pj_thread_register("teams", desc, &thread);
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	std::unique_lock<std::mutex> lock(g_lock);
	for (;;) {
		if (g_quit) {
			// Si se cierra la app en mitad de una llamada, Teams no debe
			// quedarse Ocupado.
			wchar_t arg[64] = {};
			bool restore = g_busyOwned;
			if (restore) {
				wcscpy_s(arg, g_previous[0] ? g_previous : L"--reset-presence");
				g_busyOwned = false;
			}
			lock.unlock();
			if (restore) {
				Run(arg);
			}
			break;
		}

		if (g_desired == g_applied) {
			lock.unlock();
			WaitForSingleObject(g_wake, INFINITE);
			lock.lock();
			continue;
		}

		bool want = g_desired;

		// Dos comandos seguidos se pisan: se deja pasar el margen.
		DWORD since = GetTickCount() - g_lastRunTick;
		if (g_lastRunTick != 0 && since < kMinGapMs) {
			lock.unlock();
			WaitForSingleObject(g_wake, kMinGapMs - since);
			lock.lock();
			continue;
		}

		if (want) {
			// Leer el estado previo es lento: fuera del lock para que Apply()
			// pueda cambiar el estado mientras tanto.
			lock.unlock();
			std::wstring name;
			bool readOk = ReadStatusName(name);
			lock.lock();
			if (g_quit) {
				continue;
			}
			if (!g_desired) {
				// La llamada termino mientras se leia: no se toca Teams. Es el
				// caso de las llamadas muy cortas (login de centralita).
				continue;
			}
			if (!g_busyOwned) {
				g_busyOwned = true;
				g_previous[0] = L'\0';
				if (readOk) {
					MapStatus(name, g_previous);
					PJ_LOG(3, ("TeamsPresence", "estado previo: %ls -> %ls", name.c_str(),
						g_previous[0] ? g_previous : L"(automatico)"));
				}
				else {
					PJ_LOG(3, ("TeamsPresence", "no se pudo leer el estado previo"));
				}
			}
			lock.unlock();
			Run(L"--set-presence-to-busy");
			lock.lock();
			g_lastRunTick = GetTickCount();
			g_applied = true;
			continue;
		}

		// Colgar: restaurar el estado que hubiera antes de la llamada.
		wchar_t arg[64] = {};
		bool expectAvailable = false;
		if (g_busyOwned) {
			wcscpy_s(arg, g_previous[0] ? g_previous : L"--reset-presence");
			expectAvailable = wcscmp(arg, L"--set-presence-to-busy") != 0;
			g_busyOwned = false;
		}
		g_applied = false;
		lock.unlock();
		if (arg[0] != L'\0') {
			Run(arg);
			lock.lock();
			g_lastRunTick = GetTickCount();
			lock.unlock();
			if (expectAvailable) {
				VerifyRestore(arg);
			}
		}
		lock.lock();
	}

	CoUninitialize();
	return 0;
}

} // namespace

bool TeamsPresence::Running()
{
	return ProcessRunning(L"ms-teams.exe");
}

void TeamsPresence::Apply(bool busy)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_thread) {
		g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!g_wake) {
			return;
		}
		g_thread = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
		if (!g_thread) {
			CloseHandle(g_wake);
			g_wake = nullptr;
			return;
		}
	}
	g_desired = busy;
	SetEvent(g_wake);
}

void TeamsPresence::Stop()
{
	std::unique_lock<std::mutex> lock(g_lock);
	if (!g_thread) {
		return;
	}
	g_quit = true;
	SetEvent(g_wake);
	lock.unlock();

	if (WaitForSingleObject(g_thread, 15000) != WAIT_OBJECT_0) {
		// No se pudo parar: no se cierran los handles (el proceso va a salir).
		return;
	}
	CloseHandle(g_thread);
	CloseHandle(g_wake);
	g_thread = nullptr;
	g_wake = nullptr;

	lock.lock();
	g_quit = false;
	g_desired = false;
	g_applied = false;
	g_busyOwned = false;
	g_lastRunTick = 0;
	g_previous[0] = L'\0';
}
