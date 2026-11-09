#pragma once

#include <windows.h>

/*
 * Icono de la bandeja y overlay del boton de la barra de tareas.
 *
 * Ademas del icono normal, mantiene dos variantes que se dibujan enteras (no se
 * retoca el original) porque a 16 px una insignia en la esquina no se lee:
 *   - en llamada, micro abierto -> cuadrado verde con un microfono
 *   - en llamada, micro silenciado -> igual en rojo, cruzado por una barra
 * Fuera de llamada se usa el icono de la aplicacion. El rojo se muestrea del
 * icono base y el verde sale de intercambiar sus canales R y G.
 */
namespace TrayIcon {

// Crea el icono de bandeja. Los textos son el tooltip en cada estado.
void Add(HWND hwnd, HINSTANCE instance, const wchar_t* title,
	const wchar_t* liveTip, const wchar_t* mutedTip);

void Remove();

// Cambia icono, tooltip y overlay solo cuando cambia el estado.
void Update(bool inCall, bool muted);

} // namespace TrayIcon
