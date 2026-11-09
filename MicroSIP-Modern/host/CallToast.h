// Notificacion flotante de llamada.
//
// Aparece en la esquina inferior derecha cuando la aplicacion no esta en
// primer plano y hay una llamada entrante (o una llamada en curso): muestra
// quien llama y permite contestar o colgar sin traer la ventana al frente.
//
// Es una ventana propia (no un toast de WinRT) por dos motivos: los botones de
// un toast de una app sin empaquetar necesitan un servidor COM registrado en el
// sistema, y aqui no hay instalador; y asi el texto y los colores son los de la
// app.
#pragma once

#include <windows.h>

#include <string>

// Mensajes que la notificacion envia a la ventana principal.
constexpr UINT MSIP_TOAST_ANSWER = WM_APP + 8;   // wParam = id de llamada
constexpr UINT MSIP_TOAST_HANGUP = WM_APP + 9;   // wParam = id de llamada
constexpr UINT MSIP_TOAST_OPEN = WM_APP + 10;    // clic en el cuerpo

namespace CallToast {

struct Info {
	// Hay algo que mostrar; false oculta la notificacion.
	bool visible = false;
	// Timbre: muestra tambien el boton de contestar.
	bool ringing = false;
	int callId = -1;
	std::wstring name;          // contacto o numero
	std::wstring detail;        // numero y estado ("1234 - Llamada entrante")
	std::wstring answerLabel;   // Contestar
	std::wstring hangupLabel;   // Colgar
	COLORREF accent = RGB(198, 12, 48);
	// Esquina de la pantalla: 0 arriba-derecha, 1 arriba-izquierda,
	// 2 abajo-derecha, 3 abajo-izquierda.
	int corner = 0;
	// true: pantalla principal; false: pantalla donde esta MicroSIP.
	bool primaryScreen = true;
};

// Muestra, actualiza u oculta la notificacion.
void Sync(HWND owner, const Info& info);

// Destruye la ventana y libera las fuentes (al salir de la aplicacion).
void Close();

} // namespace CallToast
