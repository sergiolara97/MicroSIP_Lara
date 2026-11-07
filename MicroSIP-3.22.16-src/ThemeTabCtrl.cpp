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
#include "ThemeTabCtrl.h"
#include "Theme.h"

IMPLEMENT_DYNAMIC(CThemeTabCtrl, CTabCtrl)

BEGIN_MESSAGE_MAP(CThemeTabCtrl, CTabCtrl)
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

void CThemeTabCtrl::PreSubclassWindow()
{
	CTabCtrl::PreSubclassWindow();
	ModifyStyle(0, TCS_OWNERDRAWFIXED);
}

BOOL CThemeTabCtrl::OnEraseBkgnd(CDC* pDC)
{
	if (Theme::IsDark()) {
		CRect rect;
		GetClientRect(&rect);
		pDC->FillSolidRect(rect, Theme::TabBg());
		return TRUE;
	}
	return CTabCtrl::OnEraseBkgnd(pDC);
}

void CThemeTabCtrl::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	if (lpDIS->itemID == (UINT)-1) {
		return;
	}

	CDC* pDC = CDC::FromHandle(lpDIS->hDC);
	if (!pDC) {
		return;
	}

	CRect rcItem(lpDIS->rcItem);
	bool selected = ((int)lpDIS->itemID == GetCurSel());

	pDC->FillSolidRect(rcItem, selected ? Theme::TabSelectedBg() : Theme::TabBg());

	TCHAR text[256] = { 0 };
	TCITEM item;
	item.mask = TCIF_TEXT;
	item.pszText = text;
	item.cchTextMax = _countof(text);
	if (GetItem(lpDIS->itemID, &item)) {
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(Theme::Text());
		CRect rcText(rcItem);
		pDC->DrawText(text, rcText, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	}

	// Acento rojo en la pestana activa.
	if (selected) {
		CRect rcAccent(rcItem);
		rcAccent.top = rcAccent.bottom - 2;
		pDC->FillSolidRect(rcAccent, Theme::Accent());
	}
}
