//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================

#include "render_window.hpp"
#include <Windows.h>

#include <iostream>
#include <vector>
#include <d3dcompiler.h>
//#include"NV12LFshaders.hpp"
#include <d3d11_3.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include<assert.h>

template <typename T> inline void reset_ptr(T** ppObj) {
    if (ppObj[0] != nullptr) {
        ppObj[0]->Release();
        ppObj[0] = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	PAINTSTRUCT ps;
	HDC hdc;

	// set or extract pointer to the class attached to hWnd
	render_window* current_wnd = nullptr;
	if (message == WM_NCCREATE || message == WM_CREATE)
	{
		CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
		current_wnd = reinterpret_cast<render_window*>(pCreate->lpCreateParams);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)current_wnd);
	}
	else
	{
		current_wnd = reinterpret_cast<render_window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}


	switch (message) {
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	//case WM_KEYDOWN:
	//	printf("key is %c\n", wParam);
	//	switch (wParam)
	//	{
	//	case 'A':
	//		printf("key is %c\n", wParam);
	//		break;
	//	default:
	//		break;
	//	}
	case WM_CHAR: //this is just for a program exit besides window's borders/taskbar
		if (wParam == VK_ESCAPE)
		{
			DestroyWindow(hWnd);
		}
		// process other keys here
		break;
	case WM_SIZE:
		//if (wParam == SIZE_MINIMIZED)
		//{
		//	// The window was minimized (you should probably suspend the application)
		//	if (!s_minimized)
		//	{
		//		s_minimized = true;
		//	}
		//}
		//else if (s_minimized)
		//{
		//	// The window was minimized and is now restored (resume from suspend)
		//	s_minimized = false;
		//}
		//else if (!s_in_sizemove)
		//{
		//	// Here is where you handle the swapchain resize for maximize or unmaximize
		//}
		break;

	//case WM_ENTERSIZEMOVE:
	//	// We want to avoid trying to resizing the swapchain as the user does the 'rubber band' resize
	//	//s_in_sizemove = true;
	//	break;

	//case WM_EXITSIZEMOVE:
	//	//s_in_sizemove = false;
	//	// Here is the other place where you handle the swapchain resize after the user stops using the 'rubber-band' 
	//	break;

	//case WM_GETMINMAXINFO:
	//{
	//	// We want to prevent the window from being set too tiny
	//	auto info = reinterpret_cast<MINMAXINFO*>(lParam);
	//	info->ptMinTrackSize.x = 320;
	//	info->ptMinTrackSize.y = 200;
	//}
	//break;
	//case WM_MOUSEWHEEL :
	//{
	//    // process scrolling here
	//      fwKeys = GET_KEYSTATE_WPARAM(wParam);
	//      zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
	//      xPos = GET_X_LPARAM(lParam); 
	//      yPos = GET_Y_LPARAM(lParam);
	//}
	//break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);

	}
	return 0;
}

bool render_window::init(HINSTANCE hInstance, int x, int y, int w, int h) {

	// Register class
	WNDCLASSEX wcex;
	if (GetClassInfoEx(hInstance, L"LFRenderWindowClass", &wcex) == FALSE) {
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = nullptr;
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = L"LFRenderWindowClass";
		wcex.hIconSm = nullptr;
		if (!RegisterClassEx(&wcex))
			return false;
	}
	// Create window
	//RECT rc = { 0, 0, 7680, 4320 };
	//long long t = rc.right - rc.left;
	//long long m = rc.bottom - rc.top;
	//AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	hwnd = CreateWindow(L"LFRenderWindowClass", L"Direct3D 11 LF Render Sample",
		WS_POPUP,
		x, y, w, h, nullptr, nullptr, hInstance,
		this);
	if (hwnd==NULL)
		return false;
	x0 = x; y0 = y; width = w; height = h;

	ShowWindow(hwnd, SW_MAXIMIZE);
	return true;
}

int render_window::run() {
	/* Implement message loop */
	int bRet;
	MSG msg;      /* incoming message to be evaluated */
	while (bRet = GetMessage(&msg, NULL, 0, 0))
	{
		if (bRet == -1) {
			/* handle the error and possibly exit */
			return 0;
		}
		else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
}
