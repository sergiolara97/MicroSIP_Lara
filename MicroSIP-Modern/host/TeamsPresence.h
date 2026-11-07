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
#pragma once

namespace TeamsPresence {

// Hay un cliente de Teams en ejecucion (no se lanza si esta cerrado).
bool Running();

// Pone el estado en Ocupado / Disponible. Silencioso si Teams no esta abierto.
void SetBusy();
void SetAvailable();

// Aplica el estado de una llamada: al entrar guarda el estado que habia y pone
// Ocupado; al salir lo restaura (o --reset-presence si era automatico).
void Apply(bool busy);

// Restaura el estado guardado por Apply(true).
void Restore();

} // namespace TeamsPresence
