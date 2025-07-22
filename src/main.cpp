#include <algorithm>
#include <stdio.h>
#include <stdint.h>
#include <vector>

#include <Windows.h>
#include <hidapi.h>

static hid_device* g_handle = nullptr;
static bool g_reconnecting = false;
static std::vector<uint16_t> g_framebuffer(160 * 80, 0);

typedef struct {
	BITMAPINFOHEADER    bmiHeader;
	DWORD               bmiColors[3];
} BITMAPINFOJANK;

bool TryConnectToHidDevice() {
    if (g_handle) {
        hid_close(g_handle);
        g_handle = nullptr;
    }

    auto* enum_list = hid_enumerate(0x8089, 0x0009);
    if (!enum_list) {
        fprintf(stderr, "hid_enumerate failed during reconnection attempt: %ls\n", hid_error(NULL));
        return false;
    }

    for (auto* dev = enum_list; dev; dev = dev->next) {
        if (dev->usage_page == 0xFF12) {
            g_handle = hid_open_path(dev->path);
            if (g_handle) {
                hid_free_enumeration(enum_list);
                fprintf(stdout, "Successfully reconnected to HID device.\n");
                std::fill(g_framebuffer.begin(), g_framebuffer.end(), 0);
                return true;
            }
        }
    }
    hid_free_enumeration(enum_list);
    fprintf(stderr, "Failed to find or open HID device during reconnection attempt.\n");
    return false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE: {
		SetTimer(hwnd, 1, 8, NULL);
        SetTimer(hwnd, 2, 1000, NULL);
		break;
	}
	case WM_DESTROY: {
		KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
		PostQuitMessage(0);
		break;
	}
	case WM_TIMER: {
        if (wParam == 1) {
            if (g_reconnecting || g_handle == nullptr) {
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            static uint8_t packet[1024] = {
                0x22, 0x03,
                0x00, 0x00,
                0x08, 0x00, 0x25, 0x00,
                0x00, 0x00, 0x00, 0x00,
            };
            static uint8_t res[1024];
            bool comm_error = false;

            for (uint32_t i = 0; i < g_framebuffer.size() * sizeof(uint16_t); i += 0x3F4) {
                *(uint32_t*)&packet[8] = i;
                *(uint16_t*)&packet[2] = 0;
                uint16_t checksum = 0;
                for (int index = 0; index < sizeof(packet); index += 2)
                    checksum += *(uint16_t*)&packet[index];
                *(uint16_t*)&packet[2] = checksum;

                if (hid_write(g_handle, packet, sizeof(packet)) == -1) {
                    printf("hid_write failed: %ls\n", hid_error(g_handle));
                    comm_error = true;
                    break;
                };
            }

            if (!comm_error) {
                for (uint32_t i = 0; i < g_framebuffer.size() * sizeof(uint16_t); i += 0x3F4) {
                    if (hid_read_timeout(g_handle, res, sizeof(res), 2000) == -1) {
                        fprintf(stderr, "hid_read_timeout failed: %ls\n", hid_error(g_handle));
                        comm_error = true;
                        break;
                    };
                    memcpy(&g_framebuffer[i / sizeof(uint16_t)], &res[0xC], std::clamp((int)*(uint16_t*)&res[4], 8, 0x3FC) - 8);
                }
            }

            if (comm_error) {
                g_reconnecting = true;
                hid_close(g_handle);
                g_handle = nullptr;
                std::fill(g_framebuffer.begin(), g_framebuffer.end(), 0);
                fprintf(stderr, "Error on connection, entering reconnection mode...\n");
            } else {
                std::vector<uint16_t> temp_row(160);
                for (int i = 0; i < 40; i++) {
                    memcpy(temp_row.data(), &g_framebuffer[i * 160], 160 * sizeof(uint16_t));
                    memcpy(&g_framebuffer[i * 160], &g_framebuffer[(80 - i - 1) * 160], 160 * sizeof(uint16_t));
                    memcpy(&g_framebuffer[(80 - i - 1) * 160], temp_row.data(), 160 * sizeof(uint16_t));
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wParam == 2) {
            if (g_reconnecting) {
                fprintf(stderr, "Attempting to reconnect...\n");
                if (TryConnectToHidDevice()) {
                    g_reconnecting = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }

        }
		break;
	}
	case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        BITMAPINFOJANK bmi = {
            .bmiHeader = {
                .biSize = sizeof(BITMAPINFOHEADER),
                .biWidth = 160,
                .biHeight = 80,
                .biPlanes = 1,
                .biBitCount = 16,
                .biCompression = BI_BITFIELDS
            },
            .bmiColors = {0xF800, 0x07E0, 0x001F},
        };
        StretchDIBits(hdc, 0, 0, 160 * 4, 80 * 4, 0, 0, 160, 80, g_framebuffer.data(), (BITMAPINFO*)&bmi, DIB_RGB_COLORS, SRCCOPY);

        if (g_reconnecting) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 0, 0));
            RECT textRect = {0, 0, 160 * 4, 80 * 4};
            DrawText(hdc, "Device disconnected. Reconnecting...", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        break;
	}
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	};
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    if (!TryConnectToHidDevice()) {
        fprintf(stderr, "Initial connection to HID device failed. Starting in reconnection mode.\n");
        g_reconnecting = true;
    }

	HINSTANCE instance = hInstance;
	WNDCLASS wc = {
		.lpfnWndProc = WndProc,
		.hInstance = instance,
		.lpszClassName = "MainWnd",
	};
	RegisterClass(&wc);

	RECT window_rect = { 0, 0, 160 * 4, 80 * 4 };
	AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);
	HWND window = CreateWindow(
		"MainWnd",
		"SayoDevice O3C Display",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT,
		window_rect.right - window_rect.left, window_rect.bottom - window_rect.top,
		NULL, NULL, instance, 0
	);

	ShowWindow(window, SW_NORMAL);
	UpdateWindow(window);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (g_handle) {
        hid_close(g_handle);
    }
    hid_exit();
	return 0;
}
