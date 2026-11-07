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
#include "StatusBar.h"   
#include "global.h"
#include "Theme.h"

 // StatusBar   
IMPLEMENT_DYNAMIC(StatusBar, CStatusBar)
StatusBar::StatusBar()
{
	CStatusBar::CStatusBar();
}

StatusBar::~StatusBar()
{
}

BEGIN_MESSAGE_MAP(StatusBar, CStatusBar)
	//{{AFX_MSG_MAP(StatusBar)   
	ON_MESSAGE(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_PAINT()
	//}}AFX_MSG_MAP   
END_MESSAGE_MAP()

LRESULT StatusBar::OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam)
{
	if (IsWindowVisible())
	{
		CFrameWnd* pParent = (CFrameWnd*)GetParent();
		if (pParent)
			OnUpdateCmdUI(pParent, (BOOL)wParam);
	}
	return 0L;
}

void StatusBar::OnLButtonUp(UINT nFlags, CPoint point)
{
}

void StatusBar::OnMouseMove(UINT nFlags, CPoint point)
{
}

void StatusBar::OnPaint()
{
	if (!Theme::IsDark()) {
		CStatusBar::OnPaint();
		return;
	}

	CPaintDC dc(this);
	CRect client;
	GetClientRect(&client);
	dc.FillSolidRect(client, Theme::WindowBg());
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(Theme::Text());

	int count = (int)::SendMessage(m_hWnd, SB_GETPARTS, 0, 0);
	for (int i = 0; i < count; i++) {
		CRect part;
		if (!::SendMessage(m_hWnd, SB_GETRECT, i, (LPARAM)&part)) {
			continue;
		}
		TCHAR text[512];
		text[0] = 0;
		::SendMessage(m_hWnd, SB_GETTEXT, i, (LPARAM)text);
		part.DeflateRect(4, 0);
		dc.DrawText(text, part, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
	}
}
