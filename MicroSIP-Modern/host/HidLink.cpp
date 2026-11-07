/*
 * MicroSIP Modern - botones de auriculares USB genericos (HID consumo 0xCD).
 */

#include "HidLink.h"
#include "SipCore.h"

#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <cstring>
#include <cwctype>
#include <vector>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace {

constexpr USHORT kConsumerPage = 0x0C;
constexpr USHORT kConsumerControl = 0x01;
constexpr USHORT kUsagePlayPause = 0xCD;
constexpr DWORD kRescanMs = 2000;
constexpr DWORD kRetryMs = 500;

// Identificador hex de 4 digitos tras "vid_"/"pid_" en la ruta de la interfaz.
unsigned ParseId(const std::wstring& path, const wchar_t* tag)
{
	size_t pos = path.find(tag);
	if (pos == std::wstring::npos || pos + 8 > path.size()) {
		return 0;
	}
	return (unsigned)wcstoul(path.substr(pos + 4, 4).c_str(), nullptr, 16);
}

std::wstring Lower(const std::wstring& text)
{
	std::wstring result = text;
	for (wchar_t& ch : result) {
		ch = (wchar_t)towlower(ch);
	}
	return result;
}

std::wstring Trim(const std::wstring& text)
{
	size_t start = 0;
	size_t end = text.size();
	while (start < end && iswspace(text[start])) {
		start++;
	}
	while (end > start && iswspace(text[end - 1])) {
		end--;
	}
	return text.substr(start, end - start);
}

// Botones de entrada de la pagina de consumo (incluye rangos).
bool ReadButtons(PHIDP_PREPARSED_DATA preparsed, const HIDP_CAPS& caps,
	std::vector<HIDP_BUTTON_CAPS>& list)
{
	if (!caps.NumberInputButtonCaps) {
		return false;
	}
	list.resize(caps.NumberInputButtonCaps);
	USHORT count = caps.NumberInputButtonCaps;
	if (HidP_GetButtonCaps(HidP_Input, list.data(), &count, preparsed) != HIDP_STATUS_SUCCESS) {
		return false;
	}
	list.resize(count);
	return true;
}

bool HasUsage(PHIDP_PREPARSED_DATA preparsed, const HIDP_CAPS& caps, USHORT usage)
{
	std::vector<HIDP_BUTTON_CAPS> list;
	if (!ReadButtons(preparsed, caps, list)) {
		return false;
	}
	for (const HIDP_BUTTON_CAPS& item : list) {
		if (item.UsagePage != kConsumerPage) {
			continue;
		}
		if (item.IsRange) {
			if (usage >= item.Range.UsageMin && usage <= item.Range.UsageMax) {
				return true;
			}
		}
		else if (item.NotRange.Usage == usage) {
			return true;
		}
	}
	return false;
}

} // namespace

HidLink& HidLink::Instance()
{
	static HidLink instance;
	return instance;
}

void HidLink::Start(HWND hwnd, const std::wstring& filter)
{
	if (running_) {
		return;
	}
	hwnd_ = hwnd;
	filter_ = filter;
	stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!stopEvent_) {
		return;
	}
	running_ = true;
	thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
	if (!thread_) {
		running_ = false;
		CloseHandle(stopEvent_);
		stopEvent_ = nullptr;
	}
}

void HidLink::Stop()
{
	if (!running_) {
		return;
	}
	running_ = false;
	if (stopEvent_) {
		SetEvent(stopEvent_);
	}
	if (thread_) {
		WaitForSingleObject(thread_, 3000);
		CloseHandle(thread_);
		thread_ = nullptr;
	}
	if (stopEvent_) {
		CloseHandle(stopEvent_);
		stopEvent_ = nullptr;
	}
	hwnd_ = nullptr;
}

DWORD WINAPI HidLink::ThreadProc(LPVOID param)
{
	HidLink* self = (HidLink*)param;
	self->ReadLoop();
	return 0;
}

