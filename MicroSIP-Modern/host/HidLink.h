/*
 * MicroSIP Modern - botones de auriculares USB genericos (HID consumo 0xCD).
 *
 * Los EarPods USB-C de Apple (VID 05AC) no exponen la pagina de telefonia
 * (0x0B) que usa MicroSIP clasico: el boton central es Play/Pause de la
 * pagina de consumo (0x0C, usage 0xCD). Este enlace abre esa interfaz HID,
 * lee los reportes y avisa al hilo UI con MSIP_HID_BUTTON_MSG.
 *
 * La lectura no es exclusiva: Windows sigue entregando la tecla al resto
 * de aplicaciones (musica incluida).
 */

#pragma once

#include <windows.h>
#include <string>

// Aviso al hilo UI: wParam 1 = pulsado, 0 = soltado.
constexpr UINT MSIP_HID_BUTTON_MSG = WM_APP + 6;

class HidLink
{
public:
	static HidLink& Instance();

	// filter: lista "VID:PID" separada por ';' (vacia = cualquier auricular
	// que exponga el boton 0xCD en la pagina de consumo).
	void Start(HWND hwnd, const std::wstring& filter);
	void Stop();
	bool Running() const { return running_; }

private:
	HidLink() = default;
	static DWORD WINAPI ThreadProc(LPVOID param);
	bool OpenDevice();
	bool Attach(const std::wstring& path);
	void CloseDevice();
	void ReadLoop();
	void HandleReport(const unsigned char* data, unsigned long length);
	bool MatchesFilter(unsigned vid, unsigned pid) const;

	HWND hwnd_ = nullptr;
	HANDLE thread_ = nullptr;
	HANDLE stopEvent_ = nullptr;
	HANDLE device_ = INVALID_HANDLE_VALUE;
	HANDLE readEvent_ = nullptr;
	void* preparsed_ = nullptr;
	unsigned short reportLength_ = 0;
	bool lastDown_ = false;
	std::wstring filter_;
	volatile bool running_ = false;
};
