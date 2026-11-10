// Estado de Microsoft Teams sincronizado con la llamada en curso.
//
// No hay API local de presencia: el estado de Teams vive en Microsoft 365 y la
// via oficial es Microsoft Graph (setPresence), que exige consentimiento de
// administrador. El cliente nuevo de Teams (MSIX) si trae argumentos de linea de
// comandos que aplican el estado con la sesion ya iniciada del usuario:
//
//   ms-teams.exe --set-presence-to-busy
//   ms-teams.exe --set-presence-to-available
//
// Es lo que usa su propio menu contextual / jump list, asi que no necesita
// Graph, ni registro de app, ni permisos de administrador.
//
// Leer el estado previo con UI Automation tarda 1-2 s; hacerlo en el hilo de la
// ventana retrasaba el comando "Ocupado" hasta despues de colgar y Teams se
// quedaba en Ocupado. Por eso todo el trabajo se hace en un hilo propio.
#pragma once

namespace TeamsPresence {

// Hay un cliente de Teams en ejecucion (no se lanza si esta cerrado).
bool Running();

// Pide el estado de la llamada en curso: Ocupado al entrar y el estado que
// hubiera antes al salir. Coalesce las peticiones: si la llamada termina
// mientras se aplica Ocupado, no se llega a tocar Teams.
void Apply(bool busy);

// Restaura el estado si quedo Ocupado y termina el hilo de trabajo.
void Stop();

} // namespace TeamsPresence