bool HidLink::MatchesFilter(unsigned vid, unsigned pid) const
{
	if (filter_.empty()) {
		return true;
	}
	wchar_t buffer[32] = { 0 };
	swprintf_s(buffer, L"%04X:%04X", vid, pid);
	const std::wstring wanted = buffer;

	size_t start = 0;
	while (start <= filter_.size()) {
		size_t end = filter_.find(L';', start);
		std::wstring item = filter_.substr(start,
			end == std::wstring::npos ? std::wstring::npos : end - start);
		if (_wcsicmp(Trim(item).c_str(), wanted.c_str()) == 0) {
			return true;
		}
		if (end == std::wstring::npos) {
			break;
		}
		start = end + 1;
	}
	return false;
}

// Abre la interfaz indicada y comprueba que es un control de consumo con 0xCD.
bool HidLink::Attach(const std::wstring& path)
{
	HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	PHIDP_PREPARSED_DATA preparsed = nullptr;
	if (!HidD_GetPreparsedData(handle, &preparsed)) {
		CloseHandle(handle);
		return false;
	}
	HIDP_CAPS caps;
	ZeroMemory(&caps, sizeof(caps));
	if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS
		|| caps.UsagePage != kConsumerPage || caps.Usage != kConsumerControl
		|| !HasUsage(preparsed, caps, kUsagePlayPause)) {
		HidD_FreePreparsedData(preparsed);
		CloseHandle(handle);
		return false;
	}

	HIDD_ATTRIBUTES attributes;
	attributes.Size = sizeof(attributes);
	HidD_GetAttributes(handle, &attributes);

	device_ = handle;
	preparsed_ = preparsed;
	reportLength_ = caps.InputReportByteLength;
	lastDown_ = false;
	ResetEvent(readEvent_);
	PJ_LOG(2, ("SipCore", "hid headset open vid=%04X pid=%04X report=%u",
		attributes.VendorID, attributes.ProductID, (unsigned)reportLength_));
	return true;
}

// Busca un auricular: interfaz de consumo con el boton 0xCD que no pertenezca
// a un teclado (los teclados exponen teclas multimedia con el mismo usage).
bool HidLink::OpenDevice()
{
	GUID guid;
	HidD_GetHidGuid(&guid);
	HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (set == INVALID_HANDLE_VALUE) {
		return false;
	}

	// La coleccion de teclado de Windows acaba en "\kbd" y no se puede abrir
	// (la tiene el sistema), asi que se identifica por la ruta.
	struct Entry {
		std::wstring path;
		unsigned vid = 0;
		unsigned pid = 0;
		bool keyboard = false;
	};
	std::vector<Entry> entries;

	SP_DEVICE_INTERFACE_DATA ifData;
	ifData.cbSize = sizeof(ifData);
	for (DWORD index = 0;
		SetupDiEnumDeviceInterfaces(set, nullptr, &guid, index, &ifData); index++) {
		DWORD size = 0;
		SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &size, nullptr);
		if (!size) {
			continue;
		}
		std::vector<BYTE> buffer(size);
		PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail =
			(PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer.data();
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		if (!SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, size, nullptr, nullptr)) {
			continue;
		}

		Entry entry;
		entry.path = detail->DevicePath;
		const std::wstring lower = Lower(entry.path);
		entry.vid = ParseId(lower, L"vid_");
		entry.pid = ParseId(lower, L"pid_");
		entry.keyboard = lower.size() > 4
			&& lower.compare(lower.size() - 4, 4, L"\\kbd") == 0;
		entries.push_back(entry);
	}
	SetupDiDestroyDeviceInfoList(set);

	for (const Entry& entry : entries) {
		if (entry.keyboard || !MatchesFilter(entry.vid, entry.pid)) {
			continue;
		}
		// Descarta teclados y ratones: sus teclas multimedia no son un auricular.
		bool fromKeyboard = false;
		for (const Entry& other : entries) {
			if (other.keyboard && other.vid == entry.vid && other.pid == entry.pid) {
				fromKeyboard = true;
				break;
			}
		}
		if (fromKeyboard) {
			continue;
		}
		if (Attach(entry.path)) {
			return true;
		}
	}
	return false;
}

