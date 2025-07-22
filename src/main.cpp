#include <algorithm>
#include <stdio.h>
#include <stdint.h>

#include <Windows.h>
#include <hidapi.h>

static hid_device* g_handle = nullptr;

typedef struct {
	BITMAPINFOHEADER    bmiHeader;
	DWORD               bmiColors[3];
} BITMAPINFOJANK;

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE: {
		SetTimer(hwnd, 1, 8, NULL);
		break;
	}
	case WM_DESTROY: {
		KillTimer(hwnd, 1);
		PostQuitMessage(0);
		break;
	}
	case WM_TIMER: {
		InvalidateRect(hwnd, NULL, FALSE);
		break;
	}
	case WM_PAINT: {
		static uint16_t fb[160 * 80];
		static uint8_t packet[1024] = {
			0x22, 0x03,
			0x00, 0x00,
			0x08, 0x00, 0x25, 0x00,
			0x00, 0x00, 0x00, 0x00,
		};
		static uint8_t res[1024];

		for (uint32_t i = 0; i < sizeof(fb); i += 0x3F4) {
			*(uint32_t*)&packet[8] = i;
			*(uint16_t*)&packet[2] = 0;
			uint16_t checksum = 0;
			for (int index = 0; index < sizeof(packet); index += 2)
				checksum += *(uint16_t*)&packet[index];
			*(uint16_t*)&packet[2] = checksum;

			if (hid_write(g_handle, packet, sizeof(packet)) == -1) {
				printf("hid_write failed: %ls\n", hid_error(g_handle));
				CloseWindow(hwnd);
				return 0;
			};
		}

		for (uint32_t i = 0; i < sizeof(fb); i += 0x3F4) {
			if (hid_read_timeout(g_handle, res, sizeof(res), 2000) == -1) {
				printf("hid_read_timeout failed: %ls\n", hid_error(g_handle));
				CloseWindow(hwnd);
				return 0;
			};

			memcpy(&fb[i / 2], &res[0xC], std::clamp((int)*(uint16_t*)&res[4], 8, 0x3FC) - 8);
		}

		uint16_t temp[160];
		for (int i = 0; i < 40; i++) {
			memcpy(temp, &fb[i * 160], 160 * 2);
			memcpy(&fb[i * 160], &fb[(80 - i - 1) * 160], 160 * 2);
			memcpy(&fb[(80 - i - 1) * 160], temp, 160 * 2);
		}

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
		StretchDIBits(hdc, 0, 0, 160 * 4, 80 * 4, 0, 0, 160, 80, fb, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, SRCCOPY);

		EndPaint(hwnd, &ps);
		break;
	}
	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	};
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
	auto* enum_list = hid_enumerate(0x8089, 0x0009);
	if (!enum_list) {
		printf("hid_enumerate failed: %ls\n", hid_error(NULL));
		return 1;
	}

	for (auto* dev = enum_list; dev; dev = dev->next) {
		if (dev->usage_page == 0xFF12) {
			g_handle = hid_open_path(dev->path);
			if (!g_handle) {
				printf("hid_open_path failed: %ls\n", hid_error(NULL));
				return 1;
			}
		}
	}

	if (g_handle == nullptr) {
		printf("failed to find device\n");
		return 1;
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

	hid_close(g_handle);
	return 0;
}
