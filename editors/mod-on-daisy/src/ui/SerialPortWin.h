#pragma once
// Windows side of the serial transport: the kit's USB CDC port as a SerialBackend with
// overlapped I/O, and the enumeration of COM ports with their USB VID/PID through SetupAPI
// (the Python reference filters pyserial's list by VID 0483 / PID 5740). Windows SDK only,
// no third-party library, no driver. Used only on the I/O thread of ThreadedSerialLink
// except Cancel(), which the GUI thread calls before joining.
#if defined(_WIN32)
#include "../core/SerialTransport.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace editor::ddp {
class Win32SerialBackend : public SerialBackend {
  public:
    static std::unique_ptr<SerialBackend> Open(const std::string& device, std::string& error) {
        const std::string path = "\\\\.\\" + device;
        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if(h == INVALID_HANDLE_VALUE) {
            error = "CreateFile failed (" + std::to_string(GetLastError()) + ")";
            return nullptr;
        }
        // Every configuration call is checked (review finding 10): a port that is not
        // configured would look like a silent device, because the firmware sends only with DTR.
        auto fail = [&](const char* call) {
            error = std::string(call) + " failed (" + std::to_string(GetLastError()) + ")";
            CloseHandle(h);
            return std::unique_ptr<SerialBackend>();
        };
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if(!GetCommState(h, &dcb))
            return fail("GetCommState");
        dcb.BaudRate = CBR_115200; // irrelevant for USB CDC, set like pyserial does
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE; // the firmware sends only while DTR is set
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if(!SetCommState(h, &dcb))
            return fail("SetCommState");
        // Return as soon as any byte is there, or after the constant; writes bounded too.
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.WriteTotalTimeoutConstant = 500;
        if(!SetCommTimeouts(h, &timeouts))
            return fail("SetCommTimeouts");
        if(!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR))
            return fail("PurgeComm");
        std::unique_ptr<Win32SerialBackend> backend(new Win32SerialBackend(h));
        if(!backend->read_event_ || !backend->write_event_) {
            error = "CreateEvent failed";
            return nullptr;
        }
        return backend;
    }

    ~Win32SerialBackend() override {
        if(handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
        }
        if(read_event_)
            CloseHandle(read_event_);
        if(write_event_)
            CloseHandle(write_event_);
    }

    // Both calls return promptly after Cancel(): the pending I/O is aborted and the wait
    // loops check the flag every 20 ms, so a cancel that lands between the check and the
    // OS call is still honoured within one slice.
    int Read(uint8_t* buffer, std::size_t max, uint32_t timeout_ms) override {
        if(cancelled_)
            return 0;
        OVERLAPPED ov{};
        ov.hEvent = read_event_;
        ResetEvent(read_event_);
        DWORD n = 0;
        if(ReadFile(handle_, buffer, DWORD(max), &n, &ov))
            return int(n);
        if(GetLastError() != ERROR_IO_PENDING)
            return -1;
        // The driver honours the COMMTIMEOUTS; the wait only guards against one that does not.
        const bool timed_out = !WaitSlices(read_event_, timeout_ms + 200);
        if(timed_out || cancelled_)
            CancelIoEx(handle_, &ov);
        if(!GetOverlappedResult(handle_, &ov, &n, TRUE)) {
            const DWORD err = GetLastError();
            if(cancelled_ || (timed_out && err == ERROR_OPERATION_ABORTED))
                return 0;
            return -1; // the device is gone (surprise removal, re-enumeration)
        }
        return int(n);
    }

    bool Write(const uint8_t* data, std::size_t n) override {
        if(cancelled_)
            return false;
        OVERLAPPED ov{};
        ov.hEvent = write_event_;
        ResetEvent(write_event_);
        DWORD written = 0;
        if(!WriteFile(handle_, data, DWORD(n), &written, &ov)) {
            if(GetLastError() != ERROR_IO_PENDING)
                return false;
            // Bounded like the COMMTIMEOUTS write timeout; a cancel ends the wait early.
            if(!WaitSlices(write_event_, 600) || cancelled_)
                CancelIoEx(handle_, &ov);
            if(!GetOverlappedResult(handle_, &ov, &written, TRUE))
                return false;
        }
        return written == n;
    }

    void Cancel() override {
        cancelled_ = true;
        CancelIoEx(handle_, nullptr);
    }

  private:
    explicit Win32SerialBackend(HANDLE h)
        : handle_(h), read_event_(CreateEventA(nullptr, TRUE, FALSE, nullptr)),
          write_event_(CreateEventA(nullptr, TRUE, FALSE, nullptr)) {}

    // Waits for the event in 20 ms slices; false after `total_ms` or once cancelled.
    bool WaitSlices(HANDLE event, uint32_t total_ms) const {
        for(uint32_t waited = 0; waited < total_ms; waited += 20) {
            if(WaitForSingleObject(event, 20) == WAIT_OBJECT_0)
                return true;
            if(cancelled_)
                return false;
        }
        return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE read_event_ = nullptr, write_event_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

// Present COM ports with the USB VID/PID of their device instance ("USB\VID_0483&PID_5740&MI_00\...").
inline std::vector<PortInfo> ListComPorts() {
    std::vector<PortInfo> ports;
    // GUID_DEVINTERFACE_COMPORT {86E0D1E0-8089-11D0-9CE4-08003E301F73}
    static const GUID kComPort = {0x86E0D1E0, 0x8089, 0x11D0, {0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73}};
    HDEVINFO set = SetupDiGetClassDevsA(&kComPort, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if(set == INVALID_HANDLE_VALUE)
        return ports;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for(DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        PortInfo port;
        char id[512] = {};
        if(SetupDiGetDeviceInstanceIdA(set, &info, id, sizeof(id), nullptr)) {
            const char* vid = std::strstr(id, "VID_");
            const char* pid = std::strstr(id, "PID_");
            if(vid && pid) {
                port.vid = int(std::strtol(vid + 4, nullptr, 16));
                port.pid = int(std::strtol(pid + 4, nullptr, 16));
            }
        }
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if(key != INVALID_HANDLE_VALUE) {
            char name[64] = {};
            DWORD size = sizeof(name) - 1, type = 0;
            if(RegQueryValueExA(key, "PortName", nullptr, &type, reinterpret_cast<LPBYTE>(name), &size) == ERROR_SUCCESS
               && type == REG_SZ)
                port.device = name;
            RegCloseKey(key);
        }
        char friendly[256] = {};
        DWORD size = sizeof(friendly) - 1;
        if(SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_FRIENDLYNAME, nullptr, reinterpret_cast<PBYTE>(friendly), size, &size))
            port.description = friendly;
        if(!port.device.empty())
            ports.push_back(port);
    }
    SetupDiDestroyDeviceInfoList(set);
    return ports;
}

// The port provider of the control panel on Windows.
inline std::unique_ptr<ThreadedSerialPorts> MakeWindowsSerialPorts() {
    return std::make_unique<ThreadedSerialPorts>([] { return ListComPorts(); },
                                                 [](const std::string& device, std::string& error) {
                                                     return Win32SerialBackend::Open(device, error);
                                                 });
}
}
#endif
