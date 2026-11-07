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

#pragma once

/*
 * Integracion con auriculares Jabra a traves del SDK oficial (Jabra .NET SDK 4.x).
 *
 * MicroSIP no enlaza el SDK directamente: lanza un proceso auxiliar
 * (JabraBridge.exe) que habla JSON por stdin/stdout. Asi el ejecutable
 * principal sigue siendo C++/MFC puro y sin dependencias de .NET.
 *
 * Funciones soportadas:
 *  - Boton de colgar/descolgar del auricular: descolgar, colgar, contestar
 *    y rechazar llamadas.
 *  - Boton de silencio del auricular y del boom: sincroniza el mute de MicroSIP.
 *  - LED de llamada en curso / timbre del auricular.
 *  - Busylight (luz de ocupado) encendida durante la llamada.
 *  - Boton de rellamada (redial).
 */

class Jabra
{
public:
	// Arranca el proceso puente. Devuelve false si JabraBridge.exe no esta
	// disponible o el proceso no se pudo crear.
	static bool Start();
	static void Stop();

	// El puente esta en ejecucion.
	static bool IsRunning();
	// Hay un auricular Jabra con soporte de call control conectado.
	static bool IsEnabled();
	static bool IsDevicePresent();

	// Estado de llamada que MicroSIP comunica al auricular (LEDs).
	static void SetOffhookRing(bool offhook, bool ring);
	static void SetRing(bool ring);
	static void SetMute(bool mute);
	static void SetBusylight(bool on);

	static bool GetOffhook();
	static bool GetRing();

	// Procesa una linea JSON recibida del puente.
	static void HandleEvent(const CString& payload);
};
