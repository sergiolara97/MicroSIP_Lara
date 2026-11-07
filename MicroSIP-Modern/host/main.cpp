/*
 * MicroSIP Modern - host Win32 + WebView2.
 */

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <wrl.h>
#include <WebView2.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdint>


#include <json/json.h>

#include "SipCore.h"
#include "JabraLink.h"
#include "HidLink.h"
#include "TeamsPresence.h"
#include "CallToast.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "WebView2LoaderStatic.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

const wchar_t* kWindowClass = L"MicroSIPModernWindow";
const wchar_t* kWindowTitle = L"MicroSIP Modern";
const wchar_t* kVirtualHost = L"microsip.local";

HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
// Ultimo array de llamadas del snapshot (lo usa la notificacion flotante).
Json::Value g_calls;
std::wstring g_appDir;
std::wstring g_webDir;
bool g_darkTitle = true;
NOTIFYICONDATAW g_tray = {};
bool g_trayAdded = false;
bool g_quitting = false;
bool g_ringing = false;
int g_ringVolume = 100;
std::wstring g_ringDevice;
HWAVEOUT g_ringOut = nullptr;
WAVEHDR g_ringHdr = {};
bool g_ringPrepared = false;
WAVEFORMATEX g_ringFormat = {};
std::vector<char> g_ringData;

std::wstring FindRingtone()
{
	std::wstring local = g_appDir + L"\\ringtone.wav";
	if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
		return local;
	}
	wchar_t appData[MAX_PATH] = { 0 };
	if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH)) {
		std::wstring original = std::wstring(appData) + L"\\MicroSIP\\ringtone.wav";
		if (GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES) {
			return original;
		}
	}
	return std::wstring();
}

bool LoadRingWav()
{
	g_ringData.clear();
	std::wstring path = FindRingtone();
	if (path.empty()) {
		return false;
	}
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD size = GetFileSize(file, nullptr);
	std::vector<char> raw(size);
	DWORD read = 0;
	if (!ReadFile(file, raw.data(), size, &read, nullptr)) {
		CloseHandle(file);
		return false;
	}
	CloseHandle(file);
	if (raw.size() < 12
		|| std::memcmp(raw.data(), "RIFF", 4) != 0
		|| std::memcmp(raw.data() + 8, "WAVE", 4) != 0) {
		return false;
	}

	const char* dataPtr = nullptr;
	DWORD dataSize = 0;
	bool haveFormat = false;
	for (size_t i = 12; i + 8 <= raw.size(); ) {
		const char* id = raw.data() + i;
		uint32_t chunkSize = *(const uint32_t*)(raw.data() + i + 4);
		if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
			std::memcpy(&g_ringFormat, raw.data() + i + 8, 16);
			g_ringFormat.cbSize = 0;
			haveFormat = true;
		}
		else if (std::memcmp(id, "data", 4) == 0) {
			dataPtr = raw.data() + i + 8;
			dataSize = chunkSize;
			if (dataPtr + dataSize > raw.data() + raw.size()) {
				dataSize = (DWORD)(raw.data() + raw.size() - dataPtr);
			}
		}
		i += 8 + chunkSize + (chunkSize & 1);
	}
	if (!haveFormat || !dataPtr || dataSize == 0) {
		return false;
	}
	g_ringData.assign(dataPtr, dataPtr + dataSize);
	if (g_ringFormat.wBitsPerSample == 16) {
		for (size_t i = 0; i + 1 < g_ringData.size(); i += 2) {
			int16_t sample = *(int16_t*)&g_ringData[i];
			sample = (int16_t)((int)sample * g_ringVolume / 100);
			*(int16_t*)&g_ringData[i] = sample;
		}
	}
	return true;
}

void CALLBACK RingWaveCallback(HWAVEOUT hwo, UINT msg, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
	if (msg != WOM_DONE || !g_ringing || !g_ringOut || !g_ringPrepared) {
		return;
	}
	waveOutWrite(g_ringOut, &g_ringHdr, sizeof(g_ringHdr));
}

int FindWaveOutDevice()
{
	if (g_ringDevice.empty()) {
		return WAVE_MAPPER;
	}
	UINT count = waveOutGetNumDevs();
	for (UINT i = 0; i < count; i++) {
		WAVEOUTCAPSW caps;
		if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR
			&& g_ringDevice == caps.szPname) {
			return (int)i;
		}
	}
	return WAVE_MAPPER;
}

bool StartRingWave()
{
	if (g_ringData.empty() && !LoadRingWav()) {
		return false;
	}
	if (g_ringOut) {
		return true;
	}
	MMRESULT res = waveOutOpen(&g_ringOut, (UINT)FindWaveOutDevice(), &g_ringFormat,
		(DWORD_PTR)RingWaveCallback, 0, CALLBACK_FUNCTION);
	if (res != MMSYSERR_NOERROR) {
		g_ringOut = nullptr;
		return false;
	}
	std::memset(&g_ringHdr, 0, sizeof(g_ringHdr));
	g_ringHdr.lpData = g_ringData.data();
	g_ringHdr.dwBufferLength = (DWORD)g_ringData.size();
	if (waveOutPrepareHeader(g_ringOut, &g_ringHdr, sizeof(g_ringHdr)) != MMSYSERR_NOERROR) {
		waveOutClose(g_ringOut);
		g_ringOut = nullptr;
		return false;
	}
	g_ringPrepared = true;
	waveOutWrite(g_ringOut, &g_ringHdr, sizeof(g_ringHdr));
	return true;
}

void StopRingWave()
{
	if (!g_ringOut) {
		return;
	}
	waveOutReset(g_ringOut);
	if (g_ringPrepared) {
		waveOutUnprepareHeader(g_ringOut, &g_ringHdr, sizeof(g_ringHdr));
		g_ringPrepared = false;
	}
	waveOutClose(g_ringOut);
	g_ringOut = nullptr;
}

