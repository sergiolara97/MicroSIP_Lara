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
#include "ThemeDialog.h"
#include "Theme.h"
#include "microsip.h"

#pragma comment(lib, "dwmapi.lib") 

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

CThemeDialog::CThemeDialog(UINT nIDTemplate, CWnd* pParent /*=NULL*/)
    : CDialog(nIDTemplate, pParent)
{
}

BEGIN_MESSAGE_MAP(CThemeDialog, CDialog)
    //{{AFX_MSG_MAP(CThemeDialog)
    ON_WM_CTLCOLOR()
    ON_WM_SETTINGCHANGE()
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

BOOL CThemeDialog::OnInitDialog()
{
    CDialog::OnInitDialog();

    lastDark = -1;

    ApplyTheme();

    return TRUE;  // return TRUE  unless you set the focus to a control
}

HBRUSH CThemeDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (m_bAllowDark && lastDark == 1) {
        switch (nCtlColor)
        {
        case CTLCOLOR_EDIT:
            SetTextColor(pDC->m_hDC, Theme::Text());
            SetBkColor(pDC->m_hDC, Theme::PanelBg());
            return Theme::BrushPanel();
        case CTLCOLOR_LISTBOX:
            SetTextColor(pDC->m_hDC, Theme::Text());
            SetBkColor(pDC->m_hDC, Theme::PanelBg());
            return Theme::BrushPanel();
        case CTLCOLOR_STATIC:
            wchar_t cls[64];
            GetClassName(pWnd->m_hWnd, cls, 64);
            if (wcscmp(cls, L"SysLink") == 0) {
                SetTextColor(pDC->m_hDC, Theme::Link());
            }
            else {
                SetTextColor(pDC->m_hDC, Theme::Text());
            }
            SetBkColor(pDC->m_hDC, Theme::WindowBg());
            pDC->SetBkMode(TRANSPARENT);
            return Theme::BrushWindow();
        case CTLCOLOR_BTN:
            SetBkColor(pDC->m_hDC, Theme::WindowBg());
            pDC->SetBkMode(TRANSPARENT);
            return Theme::BrushWindow();
        case CTLCOLOR_DLG:
            return Theme::BrushWindow();
        }
    }
    HBRUSH br = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
    return br;
}

void CThemeDialog::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
    if (lpszSection &&
        wcscmp(lpszSection, L"ImmersiveColorSet") == 0) {
        ApplyTheme();
    }
    CDialog::OnSettingChange(uFlags, lpszSection);
}

void CThemeDialog::ApplyTheme()
{
    BOOL currentDark = Theme::IsDark() ? TRUE : FALSE;
    if (lastDark == -1 || currentDark != lastDark) {
        lastDark = currentDark;
        Theme::ApplyTitleBar(m_hWnd);

        EnumChildWindows(GetSafeHwnd(), [](HWND hWnd, LPARAM lParam) {
            bool allowDark = lParam & (1 << 1);
            bool dark = lParam & 1;
            wchar_t cls[32]{};
            GetClassNameW(hWnd, cls, _countof(cls));
            if (wcscmp(cls, L"Button") == 0) {
                LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
                if (((style & BS_TYPEMASK) == BS_PUSHBUTTON
                    || (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON
                    || (allowDark && (style & BS_TYPEMASK) == BS_OWNERDRAW)
                    ) && (!(style & BS_ICON) || allowDark)
                    ) {
                    SetWindowTheme(hWnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                }
                if (allowDark) {
                    if ((style & BS_TYPEMASK) == BS_AUTOCHECKBOX || (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON) {
                        if (dark) {
                            SetWindowTheme(hWnd, L"wstr", L"wstr");
                        }
                        else {
                            SetWindowTheme(hWnd, L"Explorer", nullptr);
                        }
                    }
                }
            }
            if (allowDark) {
                if (wcscmp(cls, L"ComboBox") == 0) {
                    SetWindowTheme(hWnd, dark ? L"DarkMode_CFD" : L"CFD", nullptr);
                }
                else if (wcscmp(cls, L"Edit") == 0 || wcscmp(cls, L"Static") == 0 || wcscmp(cls, L"ListBox") == 0) {
                    SetWindowTheme(hWnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                }
                else if (wcscmp(cls, L"SysTabControl32") == 0) {
                    SetWindowTheme(hWnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                }
                else if ( wcscmp(cls, L"SysLink") == 0 ) {
                    LITEM item = { 0 };
                    item.iLink = 0;
                    item.mask = LIF_ITEMINDEX | LIF_STATE;
                    item.state = LIS_DEFAULTCOLORS;
                    item.stateMask = LIS_DEFAULTCOLORS;
                    ::SendMessage(hWnd, LM_SETITEM, 0, (LPARAM)&item);
                }
            }
            return TRUE;
            }, m_bAllowDark << 1 | currentDark);
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }
}
