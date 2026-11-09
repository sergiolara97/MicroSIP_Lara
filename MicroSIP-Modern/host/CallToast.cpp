#include "CallToast.h"

#include <pj/log.h>

#include <vector>

namespace {

const wchar_t kClassName[] = L"MicroSIPCallToast";

// Colores de la tarjeta (tema oscuro, como la interfaz).
const COLORREF kBackground = RGB(30, 33, 41);
const COLORREF kBorder = RGB(64, 70, 84);
const COLORREF kText = RGB(238, 241, 246);
const COLORREF kDim = RGB(154, 163, 181);
const COLORREF kAnswer = RGB(31, 157, 85);
const COLORREF kHangup = RGB(198, 12, 48);

HWND g_hwnd = nullptr;
HWND g_owner = nullptr;
CallToast::Info g_info;
UINT g_dpi = 96;
int g_hover = -1;   // 0 contestar, 1 colgar, 2 cuerpo

// Medidas base a 96 ppp.
constexpr int kBaseWidth = 360;
constexpr int kBaseHeight = 112;
constexpr int kBaseRadius = 14;
constexpr int kBaseMargin = 16;
constexpr int kBaseAvatar = 44;
constexpr int kBaseButtonHeight = 30;

int Scale(int value)
{
	return MulDiv(value, (int)g_dpi, 96);
}

COLORREF Shade(COLORREF color, int percent)
{
	int r = GetRValue(color);
	int g = GetGValue(color);
	int b = GetBValue(color);
	auto adjust = [percent](int value) {
		int result = percent >= 0
			? value + (255 - value) * percent / 100
			: value + value * percent / 100;
		return result < 0 ? 0 : (result > 255 ? 255 : result);
	};
	return RGB(adjust(r), adjust(g), adjust(b));
}

struct Layout {
	int width = 0;
	int height = 0;
	int radius = 0;
	RECT avatar = {};
	RECT title = {};
	RECT detail = {};
	RECT answer = {};
	RECT hangup = {};
};

Layout MakeLayout(bool ringing)
{
	Layout l;
	l.width = Scale(kBaseWidth);
	l.height = Scale(kBaseHeight);
	l.radius = Scale(kBaseRadius);

	int margin = Scale(kBaseMargin);
	int avatar = Scale(kBaseAvatar);
	l.avatar = { margin, Scale(16), margin + avatar, Scale(16) + avatar };

	int textX = margin + avatar + Scale(14);
	l.title = { textX, Scale(13), l.width - margin, Scale(42) };
	l.detail = { textX, Scale(42), l.width - margin, Scale(63) };

	int buttonY = Scale(70);
	int buttonHeight = Scale(kBaseButtonHeight);
	l.answer = { Scale(136), buttonY, Scale(244), buttonY + buttonHeight };
	l.hangup = { Scale(252), buttonY, l.width - Scale(12), buttonY + buttonHeight };

	if (!ringing) {
		l.answer = {};
	}
	return l;
}

// DPI del monitor donde esta la ventana principal (per-monitor v2).
UINT DpiForWindow(HWND hwnd)
{
	using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
	static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
		GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	if (fn) {
		UINT dpi = fn(hwnd);
		if (dpi != 0) {
			return dpi;
		}
	}
	return 96;
}

// DPI efectivo del monitor indicado (GetDpiForMonitor vive en shcore.dll).
UINT DpiForMonitor(HMONITOR monitor)
{
	using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
	static GetDpiForMonitorFn fn = reinterpret_cast<GetDpiForMonitorFn>(
		GetProcAddress(LoadLibraryW(L"shcore.dll"), "GetDpiForMonitor"));
	if (fn) {
		UINT dpiX = 0;
		UINT dpiY = 0;
		if (SUCCEEDED(fn(monitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX != 0) {
			return dpiX;
		}
	}
	return 0;
}

// Monitor donde se coloca el aviso: la pantalla principal o la de MicroSIP.
HMONITOR TargetMonitor(bool primaryScreen)
{
	POINT origin = { 0, 0 };
	HWND reference = g_owner ? g_owner : g_hwnd;
	return primaryScreen
		? MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY)
		: MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST);
}

HFONT MakeFont(int pixels, int weight)
{
	LOGFONTW logFont = {};
	logFont.lfHeight = -Scale(pixels);
	logFont.lfWeight = weight;
	logFont.lfQuality = CLEARTYPE_QUALITY;
	lstrcpynW(logFont.lfFaceName, L"Segoe UI", LF_FACESIZE);
	return CreateFontIndirectW(&logFont);
}

struct Fonts {
	HFONT title = nullptr;
	HFONT detail = nullptr;
	HFONT avatar = nullptr;
	HFONT button = nullptr;
	UINT dpi = 0;
};

Fonts g_fonts;

void ReleaseFonts()
{
	DeleteObject(g_fonts.title);
	DeleteObject(g_fonts.detail);
	DeleteObject(g_fonts.avatar);
	DeleteObject(g_fonts.button);
	g_fonts = Fonts();
}

const Fonts& CurrentFonts()
{
	if (g_fonts.dpi != g_dpi) {
		ReleaseFonts();
		g_fonts.title = MakeFont(19, FW_SEMIBOLD);
		g_fonts.detail = MakeFont(15, FW_NORMAL);
		g_fonts.avatar = MakeFont(17, FW_SEMIBOLD);
		g_fonts.button = MakeFont(15, FW_SEMIBOLD);
		g_fonts.dpi = g_dpi;
	}
	return g_fonts;
}

// Iniciales del nombre: "Ana Perez" -> "AP".
std::wstring Initials(const std::wstring& name)
{
	std::wstring result;
	bool newWord = true;
	for (wchar_t c : name) {
		if (c == L' ' || c == L'.' || c == L'-' || c == L'_') {
			newWord = true;
			continue;
		}
		if (newWord) {
			wchar_t upper = c;
			if (upper >= L'a' && upper <= L'z') {
				upper = (wchar_t)(upper - L'a' + L'A');
			}
			result.push_back(upper);
			newWord = false;
			if (result.size() == 2) {
				break;
			}
		}
	}
	if (result.empty()) {
		result = L"?";
	}
	return result;
}

void PaintButton(HDC dc, const RECT& rect, const std::wstring& label,
	COLORREF fill, bool hover)
{
	HBRUSH brush = CreateSolidBrush(hover ? Shade(fill, -16) : fill);
	HGDIOBJ oldBrush = SelectObject(dc, brush);
	HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
	int round = Scale(8);
	RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, round, round);
	SelectObject(dc, oldPen);
	SelectObject(dc, oldBrush);
	DeleteObject(brush);

