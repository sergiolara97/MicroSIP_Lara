#include "TrayIcon.h"

// GDI+ necesita IStream, que WIN32_LEAN_AND_MEAN excluye de windows.h; por eso
// objidl.h va antes de gdiplus.h. Se usa GDI+ (y no GDI) porque solo el primero
// respeta el canal alfa al componer sobre un icono de 32 bits.
#include <objidl.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace {

// Mismo callback que atiende el menu de bandeja en main.cpp (WM_APP + 3).
constexpr UINT kCallbackMessage = WM_APP + 3;
constexpr UINT kTrayId = 1;

// Estados del indicador.
constexpr int kStateIdle = 0;
constexpr int kStateLive = 1;
constexpr int kStateMuted = 2;

NOTIFYICONDATAW g_tray = {};
bool g_added = false;
HWND g_hwnd = nullptr;
HICON g_idle = nullptr;
HICON g_live = nullptr;
HICON g_muted = nullptr;
int g_state = -1;
std::wstring g_tipIdle;
std::wstring g_tipLive;
std::wstring g_tipMuted;
ITaskbarList3* g_taskbar = nullptr;
bool g_taskbarTried = false;
ULONG_PTR g_gdiplus = 0;

bool EnsureGdiplus()
{
	if (g_gdiplus != 0) {
		return true;
	}
	Gdiplus::GdiplusStartupInput input;
	return Gdiplus::GdiplusStartup(&g_gdiplus, &input, nullptr) == Gdiplus::Ok;
}

// Genera la variante del icono para el estado de la llamada. Se dibuja el icono
// entero (cuadrado redondeado + microfono) en vez de retocar el original: a
// 16 px el auricular no deja sitio para una insignia, y el microfono se lee
// mejor que el telefono para "estado del micro". El rojo se toma del propio
// icono base, y el verde sale de intercambiar sus canales R y G.
HICON MakeStateIcon(HICON base, int size, int state)
{
	if (size <= 0 || !EnsureGdiplus()) {
		return nullptr;
	}

	using namespace Gdiplus;

	// Color de fondo del icono original (esquina superior, fuera del dibujo).
	Color background(255, 217, 52, 80);
	if (base) {
		Bitmap probe(base);
		Color sample;
		int probeY = size * 12 / 100;
		if (probeY < 1) {
			probeY = 1;
		}
		if (probe.GetPixel(size / 2, probeY, &sample) == Ok && sample.GetA() > 0) {
			background = sample;
		}
	}
	const Color fill = state == kStateLive
		? Color(255, background.GetG(), background.GetR(), background.GetB())
		: background;

	const REAL s = (REAL)size;
	Bitmap canvas(size, size, PixelFormat32bppARGB);
	{
		Graphics g(&canvas);
		g.SetSmoothingMode(SmoothingModeAntiAlias);

		// Cuadrado redondeado que ocupa el icono entero (radio 14 %, como el
		// original).
		const REAL radius = s * 0.14f;
		const REAL d = radius * 2.0f;
		GraphicsPath path;
		path.AddArc(0.0f, 0.0f, d, d, 180.0f, 90.0f);
		path.AddArc(s - d, 0.0f, d, d, 270.0f, 90.0f);
		path.AddArc(s - d, s - d, d, d, 0.0f, 90.0f);
		path.AddArc(0.0f, s - d, d, d, 90.0f, 90.0f);
		path.CloseFigure();
		SolidBrush backgroundBrush(fill);
		g.FillPath(&backgroundBrush, &path);

		// Microfono: capsula, arco en U y pie. Escalado para que llene el icono.
		const REAL scale = 1.15f;
		const REAL cx = s * 0.5f;
		const REAL w = s * 0.22f * scale;
		const REAL h = s * 0.36f * scale;
		const REAL top = s * (0.5f - 0.32f * scale);
		const REAL x = cx - w * 0.5f;
		const REAL half = w * 0.5f;

		SolidBrush white(Color(255, 255, 255, 255));
		g.FillRectangle(&white, x, top + half, w, h - w);
		g.FillEllipse(&white, x, top, w, w);
		g.FillEllipse(&white, x, top + h - w, w, w);

		Pen pen(Color(255, 255, 255, 255), s * 0.075f * scale);
		const REAL arcRadius = s * 0.19f * scale;
		const REAL arcCenterY = s * (0.5f - 0.06f * scale);
		g.DrawArc(&pen, cx - arcRadius, arcCenterY - arcRadius,
			arcRadius * 2.0f, arcRadius * 2.0f, 0.0f, 180.0f);
		const REAL stemBottom = s * (0.5f + 0.30f * scale);
		g.DrawLine(&pen, cx, arcCenterY + arcRadius, cx, stemBottom);
		g.DrawLine(&pen, cx - s * 0.13f * scale, stemBottom,
			cx + s * 0.13f * scale, stemBottom);
	}

	if (state == kStateMuted) {
		Graphics g(&canvas);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		Pen slash(Color(255, 28, 31, 38), s * 0.12f);
		slash.SetStartCap(LineCapRound);
		slash.SetEndCap(LineCapRound);
		const REAL a = s * 0.16f;
		const REAL b = s * 0.84f;
		g.DrawLine(&slash, a, b, b, a);
	}

	HICON icon = nullptr;
	canvas.GetHICON(&icon);
	return icon;
}

