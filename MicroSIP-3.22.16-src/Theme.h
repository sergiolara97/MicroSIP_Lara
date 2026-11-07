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

// Modo de color de la aplicacion (microsip.ini: colorMode).
enum ThemeColorMode
{
	THEME_MODE_SYSTEM = 0, // sigue el tema de Windows (por defecto)
	THEME_MODE_LIGHT = 1,
	THEME_MODE_DARK = 2
};

// Paleta y utilidades de tema. El acento de la aplicacion es el rojo.
class Theme
{
public:
	static int GetMode();
	static void SetMode(int mode);

	static bool IsSystemDark();
	static bool IsDark();

	// Paleta
	static COLORREF Accent();
	static COLORREF AccentText();
	static COLORREF WindowBg();
	static COLORREF PanelBg();
	static COLORREF Text();
	static COLORREF TextMuted();
	static COLORREF Link();
	static COLORREF TabBg();
	static COLORREF TabSelectedBg();

	// Brochas cacheadas (no liberar)
	static HBRUSH BrushWindow();
	static HBRUSH BrushPanel();

	static void ApplyTitleBar(HWND hwnd);
	// Notifica a todas las ventanas que el tema cambio.
	static void BroadcastChange();
};
