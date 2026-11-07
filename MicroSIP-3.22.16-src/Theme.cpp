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
#include "Theme.h"
#include "settings.h"

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Acento de la aplicacion (rojo).
#define THEME_ACCENT RGB(198, 12, 48)
#define THEME_ACCENT_DARK RGB(255, 96, 96)

int Theme::GetMode()
{
	return accountSettings.colorMode;
}

void Theme::SetMode(int mode)
{
	if (mode < THEME_MODE_SYSTEM || mode > THEME_MODE_DARK) {
		mode = THEME_MODE_SYSTEM;
	}
	accountSettings.colorMode = mode;
}

bool Theme::IsSystemDark()
{
	DWORD value = 1;
	DWORD size = sizeof(value);
	RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_REG_DWORD,
		nullptr,
		&value,
		&size
	);
	return value == 0;
}

bool Theme::IsDark()
{
	int mode = GetMode();
	if (mode == THEME_MODE_DARK) {
		return true;
	}
	if (mode == THEME_MODE_LIGHT) {
		return false;
	}
	return IsSystemDark();
}

COLORREF Theme::Accent()
{
	return THEME_ACCENT;
}

COLORREF Theme::AccentText()
{
	return IsDark() ? THEME_ACCENT_DARK : THEME_ACCENT;
}

COLORREF Theme::WindowBg()
{
	return IsDark() ? RGB(32, 32, 32) : GetSysColor(COLOR_3DFACE);
}

COLORREF Theme::PanelBg()
{
	return IsDark() ? RGB(45, 45, 45) : GetSysColor(COLOR_WINDOW);
}

COLORREF Theme::Text()
{
	return IsDark() ? RGB(240, 240, 240) : GetSysColor(COLOR_BTNTEXT);
}

COLORREF Theme::TextMuted()
{
	return IsDark() ? RGB(170, 170, 170) : GetSysColor(COLOR_GRAYTEXT);
}

COLORREF Theme::Link()
{
	return IsDark() ? RGB(255, 120, 120) : THEME_ACCENT;
}

COLORREF Theme::TabBg()
{
	return IsDark() ? RGB(40, 40, 40) : GetSysColor(COLOR_BTNFACE);
}

COLORREF Theme::TabSelectedBg()
{
	return IsDark() ? RGB(56, 56, 56) : RGB(255, 255, 255);
}

HBRUSH Theme::BrushWindow()
{
	static HBRUSH brush = NULL;
	static COLORREF color = 0xFFFFFFFF;
	COLORREF current = WindowBg();
	if (!brush || color != current) {
		if (brush) {
			DeleteObject(brush);
		}
		brush = CreateSolidBrush(current);
		color = current;
	}
	return brush;
}

HBRUSH Theme::BrushPanel()
{
	static HBRUSH brush = NULL;
	static COLORREF color = 0xFFFFFFFF;
	COLORREF current = PanelBg();
	if (!brush || color != current) {
		if (brush) {
			DeleteObject(brush);
		}
		brush = CreateSolidBrush(current);
		color = current;
	}
	return brush;
}

void Theme::ApplyTitleBar(HWND hwnd)
{
	if (!hwnd) {
		return;
	}
	BOOL dark = IsDark() ? TRUE : FALSE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void Theme::BroadcastChange()
{
	SendMessageTimeout(
		HWND_BROADCAST,
		WM_SETTINGCHANGE,
		0,
		(LPARAM)L"ImmersiveColorSet",
		SMTO_ABORTIFHUNG,
		500,
		NULL);
}
