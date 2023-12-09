#include <stdio.h>
#include <windows.h>
#include <vector>
#include <string>
#include <inttypes.h>
#include <conio.h>

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Fatal(const char *fmt, ...) {
    fprintf(stderr, "FATAL: ");

    va_list v;
    va_start(v, fmt);
    vfprintf(stderr, fmt, v);
    va_end(v);

    fprintf(stderr, "\n");
    exit(1);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static std::string GetWindowsErrorString(DWORD err) {
    char *message;
    DWORD format_message_result = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, (LPSTR)&message, 0, nullptr);
    if (format_message_result == 0 || !message) {
        if (message) {
            LocalFree(message), message = nullptr;
        }

        char msg[1000];
        snprintf(msg, sizeof msg, "FormatMessage failed with result %" PRIu32 " (0x%" PRIu32 ") for error %" PRIu32 " (0x%" PRIx32 ")", format_message_result, format_message_result, err, err);
        return msg;
    }

    // strip trailing newlines
    char *eom = message + format_message_result;
    while (eom > message && isspace(eom[-1])) {
        *--eom = 0;
    }

    std::string result = message;

    LocalFree(message), message = nullptr;

    return result;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Syntax() {
    printf("syntax:\n");
    printf("  tube_serial_test list - list COM port names\n");
    printf("  tube_serial_test recv PORTNAME - receive Acorn->PC data over PORTNAME\n");
    exit(0);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void List() {
    auto get_comm_ports_fn = (ULONG(WINAPI *)(PULONG, ULONG, PULONG))GetProcAddress(LoadLibraryW(L"KernelBase.dll"), "GetCommPorts");
    if (!get_comm_ports_fn) {
        Fatal("GetCommPorts function not found. Needs Windows 10 version 1803 or later");
    }

    ULONG num_ports_found;
    (*get_comm_ports_fn)(nullptr, 0, &num_ports_found);
    if (num_ports_found == 0) {
        Fatal("No COM ports found");
    }
    std::vector<ULONG> ports(num_ports_found);
    (*get_comm_ports_fn)(ports.data(), ports.size(), &num_ports_found);

    printf("%zu COM port(s):\n", ports.size());
    for (ULONG port : ports) {
        printf("  COM%lu\n", port);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static HANDLE OpenPort(const std::string &port_name, DWORD flags) {
    std::string port_path = "\\\\.\\" + port_name;
    HANDLE port_h = CreateFileA(port_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, flags, nullptr);
    if (port_h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        Fatal("Failed to open ``%s'': %s", port_path.c_str(), GetWindowsErrorString(err).c_str());
    }

    DCB dcb = {sizeof dcb};
    if (!GetCommState(port_h, &dcb)) {
        Fatal("GetCommState failed: %s", GetWindowsErrorString(GetLastError()).c_str());
    }

    dcb.ByteSize = 8;

    if (!SetCommState(port_h, &dcb)) {
        Fatal("SetCommState failed: %s", GetWindowsErrorString(GetLastError()).c_str());
    }

    if (!PurgeComm(port_h, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR)) {
        printf("PurgeCommPort failed: %s\n", GetWindowsErrorString(GetLastError()).c_str());
    }

    return port_h;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Recv(const std::string &port_name) {
    HANDLE port_h = OpenPort(port_name, FILE_FLAG_OVERLAPPED);

    //printf("Setting comm mask: ");
    //if (SetCommMask(port_h, EV_RXCHAR)) {
    //    printf("ok\n");
    //} else {
    //    printf("failed: %s\n", GetWindowsErrorString(GetLastError()).c_str());
    //}

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr); //TRUE=manual reset, FALSE=initially unsignaled
    BYTE recv_buffer[1000] = {};
    DWORD num_read = 0;
    int64_t total_num_read = 0;
    constexpr int64_t report_freq = 65536;
    bool io_pending = false;
    int last_value = -1;
    DWORD64 last_io_ticks = GetTickCount64();
    size_t num_errors = 0;
    size_t error_counts[256] = {};
    size_t transfer_counts[256] = {};

    for (;;) {
        bool print_errors = false;

        if (!io_pending) {
            BOOL ok = ReadFile(port_h, recv_buffer, sizeof recv_buffer, &num_read, &overlapped);
            if (ok) {
                // got some data straight away.
            } else {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    io_pending = true;
                    last_io_ticks = GetTickCount64();
                } else {
                    Fatal("read failed: %s", GetWindowsErrorString(err).c_str());
                }
            }
        }

        if (io_pending) {
            DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 1000);
            if (wait_result == WAIT_OBJECT_0) {
                io_pending = false;

                // https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped
                num_read = overlapped.InternalHigh;
            } else if (wait_result == WAIT_TIMEOUT) {
                printf("Waiting for input: %.3f secs\n", (GetTickCount64() - last_io_ticks) / 1000.);
            } else {
                Fatal("WaitForSingleObject failed: %s", GetWindowsErrorString(GetLastError()).c_str());
            }
        }

        if (!io_pending) {
            for (DWORD i = 0; i < num_read; ++i) {
                if (recv_buffer[i] == last_value) {
                    ++error_counts[recv_buffer[i]];
                    ++num_errors;
                    if (num_errors % 1000 == 1) {
                        print_errors = true;
                    }
                } else {
                    ++transfer_counts[recv_buffer[i]];
                }
                last_value = recv_buffer[i];
            }
        }

        if (_kbhit()) {
            int c = tolower(_getch());
            switch (c) {
            case 'q':
                goto done;

            case ' ':
                print_errors = true;
                break;
            }
        }

        if (print_errors) {
            printf("Errors:");
            if (num_errors == 0) {
                printf("none");
            } else {
                for (unsigned i = 0; i < 256; ++i) {
                    if (error_counts[i] > 0) {
                        printf(" 0x%02x: %zu/%zu", i, error_counts[i], transfer_counts[i]);
                    }
                }
            }
            printf("\n");
        }
    }
done:;

    CloseHandle(port_h), port_h = INVALID_HANDLE_VALUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void PrintValue(BYTE value) {
    printf("value: %03u (0x%02x)", value, value);

    if (value >= 32 && value < 127) {
        printf(" ('%c')", (char)value);
    }

    printf("\n");
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Write1AndFlush(HANDLE port_h, BYTE value) {
    DWORD num_written;
    BOOL write_ok = WriteFile(port_h, &value, 1, &num_written, nullptr);
    if (!write_ok) {
        DWORD err = GetLastError();
        Fatal("write failed: %s", GetWindowsErrorString(err).c_str());
    } else if (num_written != 1) {
        Fatal("wrote unexpected number of bytes: %lu", num_written);
    }

    BOOL flush_ok = FlushFileBuffers(port_h);
    if (!flush_ok) {
        DWORD err = GetLastError();
        Fatal("flush failed: %s", GetWindowsErrorString(err).c_str());
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void EchoOrDrain(const std::string &port_name, bool echo) {
    HANDLE port_h = OpenPort(port_name, 0);

    uint64_t num_bytes = 0;

    while (!_kbhit()) {
        BYTE value;

        DWORD num_read;
        BOOL read_ok = ReadFile(port_h, &value, 1, &num_read, nullptr);
        if (!read_ok) {
            DWORD err = GetLastError();
            Fatal("read failed: %s", GetWindowsErrorString(err).c_str());
        } else if (num_read != 1) {
            Fatal("read unexpected number of bytes: %lu", num_read);
        }

        PrintValue(value);

        if (echo) {
            Write1AndFlush(port_h, value);
        }

        ++num_bytes;
        if (num_bytes % 10000 == 1) {
            printf("transferred %" PRIu64 " bytes\n", num_bytes);
        }
    }

    CloseHandle(port_h), port_h = INVALID_HANDLE_VALUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Send(const std::string &port_name) {
    HANDLE port_h = OpenPort(port_name, 0);

    for (;;) {
        int c = _getch();
        if (c == 27) {
            break;
        }

        PrintValue((BYTE)c);

        Write1AndFlush(port_h, (BYTE)c);
    }

    CloseHandle(port_h), port_h = INVALID_HANDLE_VALUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    if (argc < 2) {
        Syntax();
    }

    //setvbuf(stdout, nullptr, _IONBF, 0);

    if (_stricmp(argv[1], "list") == 0) {
        if (argc != 2) {
            Syntax();
        }

        List();
    } else if (_stricmp(argv[1], "recv") == 0) {
        if (argc < 3) {
            Syntax();
        }

        Recv(argv[2]);
    } else if (_stricmp(argv[1], "echo") == 0) {
        if (argc < 3) {
            Syntax();
        }
        EchoOrDrain(argv[2], true);
    } else if (_stricmp(argv[1], "drain") == 0) {
        if (argc < 3) {
            Syntax();
        }
        EchoOrDrain(argv[2], false);
    } else if (_stricmp(argv[1], "send") == 0) {
        if (argc < 3) {
            Syntax();
        }
        Send(argv[2]);
    } else {
        Fatal("unrecognised subcommand: %s", argv[1]);
        return 1;
    }

    return 0;
}
