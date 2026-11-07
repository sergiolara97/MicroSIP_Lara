/*
 * Copyright (C) 2011-2026 MicroSIP (http://www.microsip.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "stdafx.h"

#define THIS_FILENAME "Jabra.cpp"

#include "Jabra.h"

#include <pjsua-lib/pjsua.h>
#include <string>

#include "settings.h"
#include "global.h"
#include "mainDlg.h"
#include "Hid.h"
#include "json.h"

// ---------------------------------------------------------------------------
// Estado del proceso puente
// ---------------------------------------------------------------------------

static HANDLE s_hProcess = NULL;
static HANDLE s_hStdin = NULL;
static HANDLE s_hStdout = NULL;
static HANDLE s_hThread = NULL;
static volatile bool s_running = false;

static CRITICAL_SECTION s_cs;
static bool s_csInit = false;

// Estado del dispositivo
static bool s_enabled = false;
static bool s_devicePresent = false;
static bool s_busylightSupported = false;

// Estado fisico del auricular (gancho / mute)
static bool s_offhook = false;
static bool s_mute = false;

// Estado de MicroSIP que se refleja en el auricular
static bool s_callActive = false;
static bool s_callingRing = false;
static bool s_hold = false;

// Ultimo estado enviado (para no repetir comandos)
static bool s_lastValid = false;
static bool s_lastOffhook = false;
static bool s_lastRing = false;
static bool s_lastMute = false;
static bool s_lastHold = false;

static bool s_lastBusylightValid = false;
static bool s_lastBusylight = false;

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

static CString GetExeDir()
{
	TCHAR path[MAX_PATH];
	if (!GetModuleFileName(NULL, path, MAX_PATH)) {
		return CString();
	}
	CString dir(path);
	int pos = dir.ReverseFind(_T('\\'));
	if (pos > 0) {
		dir = dir.Left(pos);
	}
	return dir;
}

static CString FindBridge()
{
	CString dir = GetExeDir();
	if (dir.IsEmpty()) {
		return CString();
	}

	CString candidates[3];
	candidates[0].Format(_T("%s\\JabraBridge\\JabraBridge.exe"), (LPCTSTR)dir);
	candidates[1].Format(_T("%s\\JabraBridge.exe"), (LPCTSTR)dir);
	candidates[2].Format(_T("%s\\jabra\\JabraBridge.exe"), (LPCTSTR)dir);

	for (int i = 0; i < 3; i++) {
		if (GetFileAttributes(candidates[i]) != INVALID_FILE_ATTRIBUTES) {
			return candidates[i];
		}
	}
	return CString();
}

static CStringA ToUtf8(const CString& text)
{
	CStringA result;
	if (text.IsEmpty()) {
		return result;
	}
	int size = WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), NULL, 0, NULL, NULL);
	if (size <= 0) {
		return result;
	}
	char* buffer = result.GetBuffer(size);
	WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), buffer, size, NULL, NULL);
	result.ReleaseBuffer(size);
	return result;
}

static bool WriteLine(const CStringA& line)
{
	if (!s_hStdin) {
		return false;
	}

	std::string data((LPCSTR)line, line.GetLength());
	data += "\n";

	DWORD written = 0;
	if (s_csInit) {
		EnterCriticalSection(&s_cs);
	}
	BOOL ok = WriteFile(s_hStdin, data.data(), (DWORD)data.size(), &written, NULL);
	if (s_csInit) {
		LeaveCriticalSection(&s_cs);
	}
	return ok != FALSE;
}

static void SendState(bool force)
{
	if (!s_running) {
		return;
	}

	if (!force && s_lastValid
		&& s_lastOffhook == s_callActive
		&& s_lastRing == s_callingRing
		&& s_lastMute == s_mute
		&& s_lastHold == s_hold) {
		return;
	}

	CStringA json;
	json.Format("{\"cmd\":\"state\",\"offhook\":%s,\"ring\":%s,\"mute\":%s,\"hold\":%s}",
		s_callActive ? "true" : "false",
		s_callingRing ? "true" : "false",
		s_mute ? "true" : "false",
		s_hold ? "true" : "false");

	WriteLine(json);

	s_lastValid = true;
	s_lastOffhook = s_callActive;
	s_lastRing = s_callingRing;
	s_lastMute = s_mute;
	s_lastHold = s_hold;
}

static void LogInfo(const CString& message)
{
	if (is_pjsua_running()) {
		PJ_LOG(3, (THIS_FILENAME, "%ls", (LPCTSTR)message));
	}
}

// ---------------------------------------------------------------------------
// Presentacion del dispositivo
// ---------------------------------------------------------------------------

static void SetDevicePresent(bool present)
{
	if (s_devicePresent == present) {
		return;
	}

	s_devicePresent = present;
	s_enabled = present;

	if (present) {
		LogInfo(_T("Auriculares Jabra detectados: se usa el SDK de Jabra"));
		// El auricular Jabra manda: se cierra el acceso HID generico para no
		// procesar dos veces los mismos botones.
		if (Hid::IsOpen()) {
			Hid::CloseDevice(false);
		}
	}
	else {
		LogInfo(_T("Sin auriculares Jabra: se vuelve al soporte HID generico"));
		if (accountSettings.headsetSupport && !Hid::IsOpen()) {
			Hid::OpenDevice();
		}
	}

	SendState(true);
}

// ---------------------------------------------------------------------------
// Senales del auricular
// ---------------------------------------------------------------------------

static void HandleSignal(const Json::Value& root)
{
	std::string name = root["name"].asString();
	bool value = root["value"].asBool();
	std::string change = root["change"].asString();
	bool absolute = (change == "Absolute");

	if (name == "HookSwitch") {
		bool offhook = absolute ? value : !s_offhook;
		bool previous = s_offhook;
		s_offhook = offhook;

		if (offhook == previous) {
			return;
		}

		LogInfo(offhook ? _T("Jabra: gancho descolgado") : _T("Jabra: gancho colgado"));

		if (offhook) {
			if (!s_callActive) {
				if (s_callingRing) {
					if (mainDlg) {
						mainDlg->CommandCallAnswer();
					}
				}
				else if (mainDlg) {
					mainDlg->pageDialer->Action(ACTION_CALL);
				}
			}
			else if (s_callingRing) {
				// Llamada en espera: el boton del auricular la rechaza.
				if (mainDlg) {
					mainDlg->CommandCallReject();
				}
			}
		}
		else {
			if (s_callActive) {
				msip_call_hangup_all_noincoming(true);
			}
			else if (s_callingRing) {
				if (mainDlg) {
					mainDlg->CommandCallReject();
				}
			}
		}
	}
	else if (name == "PhoneMute") {
		bool muted = absolute ? value : !s_mute;
		if (muted == s_mute) {
			return;
		}
		s_mute = muted;

		LogInfo(muted ? _T("Jabra: microfono silenciado") : _T("Jabra: microfono activo"));

		if (accountSettings.jabraBoomHangup && muted && s_callActive) {
			// "Bajar el microfono" (plegar el boom) cuelga la llamada.
			LogInfo(_T("Jabra: boom plegado, se cuelga la llamada"));
			msip_call_hangup_all_noincoming(true);
		}
		else if (mainDlg) {
			mainDlg->pageDialer->MuteInput(muted);
		}
	}
	else if (name == "Redial") {
		if (value && mainDlg) {
			LogInfo(_T("Jabra: rellamada"));
			mainDlg->Redial();
		}
	}
	else if (name == "RejectCall") {
		if (mainDlg) {
			LogInfo(_T("Jabra: rechazar llamada"));
			mainDlg->CommandCallReject();
		}
	}
	else if (name == "Flash") {
		LogInfo(_T("Jabra: flash (no implementado)"));
	}
}

// ---------------------------------------------------------------------------
// Hilo lector del puente
// ---------------------------------------------------------------------------

static DWORD WINAPI ReaderThread(LPVOID)
{
	std::string buffer;
	char chunk[4096];

	while (s_running) {
		DWORD read = 0;
		if (!ReadFile(s_hStdout, chunk, sizeof(chunk), &read, NULL) || read == 0) {
			break;
		}

		buffer.append(chunk, read);

		size_t pos;
		while ((pos = buffer.find('\n')) != std::string::npos) {
			std::string line = buffer.substr(0, pos);
			buffer.erase(0, pos + 1);

			if (!line.empty() && line[line.size() - 1] == '\r') {
				line.erase(line.size() - 1);
			}
			if (line.empty()) {
				continue;
			}

			int size = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0);
			if (size <= 0) {
				continue;
			}
			CString* payload = new CString();
			LPTSTR target = payload->GetBuffer(size);
			MultiByteToWideChar(CP_UTF8, 0, line.c_str(), (int)line.size(), target, size);
			payload->ReleaseBuffer(size);

			if (!mainDlg || !mainDlg->PostMessage(UM_JABRA_EVENT, 0, (LPARAM)payload)) {
				delete payload;
			}
		}
	}

	// Si el puente termina de forma inesperada (proceso caido, .NET ausente...)
	// avisamos a la ventana principal para volver al soporte HID.
	if (s_running && mainDlg) {
		CString* payload = new CString(_T("{\"ev\":\"bridge_exit\"}"));
		if (!mainDlg->PostMessage(UM_JABRA_EVENT, 0, (LPARAM)payload)) {
			delete payload;
		}
	}

	return 0;
}

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------

bool Jabra::Start()
{
	if (s_running) {
		return true;
	}
	if (!accountSettings.jabraIntegration) {
		return false;
	}

	CString bridge = FindBridge();
	if (bridge.IsEmpty()) {
		LogInfo(_T("Jabra: JabraBridge.exe no encontrado"));
		return false;
	}

	if (!s_csInit) {
		InitializeCriticalSection(&s_cs);
		s_csInit = true;
	}

	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE hStdoutRead = NULL;
	HANDLE hStdoutWrite = NULL;
	HANDLE hStdinRead = NULL;
	HANDLE hStdinWrite = NULL;

	if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
		return false;
	}
	if (!SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
		CloseHandle(hStdoutRead);
		CloseHandle(hStdoutWrite);
		return false;
	}
	if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
		CloseHandle(hStdoutRead);
		CloseHandle(hStdoutWrite);
		return false;
	}
	if (!SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0)) {
		CloseHandle(hStdoutRead);
		CloseHandle(hStdoutWrite);
		CloseHandle(hStdinRead);
		CloseHandle(hStdinWrite);
		return false;
	}

	HANDLE hNul = CreateFile(_T("NUL"), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		&sa, OPEN_EXISTING, 0, NULL);

	STARTUPINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = hStdinRead;
	si.hStdOutput = hStdoutWrite;
	si.hStdError = (hNul != INVALID_HANDLE_VALUE) ? hNul : NULL;

	CString workDir = bridge;
	int pos = workDir.ReverseFind(_T('\\'));
	if (pos > 0) {
		workDir = workDir.Left(pos);
	}

	CString commandLine;
	commandLine.Format(_T("\"%s\""), (LPCTSTR)bridge);

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	BOOL created = CreateProcess(NULL, commandLine.GetBuffer(), NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, workDir, &si, &pi);
	commandLine.ReleaseBuffer();

	CloseHandle(hStdinRead);
	CloseHandle(hStdoutWrite);
	if (hNul != INVALID_HANDLE_VALUE) {
		CloseHandle(hNul);
	}

	if (!created) {
		LogInfo(_T("Jabra: no se pudo iniciar JabraBridge.exe"));
		CloseHandle(hStdoutRead);
		CloseHandle(hStdinWrite);
		return false;
	}

	CloseHandle(pi.hThread);

	s_hProcess = pi.hProcess;
	s_hStdout = hStdoutRead;
	s_hStdin = hStdinWrite;
	s_running = true;
	s_lastValid = false;
	s_lastBusylightValid = false;

	s_hThread = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);
	if (!s_hThread) {
		Stop();
		return false;
	}

	LogInfo(_T("Jabra: JabraBridge iniciado"));
	return true;
}

void Jabra::Stop()
{
	if (!s_hProcess && !s_hStdin && !s_hStdout) {
		s_running = false;
		return;
	}

	s_running = false;

	if (s_hStdin) {
		CStringA quit("{\"cmd\":\"quit\"}");
		WriteLine(quit);
	}

	// Cerrar stdin hace que el puente termine aunque no procese el comando.
	if (s_hStdin) {
		CloseHandle(s_hStdin);
		s_hStdin = NULL;
	}

	if (s_hProcess) {
		if (WaitForSingleObject(s_hProcess, 2000) == WAIT_TIMEOUT) {
			TerminateProcess(s_hProcess, 0);
		}
		CloseHandle(s_hProcess);
		s_hProcess = NULL;
	}

	// Cerrar el pipe de salida desbloquea el hilo lector.
	if (s_hStdout) {
		CloseHandle(s_hStdout);
		s_hStdout = NULL;
	}

	if (s_hThread) {
		WaitForSingleObject(s_hThread, 2000);
		CloseHandle(s_hThread);
		s_hThread = NULL;
	}

	s_enabled = false;
	s_devicePresent = false;
	s_busylightSupported = false;
	s_lastValid = false;
	s_lastBusylightValid = false;

	LogInfo(_T("Jabra: JabraBridge detenido"));
}

bool Jabra::IsRunning()
{
	return s_running && s_hProcess != NULL;
}

bool Jabra::IsEnabled()
{
	return s_running && s_enabled;
}

bool Jabra::IsDevicePresent()
{
	return s_running && s_devicePresent;
}

void Jabra::SetOffhookRing(bool offhook, bool ring)
{
	s_callActive = offhook;
	s_callingRing = ring;
	SendState(false);
	SetBusylight(offhook);
}

void Jabra::SetRing(bool ring)
{
	s_callingRing = ring;
	SendState(false);
}

void Jabra::SetMute(bool mute)
{
	s_mute = mute;
	SendState(false);
}

void Jabra::SetBusylight(bool on)
{
	if (s_lastBusylightValid && s_lastBusylight == on) {
		return;
	}
	s_lastBusylightValid = true;
	s_lastBusylight = on;

	if (!s_running || !s_busylightSupported) {
		return;
	}

	CStringA json;
	json.Format("{\"cmd\":\"busylight\",\"on\":%s}", on ? "true" : "false");
	WriteLine(json);
}

bool Jabra::GetOffhook()
{
	return s_offhook;
}

bool Jabra::GetRing()
{
	return s_callingRing;
}

void Jabra::HandleEvent(const CString& payload)
{
	CStringA utf8 = ToUtf8(payload);
	std::string text((LPCSTR)utf8, utf8.GetLength());

	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(text, root)) {
		return;
	}

	std::string event = root["ev"].asString();

	if (event == "scan") {
		int devices = root["devices"].asInt();
		if (devices == 0) {
			// No hay auriculares Jabra: se usa el soporte HID generico.
			s_devicePresent = false;
			s_enabled = false;
			if (accountSettings.headsetSupport && !Hid::IsOpen()) {
				Hid::OpenDevice();
			}
		}
	}
	else if (event == "device") {
		bool connected = root["connected"].asBool();
		bool callControl = root["callControl"].asBool();
		if (connected) {
			if (callControl) {
				SetDevicePresent(true);
			}
		}
		else {
			SetDevicePresent(false);
		}
	}
	else if (event == "busylight") {
		s_busylightSupported = root["supported"].asBool();
		s_lastBusylightValid = false;
		SetBusylight(s_callActive);
	}
	else if (event == "signal") {
		HandleSignal(root);
	}
	else if (event == "bridge_exit") {
		LogInfo(_T("Jabra: el puente termino inesperadamente"));
		s_running = false;
		s_enabled = false;
		s_devicePresent = false;
		s_busylightSupported = false;
		if (accountSettings.headsetSupport && !Hid::IsOpen()) {
			Hid::OpenDevice();
		}
	}
	else if (event == "error") {
		CString message;
		message.Format(_T("Jabra: error del puente: %hs"), root["message"].asString().c_str());
		LogInfo(message);
	}
}