void UpdateRingtone(bool ring)
{
	if (ring == g_ringing) {
		return;
	}
	g_ringing = ring;
	if (ring) {
		if (StartRingWave()) {
			PJ_LOG(2, ("SipCore", "ringtone start volume=%d device=%d",
				g_ringVolume, FindWaveOutDevice()));
		}
		else {
			PlaySoundW(L"SystemHand", nullptr, SND_ALIAS | SND_ASYNC | SND_LOOP);
			PJ_LOG(2, ("SipCore", "ringtone: waveOut fallo, uso SystemHand"));
		}
	}
	else {
		StopRingWave();
		PlaySoundW(nullptr, nullptr, 0);
		PJ_LOG(2, ("SipCore", "ringtone stop"));
	}
}

std::string WideToUtf8(const std::wstring& value)
{
	if (value.empty()) {
		return std::string();
	}
	int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], size, nullptr, nullptr);
	return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty()) {
		return std::wstring();
	}
	int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), nullptr, 0);
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), (int)value.size(), &result[0], size);
	return result;
}

std::wstring GetAppDir()
{
	wchar_t path[MAX_PATH] = { 0 };
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring dir(path);
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		dir = dir.substr(0, pos);
	}
	return dir;
}

bool IsSystemDark()
{
	DWORD value = 1;
	DWORD size = sizeof(value);
	RegGetValueW(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
	return value == 0;
}

void ApplyDarkTitleBar()
{
	BOOL dark = g_darkTitle ? TRUE : FALSE;
	DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void AddTrayIcon(HWND hwnd, HINSTANCE instance)
{
	if (g_trayAdded) {
		return;
	}
	ZeroMemory(&g_tray, sizeof(g_tray));
	g_tray.cbSize = sizeof(g_tray);
	g_tray.hWnd = hwnd;
	g_tray.uID = 1;
	g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	g_tray.uCallbackMessage = WM_APP + 3;
	g_tray.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	lstrcpynW(g_tray.szTip, L"MicroSIP Modern", ARRAYSIZE(g_tray.szTip));
	if (Shell_NotifyIconW(NIM_ADD, &g_tray)) {
		g_trayAdded = true;
	}
}

void RemoveTrayIcon()
{
	if (g_trayAdded) {
		Shell_NotifyIconW(NIM_DELETE, &g_tray);
		g_trayAdded = false;
	}
}

void ShowMainWindow(HWND hwnd)
{
	ShowWindow(hwnd, SW_SHOW);
	ShowWindow(hwnd, SW_RESTORE);
	SetForegroundWindow(hwnd);
}

std::wstring HostLanguage()
{
	wchar_t lang[16] = { 0 };
	std::wstring ini = g_appDir + L"\\microsip.ini";
	GetPrivateProfileStringW(L"Settings", L"language", L"", lang, 16, ini.c_str());
	if (lang[0] != L'\0') {
		return lang;
	}
	switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
	case LANG_SPANISH: return L"es";
	case LANG_CATALAN: return L"ca";
	case LANG_PORTUGUESE: return L"pt";
	case LANG_FRENCH: return L"fr";
	case LANG_GERMAN: return L"de";
	case LANG_ENGLISH: return L"en";
	default: return L"en";
	}
}

const wchar_t* HostText(const wchar_t* es, const wchar_t* en, const wchar_t* ca,
	const wchar_t* pt, const wchar_t* fr, const wchar_t* de)
{
	std::wstring lang = HostLanguage();
	if (lang == L"es") return es;
	if (lang == L"ca") return ca;
	if (lang == L"pt") return pt;
	if (lang == L"fr") return fr;
	if (lang == L"de") return de;
	return en;
}

void ShowTrayMenu(HWND hwnd)
{
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, 1, HostText(
		L"Abrir MicroSIP Modern", L"Open MicroSIP Modern", L"Obre MicroSIP Modern",
		L"Abrir MicroSIP Modern", L"Ouvrir MicroSIP Modern", L"MicroSIP Modern öffnen"));
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, 2, HostText(
		L"Salir", L"Exit", L"Surt", L"Sair", L"Quitter", L"Beenden"));
	POINT point;
	GetCursorPos(&point);
	SetForegroundWindow(hwnd);
	int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
		point.x, point.y, 0, hwnd, nullptr);
	DestroyMenu(menu);
	if (command == 1) {
		ShowMainWindow(hwnd);
	}
	else if (command == 2) {
		g_quitting = true;
		DestroyWindow(hwnd);
	}
}