ITaskbarList3* Taskbar()
{
	if (!g_taskbarTried) {
		g_taskbarTried = true;
		if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_taskbar))) && g_taskbar) {
			if (FAILED(g_taskbar->HrInit())) {
				g_taskbar->Release();
				g_taskbar = nullptr;
			}
		}
	}
	return g_taskbar;
}

HICON IconForState(int state)
{
	HICON icon = state == kStateMuted ? g_muted : (state == kStateLive ? g_live : g_idle);
	return icon ? icon : g_idle;
}

const std::wstring& TipForState(int state)
{
	if (state == kStateMuted) {
		return g_tipMuted;
	}
	return state == kStateLive ? g_tipLive : g_tipIdle;
}

} // namespace

void TrayIcon::Add(HWND hwnd, HINSTANCE instance, const wchar_t* title,
	const wchar_t* liveTip, const wchar_t* mutedTip)
{
	if (g_added) {
		return;
	}

	g_hwnd = hwnd;
	g_tipIdle = title ? title : L"";
	g_tipLive = liveTip ? liveTip : g_tipIdle;
	g_tipMuted = mutedTip ? mutedTip : g_tipIdle;

	const int size = GetSystemMetrics(SM_CXSMICON);
	g_idle = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
		size, size, 0);
	g_live = MakeStateIcon(g_idle, size, kStateLive);
	g_muted = MakeStateIcon(g_idle, size, kStateMuted);

	ZeroMemory(&g_tray, sizeof(g_tray));
	g_tray.cbSize = sizeof(g_tray);
	g_tray.hWnd = hwnd;
	g_tray.uID = kTrayId;
	g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	g_tray.uCallbackMessage = kCallbackMessage;
	g_tray.hIcon = g_idle;
	lstrcpynW(g_tray.szTip, g_tipIdle.c_str(), ARRAYSIZE(g_tray.szTip));
	if (Shell_NotifyIconW(NIM_ADD, &g_tray)) {
		g_added = true;
	}
	g_state = kStateIdle;
}

void TrayIcon::Remove()
{
	if (g_added) {
		Shell_NotifyIconW(NIM_DELETE, &g_tray);
		g_added = false;
	}
	if (g_live) {
		DestroyIcon(g_live);
		g_live = nullptr;
	}
	if (g_muted) {
		DestroyIcon(g_muted);
		g_muted = nullptr;
	}
	if (g_idle) {
		DestroyIcon(g_idle);
		g_idle = nullptr;
	}
	if (g_taskbar) {
		g_taskbar->Release();
		g_taskbar = nullptr;
	}
	if (g_gdiplus != 0) {
		Gdiplus::GdiplusShutdown(g_gdiplus);
		g_gdiplus = 0;
	}
	g_state = -1;
}

void TrayIcon::Update(bool inCall, bool muted)
{
	const int state = !inCall ? kStateIdle : (muted ? kStateMuted : kStateLive);
	if (state == g_state) {
		return;
	}
	g_state = state;

	HICON icon = IconForState(state);

	if (g_added) {
		g_tray.uFlags = NIF_ICON | NIF_TIP;
		g_tray.hIcon = icon;
		lstrcpynW(g_tray.szTip, TipForState(state).c_str(),
			ARRAYSIZE(g_tray.szTip));
		Shell_NotifyIconW(NIM_MODIFY, &g_tray);
	}

	// Boton de la barra de tareas: insignia encima del icono de la app, como
	// hacen Teams o Zoom. Sin llamada se quita.
	if (ITaskbarList3* taskbar = Taskbar()) {
		taskbar->SetOverlayIcon(g_hwnd,
			state == kStateIdle ? nullptr : icon,
			state == kStateIdle ? nullptr : TipForState(state).c_str());
	}
}
