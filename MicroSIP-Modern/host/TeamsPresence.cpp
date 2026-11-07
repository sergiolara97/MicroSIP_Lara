#include "TeamsPresence.h"

#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <tlhelp32.h>
#include <pj/log.h>

#include <string>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "ole32.lib")

namespace {

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

// Lanza "ms-teams.exe <arg>" sin esperar: el cliente aplica el estado y termina.
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
	if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		PJ_LOG(2, ("TeamsPresence", "fallo %ls (err=%lu)", arg, GetLastError()));
		return;
	}
	PJ_LOG(3, ("TeamsPresence", "%ls", arg));
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

} // namespace

bool TeamsPresence::Running()
{
	return ProcessRunning(L"ms-teams.exe");
}

void TeamsPresence::SetBusy()
{
	Run(L"--set-presence-to-busy");
}

void TeamsPresence::SetAvailable()
{
	Run(L"--set-presence-to-available");
}

void TeamsPresence::Restore()
{
	if (g_previous[0] != L'\0') {
		Run(g_previous);
	}
	else {
		Run(L"--reset-presence");
	}
	g_previous[0] = L'\0';
}

void TeamsPresence::Apply(bool busy)
{
	if (busy) {
		// Se guarda el estado visible antes de pisarlo.
		std::wstring name;
		g_previous[0] = L'\0';
		if (ReadStatusName(name)) {
			std::wstring lower = name;
			CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
			for (const StatusArg& entry : kStatusMap) {
				if (lower.find(entry.needle) != std::wstring::npos) {
					wcscpy_s(g_previous, entry.arg);
					break;
				}
			}
			PJ_LOG(3, ("TeamsPresence", "estado previo: %ls -> %ls", name.c_str(),
				g_previous[0] ? g_previous : L"(automatico)"));
		}
		else {
			PJ_LOG(3, ("TeamsPresence", "no se pudo leer el estado previo"));
		}
		SetBusy();
	}
	else {
		Restore();
	}
}
