/*
 * MicroSIP Modern - enlace con JabraBridge.exe (SDK oficial de Jabra).
 */

#include "JabraLink.h"
#include "SipCore.h"

#include <json/json.h>

#include <string>

namespace {

std::wstring FindBridge(const std::wstring& appDir)
{
	const wchar_t* candidates[] = {
		L"\\JabraBridge\\JabraBridge.exe",
		L"\\JabraBridge.exe",
		L"\\jabra\\JabraBridge.exe",
	};
	for (const wchar_t* candidate : candidates) {
		std::wstring path = appDir + candidate;
		if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
			return path;
		}
	}
	return std::wstring();
}

} // namespace

JabraLink& JabraLink::Instance()
{
	static JabraLink instance;
	return instance;
}

bool JabraLink::WriteLine(const std::string& line)
{
	if (!stdin_) {
		return false;
	}
	std::string data = line + "\n";
	DWORD written = 0;
	if (csInit_) {
		EnterCriticalSection(&cs_);
	}
	BOOL ok = WriteFile(stdin_, data.data(), (DWORD)data.size(), &written, nullptr);
	if (csInit_) {
		LeaveCriticalSection(&cs_);
	}
	return ok != FALSE;
}

DWORD WINAPI JabraLink::ThreadProc(LPVOID param)
{
	JabraLink* self = (JabraLink*)param;
	std::string buffer;
	char chunk[2048];

	while (self->running_) {
		DWORD read = 0;
		if (!ReadFile(self->stdout_, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
			break;
		}
		buffer.append(chunk, read);

		size_t pos;
		while ((pos = buffer.find('\n')) != std::string::npos) {
			std::string line = buffer.substr(0, pos);
			buffer.erase(0, pos + 1);
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) {
				continue;
			}
			std::string* payload = new std::string(line);
			if (!self->hwnd_ || !PostMessage(self->hwnd_, WM_APP + 2, 0, (LPARAM)payload)) {
				delete payload;
			}
		}
	}
	return 0;
}

bool JabraLink::Start(const std::wstring& appDir, HWND hwnd)
{
	if (running_) {
		return true;
	}
	hwnd_ = hwnd;

	std::wstring bridge = FindBridge(appDir);
	if (bridge.empty()) {
		return false;
	}

	if (!csInit_) {
		InitializeCriticalSection(&cs_);
		csInit_ = true;
	}

	SECURITY_ATTRIBUTES sa = { 0 };
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
	if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
		return false;
	}
	SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
	if (!CreatePipe(&inRead, &inWrite, &sa, 0)) {
		CloseHandle(outRead);
		CloseHandle(outWrite);
		return false;
	}
	SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFO si = { 0 };
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = inRead;
	si.hStdOutput = outWrite;
	si.hStdError = outWrite;

	std::wstring commandLine = L"\"" + bridge + L"\"";
	std::wstring workDir = bridge.substr(0, bridge.find_last_of(L"\\/"));

	PROCESS_INFORMATION pi = { 0 };
	BOOL created = CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);

	CloseHandle(inRead);
	CloseHandle(outWrite);

	if (!created) {
		CloseHandle(outRead);
		CloseHandle(inWrite);
		return false;
	}

	CloseHandle(pi.hThread);
	process_ = pi.hProcess;
	stdout_ = outRead;
	stdin_ = inWrite;
	running_ = true;

	thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
	if (!thread_) {
		Stop();
		return false;
	}
	return true;
}

void JabraLink::Stop()
{
	if (!running_ && !process_) {
		return;
	}
	running_ = false;

	if (stdin_) {
		WriteLine("{\"cmd\":\"quit\"}");
		CloseHandle(stdin_);
		stdin_ = nullptr;
	}
	if (process_) {
		if (WaitForSingleObject(process_, 2000) == WAIT_TIMEOUT) {
			TerminateProcess(process_, 0);
		}
		CloseHandle(process_);
		process_ = nullptr;
	}
	if (stdout_) {
		CloseHandle(stdout_);
		stdout_ = nullptr;
	}
	if (thread_) {
		WaitForSingleObject(thread_, 2000);
		CloseHandle(thread_);
		thread_ = nullptr;
	}
	lastValid_ = false;
}

void JabraLink::SendState(bool offhook, bool ring, bool mute)
{
	if (!running_) {
		return;
	}
	if (lastValid_ && lastOffhook_ == offhook && lastRing_ == ring && lastMute_ == mute) {
		return;
	}
	DWORD now = GetTickCount();
	if (ring && (!lastValid_ || !lastRing_)) {
		ringSince_ = now;
	}
	lastValid_ = true;
	lastOffhook_ = offhook;
	lastRing_ = ring;
	lastMute_ = mute;

	char buffer[256];
	sprintf_s(buffer, "{\"cmd\":\"state\",\"offhook\":%s,\"ring\":%s,\"mute\":%s,\"hold\":false}",
		offhook ? "true" : "false", ring ? "true" : "false", mute ? "true" : "false");
	WriteLine(buffer);

	if (busylightSupported_) {
		char busy[96];
		sprintf_s(busy, "{\"cmd\":\"busylight\",\"on\":%s}", offhook ? "true" : "false");
		WriteLine(busy);
	}
}

void JabraLink::HandleEvent(const std::string& json)
{
	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(json, root)) {
		return;
	}

	std::string event = root["ev"].asString();
	SipCore& core = SipCore::Instance();

	if (event == "busylight") {
		busylightSupported_ = root["supported"].asBool();
		return;
	}
	if (event != "signal") {
		return;
	}

	std::string name = root["name"].asString();
	bool value = root["value"].asBool();
	std::string change = root["change"].asString();

	PJ_LOG(2, ("JabraLink", "signal %s value=%d change=%s",
		name.c_str(), value ? 1 : 0, change.c_str()));

	if (name == "HookSwitch") {
		// Absolute trae el estado fisico del gancho; Relative es una pulsacion
		// que lo alterna. Un true siempre contesta (si no hay entrante,
		// AnswerIncoming no hace nada); es lo que manda el SDK al bajar el
		// boom para contestar y como acuse de OffHook(true).
		bool offhook = (change == "Absolute") ? value : !hookOff_;
		hookOff_ = offhook;

		if (offhook) {
			core.AnswerIncoming();
		}
		else {
			// Un false mientras solo timbra es el acuse de OffHook(false) del
			// estado de timbre, no una orden de colgar: se ignora.
			if (core.HasIncomingCall() && core.ActiveCallCount() == 0) {
				return;
			}
			core.HangupAll();
		}
	}
	else if (name == "PhoneMute") {
		// El dispositivo devuelve un PhoneMute como acuse de cada Mute() que
		// le manda el host. Interpretarlo como pulsacion hace que el mute
		// oscile en bucle; el puente filtra ese acuse antes de enviarlo.
		DWORD now = GetTickCount();

		// Bajar el boom para contestar tambien llega como PhoneMute (el
		// dispositivo no manda HookSwitch en todos los modelos): si hay una
		// entrante timbrando, se contesta y se deja el micro sin silenciar.
		if (core.HasIncomingCall() && ringSince_ != 0 && (DWORD)(now - ringSince_) > 1200) {
			core.SetMute(false);
			core.AnswerIncoming();
			return;
		}

		bool muted = (change == "Absolute") ? value : !core.Muted();
		if (muted != core.Muted()) {
			core.SetMute(muted);
		}
	}
	else if (name == "RejectCall") {
		core.HangupIncoming();
	}
}
