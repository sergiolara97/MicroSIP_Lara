/*
 * MicroSIP Modern - enlace con JabraBridge.exe (SDK oficial de Jabra).
 */

#pragma once

#include <windows.h>
#include <string>

class JabraLink
{
public:
	static JabraLink& Instance();

	bool Start(const std::wstring& appDir, HWND hwnd);
	void Stop();
	bool IsRunning() const { return running_; }

	// Estado que se envia al auricular (LEDs / busylight).
	void SendState(bool offhook, bool ring, bool mute, bool confirmed);

	// Procesa una linea JSON recibida del puente (se llama en el hilo UI).
	void HandleEvent(const std::string& json);

private:
	JabraLink() = default;
	bool WriteLine(const std::string& line);
	static DWORD WINAPI ThreadProc(LPVOID param);

	HWND hwnd_ = nullptr;
	HANDLE process_ = nullptr;
	HANDLE stdin_ = nullptr;
	HANDLE stdout_ = nullptr;
	HANDLE thread_ = nullptr;
	volatile bool running_ = false;
	CRITICAL_SECTION cs_{};
	bool csInit_ = false;
	bool busylightSupported_ = false;
	bool lastValid_ = false;
	bool lastOffhook_ = false;
	bool lastRing_ = false;
	bool lastMute_ = false;
	// Estado fisico del gancho segun el SDK (no el que se le manda).
	bool hookOff_ = false;
	// Momento en que la llamada paso a establecida: el auricular manda un
	// PhoneMute al abrirse el audio y no es un gesto del usuario.
	bool lastConfirmed_ = false;
	DWORD confirmedTick_ = 0;
	// El proximo estado que se mande al puente trae un mute originado por el
	// propio dispositivo (boom o boton del auricular): asi el puente no se lo
	// devuelve y no se genera eco.
	bool muteFromDevice_ = false;
	// Momento en que empezo a timbrar la llamada entrante actual.
	DWORD ringSince_ = 0;
};