void ApplyAlwaysOnTop(bool on)
{
	if (g_hwnd) {
		SetWindowPos(g_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
}

void OpenLogFile()
{
	wchar_t cwd[MAX_PATH] = { 0 };
	GetCurrentDirectoryW(MAX_PATH, cwd);
	std::wstring logPath = std::wstring(cwd) + L"\\microsip-modern.log";
	if (GetFileAttributesW(logPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		logPath = g_appDir + L"\\microsip-modern.log";
	}
	ShellExecuteW(nullptr, L"open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenWebsite()
{
	ShellExecuteW(nullptr, L"open", L"https://www.microsip.org", nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring PickFile(bool save, const wchar_t* filter, const wchar_t* defExt)
{
	wchar_t path[MAX_PATH] = { 0 };
	OPENFILENAMEW ofn = { 0 };
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = g_hwnd;
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = defExt;
	ofn.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
	return ok ? std::wstring(path) : std::wstring();
}

// "AnaPerez" <sip:204@10.0.0.1> -> nombre y numero por separado.
void SplitRemote(const std::string& remote, std::wstring& name, std::wstring& number)
{
	size_t quote = remote.find('"');
	if (quote != std::string::npos) {
		size_t end = remote.find('"', quote + 1);
		if (end != std::string::npos) {
			name = Utf8ToWide(remote.substr(quote + 1, end - quote - 1));
		}
	}
	size_t sip = remote.find("sip:");
	if (sip != std::string::npos) {
		size_t start = sip + 4;
		size_t stop = remote.find_first_of("@;>", start);
		number = Utf8ToWide(remote.substr(start,
			stop == std::string::npos ? std::string::npos : stop - start));
	}
	if (number.empty()) {
		number = name;
	}
}

// Notificacion flotante: solo cuando la app no esta en primer plano (si esta
// delante ya se ve la pantalla de llamada). Se llama con cada snapshot y al
// cambiar el foco, para que aparezca y desaparezca sola.
void SyncCallToast()
{
	if (!g_hwnd) {
		return;
	}

	CallToast::Info info;
	SipCore& core = SipCore::Instance();

	bool foreground = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)
		&& GetForegroundWindow() == g_hwnd;

	if (!foreground && core.GetSetting("callToast", "1") == "1" && g_calls.isArray()) {
		// Solo mientras entra la llamada: en cuanto se contesta desaparece.
		const Json::Value* picked = nullptr;
		for (const auto& call : g_calls) {
			if (call["state"].asString() == "incoming") {
				picked = &call;
				break;
			}
		}

		if (picked) {
			std::wstring name;
			std::wstring number;
			SplitRemote((*picked)["remote"].asString(), name, number);

			std::string lookup = number.empty() ? std::string() : WideToUtf8(number);
			std::wstring contact = Utf8ToWide(core.ContactNameFor(lookup));
			std::wstring title = contact.empty() ? name : contact;
			if (title.empty()) {
				title = number;
			}

			std::wstring status = HostText(L"Llamada entrante", L"Incoming call",
				L"Trucada entrant", L"Chamada recebida", L"Appel entrant",
				L"Eingehender Anruf");

			info.visible = true;
			info.ringing = true;
			info.callId = (*picked)["id"].asInt();
			info.name = title;
			info.detail = (number.empty() || number == title)
				? status
				: number + L" \u00B7 " + status;
			info.answerLabel = HostText(L"Contestar", L"Answer", L"Contestar",
				L"Atender", L"R\u00E9pondre", L"Annehmen");
			info.hangupLabel = HostText(L"Colgar", L"Hang up", L"Penjar",
				L"Desligar", L"Raccrocher", L"Auflegen");

			std::string accent = core.GetSetting("accentColor", "red");
			if (accent == "green") {
				info.accent = RGB(31, 157, 85);
			}
			else if (accent == "blue") {
				info.accent = RGB(37, 99, 235);
			}
			else {
				info.accent = RGB(198, 12, 48);
			}

			std::string corner = core.GetSetting("callToastPosition", "topright");
			if (corner == "topleft") {
				info.corner = 1;
			}
			else if (corner == "bottomright") {
				info.corner = 2;
			}
			else if (corner == "bottomleft") {
				info.corner = 3;
			}
		}
	}

	CallToast::Sync(g_hwnd, info);
}

void SendSnapshot()
{
	if (!g_webview) {
		return;
	}

	SipCore& core = SipCore::Instance();
	Json::Reader reader;

	Json::Value root;
	root["ev"] = "snapshot";

	Json::Value state;
	reader.parse(core.StateJson(), state);
	root["state"] = state;

	// Salidas de audio disponibles para el timbre (waveOut).
	Json::Value ringDevices(Json::arrayValue);
	UINT ringCount = waveOutGetNumDevs();
	for (UINT i = 0; i < ringCount; i++) {
		WAVEOUTCAPSW caps;
		if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
			ringDevices.append(WideToUtf8(caps.szPname));
		}
	}
	root["ringDevices"] = ringDevices;
	state["ringDevices"] = ringDevices;
	state["settings"]["ringDevice"] = WideToUtf8(g_ringDevice);
	// (log de dispositivos eliminado para no inundar el log)


	Json::Value history;
	reader.parse(core.HistoryJson(), history);
	root["history"] = history;

	Json::Value contacts;
	reader.parse(core.ContactsJson(), contacts);
	root["contacts"] = contacts;

	Json::Value messages;
	reader.parse(core.MessagesJson(), messages);
	root["messages"] = messages;

	// Estado para el auricular (LEDs y busylight)
	bool offhook = state["calls"].size() > 0;
	bool ring = false;
	for (const auto& call : state["calls"]) {
		if (call["state"].asString() == "incoming") {
			ring = true;
		}
	}
	JabraLink::Instance().SendState(offhook, ring, state["muted"].asBool());
	UpdateRingtone(ring);

	// Notificacion de llamada (solo si la app no esta delante).
	g_calls = state["calls"];
	SyncCallToast();

	std::string json = Json::FastWriter().write(root);
	g_webview->PostWebMessageAsJson(Utf8ToWide(json).c_str());
}

/* ---------------------------------------------------------------- */
/* Auricular HID: boton central de los EarPods USB-C (usage 0xCD)    */
/* ---------------------------------------------------------------- */

// Timer de pulsacion larga (3 lo usa el timbre; 1 y 2, SipCore).
constexpr UINT MSIP_TIMER_HIDHOLD = 4;

bool g_hidPressed = false;
bool g_hidLongDone = false;

// Pulsacion corta: contesta si suena, si no silencia el microfono.
void HidShortPress()
{
	SipCore& core = SipCore::Instance();
	if (core.HasIncomingCall()) {
		PJ_LOG(2, ("SipCore", "hid short press: answer"));
		core.AnswerIncoming();
	}
	else if (core.ActiveCallCount() > 0) {
		PJ_LOG(2, ("SipCore", "hid short press: mute toggle"));
		core.ToggleMute();
	}
	SendSnapshot();
}

// Pulsacion larga: rechaza el timbre o cuelga la llamada.
void HidLongPress()
{
	SipCore& core = SipCore::Instance();
	if (core.HasIncomingCall()) {
		PJ_LOG(2, ("SipCore", "hid long press: reject"));
		core.HangupIncoming();
	}
	else if (core.HasAnyCall()) {
		PJ_LOG(2, ("SipCore", "hid long press: hangup"));
		core.HangupAll();
	}
	SendSnapshot();
}

void OnHidButton(bool down)
{
	if (!down) {
		if (!g_hidPressed) {
			return;
		}
		g_hidPressed = false;
		KillTimer(g_hwnd, MSIP_TIMER_HIDHOLD);
		if (g_hidLongDone) {
			g_hidLongDone = false;
			return;
		}
		HidShortPress();
		return;
	}

	if (g_hidPressed) {
		return;
	}
	g_hidPressed = true;
	g_hidLongDone = false;
	SipCore& core = SipCore::Instance();
	if (core.HasAnyCall()) {
		int hold = atoi(core.GetSetting("hidHoldMs", "1200").c_str());
		if (hold < 300) {
			hold = 300;
		}
		SetTimer(g_hwnd, MSIP_TIMER_HIDHOLD, (UINT)hold, nullptr);
	}
}

BOOL CALLBACK EnumFindMainWindow(HWND hwnd, LPARAM lParam)
{
	wchar_t className[64] = { 0 };
	GetClassNameW(hwnd, className, 64);
	if (wcscmp(className, kWindowClass) == 0) {
		*(HWND*)lParam = hwnd;
		return FALSE;
	}
	return TRUE;
}

HWND FindMainWindow()
{
	HWND found = nullptr;
	EnumWindows(EnumFindMainWindow, (LPARAM)&found);
	return found;
}

std::wstring CommandFilePath()
{
	wchar_t temp[MAX_PATH] = { 0 };
	GetTempPathW(MAX_PATH, temp);
	return std::wstring(temp) + L"microsip-modern.cmd";
}

void SendCommandToExisting(HWND existing, const std::wstring& command)
{
	if (!existing) {
		return;
	}
	if (command == L"msip:quit") {
		PostMessageW(existing, WM_APP + 5, 1, 0);
		return;
	}
	if (command == L"msip:answer") {
		PostMessageW(existing, WM_APP + 5, 2, 0);
		return;
	}
	if (command == L"msip:hangupall") {
		PostMessageW(existing, WM_APP + 5, 3, 0);
		return;
	}
	if (command == L"msip:minimize") {
		PostMessageW(existing, WM_APP + 5, 4, 0);
		return;
	}
	if (command == L"msip:hold") {
		PostMessageW(existing, WM_APP + 5, 7, 0);
		return;
	}
	if (command == L"msip:micmute") {
		PostMessageW(existing, WM_APP + 5, 8, 0);
		return;
	}
	if (command == L"msip:micunmute") {
		PostMessageW(existing, WM_APP + 5, 9, 0);
		return;
	}
	if (command == L"msip:speakmute") {
		PostMessageW(existing, WM_APP + 5, 10, 0);
		return;
	}
	if (command == L"msip:speakunmute") {
		PostMessageW(existing, WM_APP + 5, 11, 0);
		return;
	}

	// Comandos con parametro (marcar, transferir): se pasan por fichero.
	std::wstring path = CommandFilePath();
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(file, command.c_str(),
			(DWORD)(command.size() * sizeof(wchar_t)), &written, nullptr);
		CloseHandle(file);
		PostMessageW(existing, WM_APP + 5, 6, 0);
	}
}

void RunCommandLine(const std::wstring& raw)
{
	std::wstring text = raw;
	while (!text.empty() && (text.front() == L'"' || text.front() == L' ')) {
		text.erase(text.begin());
	}
	while (!text.empty() && (text.back() == L'"' || text.back() == L' ')) {
		text.pop_back();
	}
	PJ_LOG(2, ("SipCore", "command line '%s'", WideToUtf8(text).c_str()));
	SipCore& core = SipCore::Instance();

	if (text.rfind(L"msip:", 0) == 0) {
		std::wstring cmd = text.substr(5);
		if (cmd == L"minimize") {
			if (g_hwnd) ShowWindow(g_hwnd, SW_HIDE);
		}
		else if (cmd == L"answer") {
			core.AnswerIncoming();
		}
		else if (cmd == L"hangupall") {
			core.HangupAll();
		}
		else if (cmd == L"hold") {
			core.HoldActive();
		}
		else if (cmd == L"micmute") {
			core.SetMute(true);
		}
		else if (cmd == L"micunmute") {
			core.SetMute(false);
		}
		else if (cmd == L"speakmute") {
			core.SetMuteOutput(true);
		}
		else if (cmd == L"speakunmute") {
			core.SetMuteOutput(false);
		}
		else if (cmd == L"quit") {
			if (g_hwnd) {
				g_quitting = true;
				DestroyWindow(g_hwnd);
			}
		}
		else if (cmd.rfind(L"transfer_", 0) == 0) {
			core.TransferActive(WideToUtf8(cmd.substr(9)));
		}
		else if (cmd == L"completetransfer") {
			core.CompleteAttendedTransfer();
		}
		else if (cmd == L"transferheld") {
			core.TransferHeldToActive();
		}
		else if (cmd.rfind(L"attended_", 0) == 0) {
			int id = core.FirstActiveCallId();
			if (id >= 0) {
				core.TransferAttended(id, WideToUtf8(cmd.substr(9)));
			}
		}
		else if (cmd == L"canceltransfer") {
			core.CancelAttendedTransfer();
		}
		else if (!cmd.empty()) {
			core.Dial(WideToUtf8(cmd));
		}
		SendSnapshot();
		return;
	}

	if (!text.empty()) {
		core.Dial(WideToUtf8(text));
		SendSnapshot();
	}
}

void HandleMessage(const std::wstring& message)
{
	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(WideToUtf8(message), root)) {
		return;
	}

	SipCore& core = SipCore::Instance();
	std::string cmd = root["cmd"].asString();
	PJ_LOG(2, ("SipCore", "cmd=%s", cmd.c_str()));

	if (cmd == "ready") {
		SendSnapshot();
	}
	else if (cmd == "dial") {
		core.Dial(root["number"].asString());
	}
	else if (cmd == "saveAccount") {
		MsipAccount account;
		account.index = root["index"].asInt();
		account.label = root["label"].asString();
		account.server = root["server"].asString();
		account.domain = root["domain"].asString();
		account.username = root["username"].asString();
		account.password = root["password"].asString();
		account.authId = root["authId"].asString();
		account.displayName = root["displayName"].asString();
		account.proxy = root["proxy"].asString();
		account.voicemail = root["voicemail"].asString();
		account.dialingPrefix = root["dialingPrefix"].asString();
		account.dialPlan = root["dialPlan"].asString();
		account.hideCid = root["hideCid"].asString();
		account.srtp = root["srtp"].asString();
		account.transport = root["transport"].asString();
		account.publicAddr = root["publicAddr"].asString();
		account.registerRefresh = root["registerRefresh"].asInt();
		account.keepAlive = root["keepAlive"].asInt();
		account.publish = root["publish"].asBool();
		account.ice = root["ice"].asBool();
		account.allowRewrite = root["allowRewrite"].asBool();
		account.disableSessionTimer = root["disableSessionTimer"].asBool();
		core.SaveAccount(account);
		SendSnapshot();
	}
	else if (cmd == "setting") {
		std::string key = root["key"].asString();
		std::string value = root["value"].asString();
		core.SetSetting(key.c_str(), value);
		if (key == "alwaysOnTop") {
			ApplyAlwaysOnTop(value == "1");
		}
		else if (key == "jabraIntegration") {
			if (value == "1") {
				JabraLink::Instance().Start(g_appDir, g_hwnd);
			}
			else {
				JabraLink::Instance().Stop();
			}
		}
		else if (key == "hidHeadset") {
			if (value == "1") {
				HidLink::Instance().Start(g_hwnd,
					Utf8ToWide(core.GetSetting("hidHeadsetIds", "")));
			}
			else {
				HidLink::Instance().Stop();
			}
		}
		else if (key == "teamsPresence") {
			core.UpdateTeamsPresence();
		}
		else if (key == "callToast") {
			SyncCallToast();
		}
		else if (key == "volumeRing") {
			g_ringVolume = atoi(value.c_str());
			if (g_ringVolume < 0 || g_ringVolume > 100) {
				g_ringVolume = 100;
			}
			g_ringData.clear();
			if (g_ringing) {
				g_ringing = false;
				StopRingWave();
				UpdateRingtone(true);
			}
		}
		else if (key == "ringDevice") {
			g_ringDevice = Utf8ToWide(value);
			if (g_ringing) {
				g_ringing = false;
				StopRingWave();
				UpdateRingtone(true);
			}
		}
		core.ApplyGlobalSettings();
		SendSnapshot();
	}
	else if (cmd == "alwaysOnTop") {
		bool on = root["on"].asBool();
		core.SetAlwaysOnTop(on);
		ApplyAlwaysOnTop(on);
		SendSnapshot();
	}
	else if (cmd == "saveShortcut") {
		core.SetShortcut(root["index"].asInt(), root["label"].asString(), root["number"].asString());
		SendSnapshot();
	}
	else if (cmd == "deleteShortcut") {
		core.DeleteShortcut(root["index"].asInt());
		SendSnapshot();
	}
	else if (cmd == "openLog") {
		OpenLogFile();
	}
	else if (cmd == "website") {
		OpenWebsite();
	}
	else if (cmd == "ringTest") {
		UpdateRingtone(true);
		if (g_hwnd) {
			SetTimer(g_hwnd, 3, 3000, nullptr);
		}
	}
	else if (cmd == "saveContact") {
		core.SaveContact(root["id"].asInt(), root["name"].asString(),
			root["number"].asString(), root["info"].asString(),
			root["presence"].asBool());
		SendSnapshot();
	}
	else if (cmd == "deleteContact") {
		core.DeleteContact(root["id"].asInt());
		SendSnapshot();
	}
	else if (cmd == "deleteHistory") {
		core.DeleteHistory(root["index"].asInt());
		SendSnapshot();
	}
	else if (cmd == "clearHistory") {
		core.ClearHistory();
		SendSnapshot();
	}
	else if (cmd == "exportHistory") {
		std::wstring path = PickFile(true, L"CSV (*.csv)\0*.csv\0", L"csv");
		if (!path.empty()) core.ExportHistoryCsv(path);
	}
	else if (cmd == "importHistory") {
		std::wstring path = PickFile(false, L"CSV (*.csv)\0*.csv\0", L"csv");
		if (!path.empty()) core.ImportHistoryCsv(path);
		SendSnapshot();
	}
	else if (cmd == "exportContacts") {
		std::wstring path = PickFile(true, L"CSV (*.csv)\0*.csv\0", L"csv");
		if (!path.empty()) core.ExportContactsCsv(path);
	}
	else if (cmd == "importContacts") {
		std::wstring path = PickFile(false, L"CSV (*.csv)\0*.csv\0", L"csv");
		if (!path.empty()) core.ImportContactsCsv(path);
		SendSnapshot();
	}
	else if (cmd == "exportConfig") {
		std::wstring path = PickFile(true, L"INI (*.ini)\0*.ini\0", L"ini");
		if (!path.empty()) core.ExportConfig(path);
	}
	else if (cmd == "importConfig") {
		std::wstring path = PickFile(false, L"INI (*.ini)\0*.ini\0", L"ini");
		if (!path.empty()) core.ImportConfig(path);
		SendSnapshot();
	}
	else if (cmd == "voicemail") {
		core.DialVoicemail();
	}
	else if (cmd == "pickup") {
		core.Pickup(root["number"].asString());
		SendSnapshot();
	}
	else if (cmd == "setAccount") {
		core.SetActiveAccount(root["index"].asInt());
		SendSnapshot();
	}
	else if (cmd == "importAccounts") {
		core.ImportAccountsFromOriginal();
		SendSnapshot();
	}
	else if (cmd == "sendMessage") {
		core.SendMessage(root["to"].asString(), root["body"].asString());
		SendSnapshot();
	}
	else if (cmd == "answer") {
		core.Answer(root["id"].asInt());
	}
	else if (cmd == "hangup") {
		core.Hangup(root["id"].asInt());
	}
	else if (cmd == "hangupAll") {
		core.HangupAll();
	}
	else if (cmd == "hold") {
		core.Hold(root["id"].asInt(), root["on"].asBool());
	}
	else if (cmd == "swap") {
		core.SwapTo(root["id"].asInt());
		SendSnapshot();
	}
	else if (cmd == "muteOut") {
		core.SetMuteOutput(root["on"].asBool());
		SendSnapshot();
	}
	else if (cmd == "addCall") {
		core.AddCall(root["number"].asString());
		SendSnapshot();
	}
	else if (cmd == "merge") {
		core.MergeCalls();
		SendSnapshot();
	}
	else if (cmd == "attendedTransfer") {
		core.TransferAttended(root["id"].asInt(), root["number"].asString());
		SendSnapshot();
	}
	else if (cmd == "completeTransfer") {
		core.CompleteAttendedTransfer();
		SendSnapshot();
	}
	else if (cmd == "cancelTransfer") {
		core.CancelAttendedTransfer();
		SendSnapshot();
	}
	else if (cmd == "mute") {
		core.SetMute(root["on"].asBool());
		SendSnapshot();
	}
	else if (cmd == "dtmf") {
		core.SendDtmf(root["digit"].asString());
	}
	else if (cmd == "transfer") {
		core.Transfer(root["id"].asInt(), root["number"].asString());
		SendSnapshot();
	}
	else if (cmd == "copy") {
		std::string text = root["text"].asString();
		if (OpenClipboard(g_hwnd)) {
			EmptyClipboard();
			std::wstring wide = Utf8ToWide(text);
			size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (memory) {
				void* destination = GlobalLock(memory);
				if (destination) {
					memcpy(destination, wide.c_str(), bytes);
					GlobalUnlock(memory);
					SetClipboardData(CF_UNICODETEXT, memory);
				}
				else {
					GlobalFree(memory);
				}
			}
			CloseClipboard();
		}
	}
	else if (cmd == "paste") {
		std::wstring text;
		if (OpenClipboard(g_hwnd)) {
			HANDLE data = GetClipboardData(CF_UNICODETEXT);
			if (data) {
				const wchar_t* source = (const wchar_t*)GlobalLock(data);
				if (source) {
					text = source;
					GlobalUnlock(data);
				}
			}
			CloseClipboard();
		}
		if (g_webview) {
			Json::Value message;
			message["ev"] = "clipboard";
			message["text"] = WideToUtf8(text);
			g_webview->PostWebMessageAsJson(Utf8ToWide(Json::FastWriter().write(message)).c_str());
		}
	}
	else if (cmd == "quit") {
		if (g_hwnd) {
			g_quitting = true;
			DestroyWindow(g_hwnd);
		}
	}
	else if (cmd == "jserror") {
		PJ_LOG(1, ("SipCore", "JS error: %s", root["message"].asString().c_str()));
	}
	else if (cmd == "volume") {
		core.SetVolume(root["output"].asBool(), root["value"].asInt());
	}
	else if (cmd == "dnd") {
		core.SetDnd(root["on"].asBool());
		SendSnapshot();
	}
	else if (cmd == "record") {
		core.SetRecord(root["on"].asBool());
		SendSnapshot();
	}
	else if (cmd == "accent") {
		core.SetSetting("accentColor", root["name"].asString());
	}
	else if (cmd == "theme") {
		std::string mode = root["mode"].asString();
		core.SetSetting("colorMode", mode == "system" ? "0" : (mode == "dark" ? "2" : "1"));
		if (mode == "dark") {
			g_darkTitle = true;
		}
		else if (mode == "light") {
			g_darkTitle = false;
		}
		else {
			g_darkTitle = IsSystemDark();
		}
		ApplyDarkTitleBar();
		InvalidateRect(g_hwnd, nullptr, TRUE);
	}
}

void ResizeWebView()
{
	if (!g_controller) {
		return;
	}
	RECT bounds;
	GetClientRect(g_hwnd, &bounds);
	g_controller->put_Bounds(bounds);
}

void CreateWebView()
{
	std::wstring userData = g_appDir + L"\\webview-data";
	std::wstring url = std::wstring(L"https://") + kVirtualHost + L"/index.html?v=31";

	CreateCoreWebView2EnvironmentWithOptions(nullptr, userData.c_str(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[url](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
				if (FAILED(result) || !environment) {
					return result;
				}

				environment->CreateCoreWebView2Controller(g_hwnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[url](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
							if (FAILED(result) || !controller) {
								return result;
							}

							g_controller = controller;
							g_controller->get_CoreWebView2(&g_webview);
							ResizeWebView();
							g_controller->put_IsVisible(TRUE);

							// Quita el menu web (Imprimir, Guardar como, Inspeccionar...).
							ComPtr<ICoreWebView2Settings> settings;
							if (g_webview && SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
								settings->put_AreDefaultContextMenusEnabled(FALSE);
								settings->put_AreDevToolsEnabled(FALSE);
								settings->put_IsStatusBarEnabled(FALSE);
								settings->put_IsZoomControlEnabled(FALSE);
								settings->put_IsBuiltInErrorPageEnabled(FALSE);
							}

							ComPtr<ICoreWebView2_3> webview3;
							if (SUCCEEDED(g_webview.As(&webview3))) {
								webview3->SetVirtualHostNameToFolderMapping(
									kVirtualHost,
									g_webDir.c_str(),
									COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
							}

							EventRegistrationToken token;
							g_webview->add_WebMessageReceived(
								Callback<ICoreWebView2WebMessageReceivedEventHandler>(
									[](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
										LPWSTR message = nullptr;
										if (SUCCEEDED(args->get_WebMessageAsJson(&message)) && message) {
											HandleMessage(message);
											CoTaskMemFree(message);
										}
										return S_OK;
									}).Get(),
								&token);

							g_webview->Navigate(url.c_str());
							return S_OK;
						}).Get());

				return S_OK;
			}).Get());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_CREATE:
		g_hwnd = hwnd;
		ApplyDarkTitleBar();
		CreateWebView();
		return 0;

	case WM_SIZE:
		ResizeWebView();
		SyncCallToast();
		return 0;

	case WM_ACTIVATE:
		// Al pasar a primer plano la notificacion sobra; al perderlo, aparece.
		SyncCallToast();
		return 0;

	case WM_SHOWWINDOW:
		SyncCallToast();
		return 0;

	case MSIP_TOAST_ANSWER:
		SipCore::Instance().Answer((int)wParam);
		return 0;

	case MSIP_TOAST_HANGUP:
		SipCore::Instance().Hangup((int)wParam);
		return 0;

	case MSIP_TOAST_OPEN:
		ShowMainWindow(hwnd);
		return 0;

	case WM_APP + 1:
		SendSnapshot();
		return 0;

	case WM_APP + 2: {
		std::string* payload = (std::string*)lParam;
		if (payload) {
			JabraLink::Instance().HandleEvent(*payload);
			delete payload;
			SendSnapshot();
		}
		return 0;
	}

	case WM_ERASEBKGND: {
		RECT rect;
		GetClientRect(hwnd, &rect);
		HBRUSH brush = CreateSolidBrush(g_darkTitle ? RGB(23, 26, 31) : RGB(255, 255, 255));
		FillRect((HDC)wParam, &rect, brush);
		DeleteObject(brush);
		return 1;
	}

	case WM_CLOSE:
		// Cerrar (X o Alt+F4) oculta a la bandeja; para salir, menu Salir.
		ShowWindow(hwnd, SW_HIDE);
		return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_CLOSE) {
			ShowWindow(hwnd, SW_HIDE);
			return 0;
		}
		break;

	case WM_APP + 3:
		if (LOWORD(lParam) == WM_LBUTTONUP) {
			ShowMainWindow(hwnd);
		}
		else if (LOWORD(lParam) == WM_RBUTTONUP) {
			ShowTrayMenu(hwnd);
		}
		return 0;

	case WM_APP + 4:
		ShowMainWindow(hwnd);
		return 0;

	case WM_APP + 5: {
		SipCore& core = SipCore::Instance();
		switch (wParam) {
		case 1:
			g_quitting = true;
			DestroyWindow(hwnd);
			break;
		case 2:
			core.AnswerIncoming();
			SendSnapshot();
			break;
		case 3:
			core.HangupAll();
			SendSnapshot();
			break;
		case 4:
			ShowWindow(hwnd, SW_HIDE);
			break;
		case 5:
			ShowMainWindow(hwnd);
			break;
		case 6: {
			std::wstring path = CommandFilePath();
			HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE) {
				wchar_t buffer[1024] = { 0 };
				DWORD read = 0;
				ReadFile(file, buffer, sizeof(buffer) - sizeof(wchar_t), &read, nullptr);
				CloseHandle(file);
				DeleteFileW(path.c_str());
				RunCommandLine(buffer);
			}
			break;
		}
		case 7:
			core.HoldActive();
			SendSnapshot();
			break;
		case 8:
			core.SetMute(true);
			SendSnapshot();
			break;
		case 9:
			core.SetMute(false);
			SendSnapshot();
			break;
		case 10:
			core.SetMuteOutput(true);
			SendSnapshot();
			break;
		case 11:
			core.SetMuteOutput(false);
			SendSnapshot();
			break;
		default:
			break;
		}
		return 0;
	}

	case WM_COPYDATA: {
		COPYDATASTRUCT* data = (COPYDATASTRUCT*)lParam;
		if (data && data->lpData && data->cbData > 0) {
			std::wstring text((const wchar_t*)data->lpData, data->cbData / sizeof(wchar_t));
			while (!text.empty() && text.back() == 0) {
				text.pop_back();
			}
			RunCommandLine(text);
		}
		return TRUE;
	}

	case WM_TIMER:
		if (wParam == 3) {
			UpdateRingtone(false);
			KillTimer(hwnd, 3);
			return 0;
		}
		if (wParam == MSIP_TIMER_HIDHOLD) {
			KillTimer(hwnd, MSIP_TIMER_HIDHOLD);
			if (g_hidPressed && !g_hidLongDone) {
				g_hidLongDone = true;
				HidLongPress();
			}
			return 0;
		}
		SipCore::Instance().OnTimer(wParam);
		return 0;

	case MSIP_HID_BUTTON_MSG:
		OnHidButton(wParam != 0);
		return 0;

	case MSIP_TEAMS_STATE_MSG:
		TeamsPresence::Apply(wParam != 0);
		return 0;

	case WM_DESTROY:
		PlaySoundW(nullptr, nullptr, 0);
		RemoveTrayIcon();
		CallToast::Close();
		HidLink::Instance().Stop();
		JabraLink::Instance().Stop();
		SipCore::Instance().Shutdown();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	g_appDir = GetAppDir();
	g_webDir = g_appDir + L"\\web";

	// Argumentos de linea de comandos (sin el ejecutable).
	std::wstring fullCommand = GetCommandLineW();
	std::wstring args;
	if (!fullCommand.empty() && fullCommand.front() == L'"') {
		size_t close = fullCommand.find(L'"', 1);
		if (close != std::wstring::npos) {
			args = fullCommand.substr(close + 1);
		}
	}
	else {
		size_t space = fullCommand.find(L' ');
		if (space != std::wstring::npos) {
			args = fullCommand.substr(space + 1);
		}
	}
	while (!args.empty() && args.front() == L' ') {
		args.erase(args.begin());
	}
	while (!args.empty() && args.back() == L' ') {
		args.pop_back();
	}
	bool cmdMinimized = (args == L"/minimized");
	bool cmdExit = (args == L"/exit");
	bool cmdAnswer = (args == L"/answer");
	bool cmdHangupAll = (args == L"/hangupall");

	// Instancia unica (se puede desactivar en Ajustes).
	std::wstring iniPath = g_appDir + L"\\microsip.ini";
	wchar_t ringText[16] = { 0 };
	GetPrivateProfileStringW(L"Settings", L"volumeRing", L"100", ringText, 16, iniPath.c_str());
	g_ringVolume = _wtoi(ringText);
	if (g_ringVolume < 0 || g_ringVolume > 100) {
		g_ringVolume = 100;
	}
	wchar_t ringDevice[256] = { 0 };
	GetPrivateProfileStringW(L"Settings", L"ringDevice", L"", ringDevice, 256, iniPath.c_str());
	g_ringDevice = ringDevice;
	wchar_t singleText[8] = { 0 };
	GetPrivateProfileStringW(L"Settings", L"singleInstance", L"1", singleText, 8, iniPath.c_str());
	HANDLE instanceMutex = nullptr;
	if (_wtoi(singleText) != 0) {
		instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\MicroSIPModernInstance");
		if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
			// Ya hay una instancia: se le pasa el comando (msip:..., numero)
			// o se muestra la ventana existente.
			HWND existing = FindMainWindow();
			if (existing) {
				std::wstring command;
				if (cmdExit) {
					command = L"msip:quit";
				}
				else if (cmdAnswer) {
					command = L"msip:answer";
				}
				else if (cmdHangupAll) {
					command = L"msip:hangupall";
				}
				else if (cmdMinimized) {
					command = L"msip:minimize";
				}
				else if (!args.empty() && !cmdMinimized) {
					command = args;
				}
				if (!command.empty()) {
					SendCommandToExisting(existing, command);
				}
				else {
					PostMessageW(existing, WM_APP + 4, 0, 0);
				}
			}
			CloseHandle(instanceMutex);
			CoUninitialize();
			return 0;
		}
	}

	if (cmdExit || cmdAnswer || cmdHangupAll) {
		if (instanceMutex) {
			CloseHandle(instanceMutex);
		}
		CoUninitialize();
		return 0;
	}


	HICON iconBig = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
	HICON iconSmall = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

	WNDCLASSEX wc = { 0 };
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = kWindowClass;
	wc.hIcon = iconBig;
	wc.hIconSm = iconSmall;
	RegisterClassEx(&wc);

	RECT rect = { 0, 0, 420, 664 };
	DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect(&rect, windowStyle, FALSE);

	HWND hwnd = CreateWindowEx(
		0,
		kWindowClass,
		kWindowTitle,
		windowStyle,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, instance, nullptr);

	if (!hwnd) {
		return 1;
	}

	if (iconBig) {
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
	}
	if (iconSmall) {
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
	}

	AddTrayIcon(hwnd, instance);

	SipCore::Instance().Init(hwnd, g_appDir);
	ApplyAlwaysOnTop(SipCore::Instance().AlwaysOnTop());
	JabraLink::Instance().Start(g_appDir, hwnd);
	if (SipCore::Instance().GetSetting("hidHeadset", "1") == "1") {
		HidLink::Instance().Start(hwnd,
			Utf8ToWide(SipCore::Instance().GetSetting("hidHeadsetIds", "")));
	}
	SipCore::Instance().LoadContacts(g_appDir + L"\\contacts.db");

	std::string colorMode = SipCore::Instance().GetSetting("colorMode", "0");
	g_darkTitle = (colorMode == "2") || (colorMode == "0" && IsSystemDark());
	ApplyDarkTitleBar();

	int show = showCommand == 0 ? SW_SHOWNORMAL : showCommand;
	if (SipCore::Instance().GetSetting("startMinimized", "0") == "1") {
		show = SW_HIDE;
	}
	if (cmdMinimized) {
		show = SW_HIDE;
	}
	ShowWindow(hwnd, show);
	UpdateWindow(hwnd);
	PostMessage(hwnd, WM_APP + 1, 0, 0);

	if (!args.empty() && !cmdMinimized) {
		RunCommandLine(args);
	}

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (instanceMutex) {
		CloseHandle(instanceMutex);
	}
	CoUninitialize();
	return (int)msg.wParam;
}