void HidLink::CloseDevice()
{
	if (preparsed_) {
		HidD_FreePreparsedData((PHIDP_PREPARSED_DATA)preparsed_);
		preparsed_ = nullptr;
	}
	if (device_ != INVALID_HANDLE_VALUE) {
		CloseHandle(device_);
		device_ = INVALID_HANDLE_VALUE;
		PJ_LOG(2, ("SipCore", "hid headset close"));
	}
	reportLength_ = 0;
	lastDown_ = false;
}

void HidLink::ReadLoop()
{
	// pjlib exige registrar los hilos ajenos antes de usar PJ_LOG.
	pj_thread_desc desc;
	pj_thread_t* thread = nullptr;
	pj_thread_register("hidlink", desc, &thread);

	readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!readEvent_) {
		return;
	}

	while (running_) {
		if (device_ == INVALID_HANDLE_VALUE && !OpenDevice()) {
			WaitForSingleObject(stopEvent_, kRescanMs);
			continue;
		}

		std::vector<BYTE> report(reportLength_ ? reportLength_ : 8);
		OVERLAPPED overlapped;
		ZeroMemory(&overlapped, sizeof(overlapped));
		ResetEvent(readEvent_);
		overlapped.hEvent = readEvent_;

		DWORD read = 0;
		if (!ReadFile(device_, report.data(), (DWORD)report.size(), &read, &overlapped)
			&& GetLastError() != ERROR_IO_PENDING) {
			CloseDevice();
			WaitForSingleObject(stopEvent_, kRetryMs);
			continue;
		}

		// Espera por trozos para poder parar y detectar desconexiones.
		HANDLE waits[2] = { stopEvent_, readEvent_ };
		bool completed = false;
		bool stopping = false;
		bool gone = false;
		while (!completed && !stopping && !gone) {
			DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 1000);
			if (wait == WAIT_OBJECT_0) {
				stopping = true;
			}
			else if (wait == WAIT_OBJECT_0 + 1) {
				completed = true;
			}
			else {
				HIDD_ATTRIBUTES attributes;
				attributes.Size = sizeof(attributes);
				if (!HidD_GetAttributes(device_, &attributes)) {
					gone = true;
				}
			}
		}

		if (stopping || gone) {
			CancelIoEx(device_, &overlapped);
			WaitForSingleObject(readEvent_, kRetryMs);
			if (gone) {
				CloseDevice();
				if (!stopping) {
					WaitForSingleObject(stopEvent_, kRetryMs);
				}
			}
			continue;
		}

		if (!GetOverlappedResult(device_, &overlapped, &read, FALSE) || read == 0) {
			CloseDevice();
			WaitForSingleObject(stopEvent_, kRetryMs);
			continue;
		}
		HandleReport(report.data(), read);
	}

	CloseDevice();
	CloseHandle(readEvent_);
	readEvent_ = nullptr;
}

void HidLink::HandleReport(const unsigned char* data, unsigned long length)
{
	if (!preparsed_ || !data || length == 0) {
		return;
	}

	// El descriptor puede o no incluir Report ID al principio del buffer: se
	// prueban ambos desplazamientos y se usa el que el HID acepte.
	bool down = false;
	bool parsed = false;
	for (unsigned long skip = 0; skip < 2 && skip < length && !parsed; skip++) {
		USAGE usages[16];
		ULONG count = 16;
		const NTSTATUS status = HidP_GetUsages(HidP_Input, kConsumerPage, 0, usages, &count,
			(PHIDP_PREPARSED_DATA)preparsed_, (PCHAR)(data + skip), length - skip);
		if (status != HIDP_STATUS_SUCCESS) {
			continue;
		}
		parsed = true;
		for (ULONG i = 0; i < count; i++) {
			if (usages[i] == kUsagePlayPause) {
				down = true;
			}
		}
	}
	if (!parsed || down == lastDown_) {
		return;
	}

	lastDown_ = down;
	PJ_LOG(2, ("SipCore", "hid button %s", down ? "down" : "up"));
	if (hwnd_) {
		PostMessageW(hwnd_, MSIP_HID_BUTTON_MSG, down ? 1 : 0, 0);
	}
}