	SetTextColor(dc, RGB(255, 255, 255));
	HGDIOBJ oldFont = SelectObject(dc, CurrentFonts().button);
	RECT text = rect;
	DrawTextW(dc, label.c_str(), -1, &text,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SelectObject(dc, oldFont);
}

void Paint(HWND hwnd, HDC dc)
{
	const Fonts& fonts = CurrentFonts();
	Layout layout = MakeLayout(g_info.ringing);
	RECT client = { 0, 0, layout.width, layout.height };

	HBRUSH background = CreateSolidBrush(kBackground);
	FillRect(dc, &client, background);
	DeleteObject(background);

	HPEN border = CreatePen(PS_SOLID, 1, kBorder);
	HGDIOBJ oldPen = SelectObject(dc, border);
	HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	RoundRect(dc, 0, 0, layout.width, layout.height, layout.radius, layout.radius);
	SelectObject(dc, oldBrush);
	SelectObject(dc, oldPen);
	DeleteObject(border);

	SetBkMode(dc, TRANSPARENT);

	// Avatar con las iniciales.
	HBRUSH avatar = CreateSolidBrush(g_info.accent);
	oldBrush = SelectObject(dc, avatar);
	oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
	Ellipse(dc, layout.avatar.left, layout.avatar.top,
		layout.avatar.right, layout.avatar.bottom);
	SelectObject(dc, oldPen);
	SelectObject(dc, oldBrush);
	DeleteObject(avatar);

	std::wstring initials = Initials(g_info.name);
	SetTextColor(dc, RGB(255, 255, 255));
	HGDIOBJ oldFont = SelectObject(dc, fonts.avatar);
	RECT avatarText = layout.avatar;
	DrawTextW(dc, initials.c_str(), -1, &avatarText,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SelectObject(dc, oldFont);

	// Nombre y detalle.
	SetTextColor(dc, kText);
	oldFont = SelectObject(dc, fonts.title);
	RECT title = layout.title;
	DrawTextW(dc, g_info.name.c_str(), -1, &title,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SelectObject(dc, oldFont);

	SetTextColor(dc, kDim);
	oldFont = SelectObject(dc, fonts.detail);
	RECT detail = layout.detail;
	DrawTextW(dc, g_info.detail.c_str(), -1, &detail,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SelectObject(dc, oldFont);

	// Botones.
	if (g_info.ringing) {
		PaintButton(dc, layout.answer, g_info.answerLabel, kAnswer, g_hover == 0);
	}
	PaintButton(dc, layout.hangup, g_info.hangupLabel, kHangup, g_hover == 1);

	// Una linea de acento bajo el texto para despegar los botones.
	RECT underline = { Scale(74), Scale(66), layout.width - Scale(16), Scale(67) };
	HBRUSH line = CreateSolidBrush(Shade(g_info.accent, 40));
	FillRect(dc, &underline, line);
	DeleteObject(line);

	(void)hwnd;
}

int HitTest(int x, int y)
{
	Layout layout = MakeLayout(g_info.ringing);
	POINT point = { x, y };
	if (g_info.ringing && PtInRect(&layout.answer, point)) {
		return 0;
	}
	if (PtInRect(&layout.hangup, point)) {
		return 1;
	}
	return 2;
}

void UpdateRegion()
{
	if (!g_hwnd) {
		return;
	}
	Layout layout = MakeLayout(g_info.ringing);
	HRGN region = CreateRoundRectRgn(0, 0, layout.width + 1, layout.height + 1,
		layout.radius, layout.radius);
	SetWindowRgn(g_hwnd, region, TRUE);
}

void PositionWindow()
{
	if (!g_hwnd) {
		return;
	}
	// Por defecto la pantalla principal; opcionalmente la de MicroSIP.
	HMONITOR monitor = TargetMonitor(g_info.primaryScreen);
	MONITORINFO info = { sizeof(info) };
	RECT work = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	if (GetMonitorInfoW(monitor, &info)) {
		work = info.rcWork;
	}

	Layout layout = MakeLayout(g_info.ringing);
	int margin = Scale(kBaseMargin);
	bool right = (g_info.corner == 0 || g_info.corner == 2);
	bool top = (g_info.corner == 0 || g_info.corner == 1);
	int x = right ? work.right - layout.width - margin : work.left + margin;
	int y = top ? work.top + margin : work.bottom - layout.height - margin;
	SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, layout.width, layout.height,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
	UpdateRegion();
}

LRESULT CALLBACK ToastProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(hwnd, &paint);
		Paint(hwnd, dc);
		EndPaint(hwnd, &paint);
		return 0;
	}

	case WM_MOUSEMOVE: {
		int hover = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
		if (hover != g_hover) {
			g_hover = hover;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&track);
		return 0;
	}

	case WM_MOUSELEAVE:
		if (g_hover != -1) {
			g_hover = -1;
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;

	case WM_SETCURSOR: {
		POINT point;
		GetCursorPos(&point);
		ScreenToClient(hwnd, &point);
		int hover = HitTest(point.x, point.y);
		SetCursor(LoadCursorW(nullptr, hover >= 0 ? IDC_HAND : IDC_ARROW));
		return TRUE;
	}

	case WM_LBUTTONUP: {
		int hover = HitTest((short)LOWORD(lParam), (short)HIWORD(lParam));
		if (hover == 0 && g_owner) {
			PostMessageW(g_owner, MSIP_TOAST_ANSWER, (WPARAM)g_info.callId, 0);
		}
		else if (hover == 1 && g_owner) {
			PostMessageW(g_owner, MSIP_TOAST_HANGUP, (WPARAM)g_info.callId, 0);
		}
		else if (g_owner) {
			PostMessageW(g_owner, MSIP_TOAST_OPEN, 0, 0);
		}
		return 0;
	}

	case WM_DESTROY:
		g_hwnd = nullptr;
		return 0;
	}
	return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsureWindow()
{
	if (g_hwnd) {
		return true;
	}

	static bool registered = false;
	if (!registered) {
		WNDCLASSEXW windowClass = { sizeof(windowClass) };
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = ToastProc;
		windowClass.hInstance = GetModuleHandleW(nullptr);
		windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClass.lpszClassName = kClassName;
		registered = RegisterClassExW(&windowClass) != 0;
		if (!registered) {
			PJ_LOG(2, ("CallToast", "RegisterClass fallo err=%lu", GetLastError()));
			return false;
		}
	}

	// Sin ventana propietaria a proposito: una ventana hija de la principal se
	// ocultaria con ella cuando la app esta en la bandeja.
	g_hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
		kClassName, L"MicroSIP", WS_POPUP,
		0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!g_hwnd) {
		PJ_LOG(2, ("CallToast", "CreateWindow fallo err=%lu", GetLastError()));
		return false;
	}
	SetLayeredWindowAttributes(g_hwnd, 0, 250, LWA_ALPHA);
	return true;
}

} // namespace

void CallToast::Sync(HWND owner, const Info& info)
{
	g_owner = owner;

	if (!info.visible) {
		if (g_hwnd) {
			DestroyWindow(g_hwnd);
			g_hwnd = nullptr;
		}
		g_hover = -1;
		g_info = Info();
		return;
	}

	UINT dpi = DpiForMonitor(TargetMonitor(info.primaryScreen));
	if (dpi == 0) {
		dpi = info.primaryScreen
			? DpiForWindow(GetDesktopWindow())
			: DpiForWindow(owner ? owner : g_hwnd);
	}
	if (dpi != 0 && dpi != g_dpi) {
		g_dpi = dpi;
	}

	bool created = false;
	if (!g_hwnd) {
		if (!EnsureWindow()) {
			return;
		}
		created = true;
		PJ_LOG(3, ("CallToast", "mostrando notificacion de llamada"));
	}

	bool changed = created
		|| g_info.name != info.name
		|| g_info.detail != info.detail
		|| g_info.ringing != info.ringing
		|| g_info.answerLabel != info.answerLabel
		|| g_info.hangupLabel != info.hangupLabel
		|| g_info.accent != info.accent
		|| g_info.primaryScreen != info.primaryScreen;

	g_info = info;
	PositionWindow();
	if (changed) {
		g_hover = -1;
		InvalidateRect(g_hwnd, nullptr, TRUE);
	}
}

void CallToast::Close()
{
	if (g_hwnd) {
		DestroyWindow(g_hwnd);
		g_hwnd = nullptr;
	}
	ReleaseFonts();
	g_info = Info();
	g_hover = -1;
}
