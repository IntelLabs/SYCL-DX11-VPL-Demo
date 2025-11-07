//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
#define NOMINMAX
// the file below should go first or else...
//#include "../lib/BridgeRuntime/bridge_utils.hpp"

#include "render_window.hpp"
#include <windows.h>
#include <windowsx.h>

#include <iostream>
#include <vector>
#include <chrono>

#include <d3d11_3.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#include<assert.h>

#include "RenderDX11.hpp"
#include "vplcodec.h"
#include "dpcpp_compute.hpp"

struct system_display {
	unsigned int adapter;	// DXGI dapter number
	RECT	dt_coord;		// desktop coordinates (windows convention)
};
void list_adapters(std::vector<system_display>& displays);

bool render_window::init_display(int& x, int& y, int& w, int& h, lf_shader_type shading, int idx) {
	std::vector<system_display> displays{};
	list_adapters(displays);
	shader_spec = shading;

	display = generic;
	if (idx < 0) idx = 0;
	if (idx >= displays.size()) {
		std::cout << "WARNING: Display " << idx << " not found. Trying display 0." << std::endl;
		idx = 0;
		if (displays.size() == 0) {
			std::cout << "ERROR: No displays detected." << std::endl;
			return false;
		}
	}
	x = displays[idx].dt_coord.left;
	y = displays[idx].dt_coord.top;
	w = displays[idx].dt_coord.right - displays[idx].dt_coord.left;
	h = displays[idx].dt_coord.bottom - displays[idx].dt_coord.top;
	std::cout << "Using " << w << "x" << h << " display " << idx << " at (" << x << " ," << y << " )." << std::endl;

	return true;
}

template <typename T> inline void reset_ptr(T** ppObj) {
    if (ppObj[0] != nullptr) {
        ppObj[0]->Release();
        ppObj[0] = nullptr;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	//PAINTSTRUCT ps;
	//HDC hdc;

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
	//case WM_PAINT:
	//	hdc = BeginPaint(hWnd, &ps);
	//	EndPaint(hWnd, &ps);
	//	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 'P':
			std::cout <<"Toggle processing\n";
			current_wnd->processing = current_wnd->processing == render_window::compute_type::none ? render_window::compute_type::filter : render_window::compute_type::none;
			break;
		default:
			break;
		}
	case WM_CHAR: //this is just for a program exit besides window's borders/taskbar
		if (wParam == VK_ESCAPE)
		{
			DestroyWindow(hWnd);
		}
		// process other keys here
		break;
	case WM_SIZE:
		//	// here is where you handle the swapchain resize for maximize or unmaximize
		break;

	//case WM_ENTERSIZEMOVE:
	//	// no swapchain resize in 'rubber band' resize
	//	break;

	//case WM_EXITSIZEMOVE:
	//	'rubber-band' finished - resize swapchain
	//	break;

	//case WM_GETMINMAXINFO:
	//{
	//	// If limiting window size is required
	//	auto info = reinterpret_cast<MINMAXINFO*>(lParam);
	//	info->ptMinTrackSize.x = 320;
	//	info->ptMinTrackSize.y = 200;
	//}
	//break;
	case WM_MOUSEWHEEL :
	{
	    // mouse scrolling shifts image
	      auto fwKeys = GET_KEYSTATE_WPARAM(wParam);
		  if (current_wnd!=nullptr) {
			  auto zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			  //auto xPos = GET_X_LPARAM(lParam);
			  //auto yPos = GET_Y_LPARAM(lParam);
			  if (fwKeys & MK_SHIFT) {
				  auto& qspec = current_wnd->get_img_spec();
				  qspec.centerXY.x += (zDelta / WHEEL_DELTA)*0.5f * 0.0078125f; // 0.5/128
				  qspec.centerXY.x = std::max(qspec.centerXY.x, 0.0f);
				  qspec.centerXY.x = std::min(qspec.centerXY.x, 1.0f);
			  }
		  }
	}
	break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);

	}
	return 0;
}

bool render_window::init(int x, int y, int w, int h, HINSTANCE hInstance) {
	LPCTSTR window_class_name = TEXT("LFRenderWindowClass");
	if (hInstance == NULL) {
		hInstance = GetModuleHandle(NULL);
	}
	// Register class
	WNDCLASSEX wcex;
	if (GetClassInfoEx(hInstance, window_class_name, &wcex) == FALSE) {
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
		wcex.lpszClassName = window_class_name;
		wcex.hIconSm = nullptr;
		if (!RegisterClassEx(&wcex))
			return false;
	}


	// Create window
	hwnd = CreateWindow(window_class_name, TEXT("Direct3D 11 LF Render Sample"),
		WS_POPUP,
		x, y, w, h, nullptr, nullptr, hInstance,
		this);
	if (hwnd==NULL)
		return false;
	x0 = x; y0 = y; width = w; height = h;

	ShowWindow(hwnd, SW_SHOW); //TODO: address autoresize to display size
	return true;
}

render_window::~render_window() {
	kill_renderer();
	if (test_tex != nullptr) test_tex->Release();
	kill_decoder();
	if (decoder_file) decoder_file.close();
	if (dpc) {
		delete dpc;
		dpc = nullptr;
	}
}

int render_window::run() {
	MSG msg = { 0 };
	while (WM_QUIT != msg.message)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			render();  // Use for rendering
		}
	}
	return 0;
}

void render_window::update_image_format(ID3D11Texture2D* tex) {
	D3D11_TEXTURE2D_DESC texd = {};
	tex->GetDesc(&texd);
	tex_format = texd.Format == DXGI_FORMAT_NV12 ? nv12 : other;
}

void render_window::render_texture(ID3D11Texture2D* tex) {
	if (dx11==nullptr || tex == nullptr) return;
	if (tex_format == dunno) update_image_format(tex);
	if (img_spec_changed)
		dx11->update_lf_parameters(img_spec);
	if (tex_format == nv12)
		dx11->render_texture_planar(tex);
	else
		dx11->render_texture(tex);
}

inline static size_t lin_size(DXGI_FORMAT fmt, unsigned int w, unsigned int h ) {
	size_t sz = 0;
	switch (fmt) {
	case DXGI_FORMAT_NV12:
		sz = size_t(w) * h * 3 / 2;
		break;
	default:
		sz = 0;
		std::cout << "ERROR: texture format not supported by compute kernel" << std::endl;
		break;
	}
	return sz;
}
// SYCL kernel computing edges with sobel and adding edge info to the image
void filter_img(sycl::queue& q, const unsigned char* src, unsigned char* dst, unsigned int w, unsigned int h, const float* kernel) {
	auto e = q.submit([&](auto& hd) {
		hd.parallel_for(sycl::range<2>(w, h),
			[=](item<2> it) {
				float rx = 0.0f;
				float ry = 0.0f;
				for (int ky = 0; ky < 3; ky++) {
					int y = max(int(it.get_id(1)) - 1 + ky, 0);
					y = min(y, int(h)); // repeat borders
					y *= w;
					for (int kx = 0; kx < 3; kx++) {
						int x = max(int(it.get_id(0)) - 1 + kx, 0);
						x = min(x, int(w));
						float pix = float(src[y + x])/255.0f;
						rx += pix * kernel[ky * 3 + kx];
						ry += pix * kernel[9 + ky * 3 + kx];
					}
				}
				float s = src[it.get_id(1) * w + it.get_id(0)];
				float r = sqrt(rx * rx + ry * ry)*255.0f;
				r = min(r + s, 255.0f); // add with saturation
				dst[it.get_id(1) * w + it.get_id(0)] = (unsigned char)r;
			}
		); // for
		}
	);
}
void render_window::process_frame(mfxFrameSurface1* frm, ID3D11Texture2D* dest) {
	auto frame_tex = VPLCodec::get_texture(frm);
	assert(frame_tex != nullptr);
	if (dpc == nullptr) { // check if dpc device exists
		ID3D11Device* dev = nullptr;
		frame_tex->GetDevice(&dev);
		dpc = new DPCPPCompute(dev);
		shared_img.init(1);
		vpl_frm.resize(1, nullptr);

		// prepare kernel buffer
		const std::vector<float> sobel_host({ -1.0f, 0.0f, 1.0f, -2.0f, 0.0f, 2.0f, -1.0f, 0.0f, 1.0f,
												-1.0f, -2.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f});
		sobel_kernel = sycl::malloc_device<float>(9 * 2, dpc->q);
		assert(sobel_kernel != nullptr);
		dpc->q.memcpy(sobel_kernel, sobel_host.data(), sobel_host.size() * sizeof(float)).wait();
	}

	D3D11_TEXTURE2D_DESC desc;
	frame_tex->GetDesc(&desc);
	size_t sz = lin_size(desc.Format, desc.Width, desc.Height);
	if (sz > 0 && sz > image_data_size) {
		if (image_data != nullptr) {
			sycl::free(image_data, dpc->q);
			sycl::free(processed_data, dpc->q);
		}
		image_data = nullptr;
		processed_data = nullptr;
		image_data = sycl::malloc_device<unsigned char>(sz, dpc->q);
		processed_data = sycl::malloc_device<unsigned char>(sz, dpc->q);
		image_data_size = (image_data != nullptr) ? sz:0;
	}
	if (sz == 0 || image_data_size == 0) {
		std::cout << "ERROR: zero data size for compute kernel." << std::endl;
		return;
	}
	// prepare for processing
	vpl_frm[0] = frm;
	shared_img.copy(vpl_frm, dpc, 0, image_data, image_data_size);

	// actual processing
	filter_img(dpc->q, image_data, processed_data, desc.Width, desc.Height, sobel_kernel); // process Y plane
	//dpc->q.memcpy(processed_data, image_data, desc.Width * desc.Height); // DEBUG: copy Y plane
	dpc->q.memcpy(processed_data + desc.Width*desc.Height, image_data + desc.Width * desc.Height, desc.Width/2 * desc.Height/2*2); // copy UV plane
	dpc->q.wait();

	if (dest == nullptr) 
		shared_img.copy_out(dpc, processed_data, frm);
	else
		dpc->linear_to_texture(processed_data, dest);
}

inline bool same_sz_fmt(const D3D11_TEXTURE2D_DESC& dst, D3D11_TEXTURE2D_DESC& src) {
	return dst.Format == src.Format && dst.Height == src.Height && dst.Width == src.Width &&
		dst.ArraySize == src.ArraySize && dst.MipLevels == src.MipLevels;
}
void modify_texture(ID3D11Texture2D** dst_tex, ID3D11Texture2D* as_tex) {
	if (dst_tex == nullptr || as_tex == nullptr) return;
	D3D11_TEXTURE2D_DESC sdesc{};
	as_tex->GetDesc(&sdesc);
	if (dst_tex[0] != nullptr) {
		D3D11_TEXTURE2D_DESC ddesc{};
		dst_tex[0]->GetDesc(&ddesc);
		if (same_sz_fmt(ddesc, sdesc)) return;
		dst_tex[0]->Release();
		dst_tex[0] = nullptr;
	}
	sdesc.Usage = D3D11_USAGE_DEFAULT;
	sdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // | D3D11_BIND_UNORDERED_ACCESS; // Important: overwrite decoder flags
	sdesc.CPUAccessFlags = 0;
	sdesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	ID3D11Device* dev = nullptr;
	as_tex->GetDevice(&dev);
	dev->CreateTexture2D(&sdesc, nullptr, dst_tex);
	if (dst_tex[0] == nullptr)
		std::cout << "ERROR: render texture creation failed." << std::endl;
}

void render_window::render() {
	// assume one per app
	static uint64_t framenum = 0;
	static auto tstart = std::chrono::high_resolution_clock::now();

 	if (decoder != nullptr) {
		mfxFrameSurface1* frm = nullptr;
		uint32_t bytesRead = 0;
		mfxStatus status;
		do {
			bytesRead = decoder->bs_in().read_from(decoder_file); // fill bitstream buffer if needed
			status = decoder->decode_frame(&frm, 1);
		} while (bytesRead && status == MFX_ERR_MORE_DATA);
		if (status != MFX_ERR_NONE) {
			decoder->rewind();
			decoder_file.clear();
			decoder_file.seekg(0, decoder_file.beg);
			do {
				bytesRead = decoder->bs_in().read_from(decoder_file); // fill bitstream buffer if needed
				status = decoder->decode_frame(&frm, 1);
			} while (bytesRead && status == MFX_ERR_MORE_DATA);
		}
		if (frm) {
			ID3D11Texture2D* frame_tex = VPLCodec::get_texture(frm);
			assert(frame_tex);
			if (processing != none) {
				modify_texture(&processed_tex, frame_tex);
				process_frame(frm, processed_tex);
				frame_tex = processed_tex;
			}
			// DEBUG
			//D3D11_TEXTURE2D_DESC dxtexd;
			//frame_quilt->GetDesc(&dxtexd);
			// END DEBUG
			if(dx11 == nullptr) init_renderer(frame_tex, hwnd, linear2srgb);
			assert(dx11 != nullptr);
			render_texture(frame_tex);
			frm->FrameInterface->Release(frm);
		}
	} else if (dx11 != nullptr && test_tex != nullptr)
		render_texture(test_tex);

	if(time_me){
		constexpr int mean_over = 30;
		static double mean_ms = 0.0;
		auto tend = std::chrono::high_resolution_clock::now();
		auto us = std::chrono::duration_cast<std::chrono::microseconds>(tend - tstart); //us
		tstart = tend;
		++framenum;
		mean_ms += double(us.count()) / 1000;
		std::cout << "FRAME: " << us.count() << "us;";
		if (framenum % mean_over == 0) {
			std::cout << "FRAME MEAN(" << mean_over << ") : " << mean_ms/mean_over << "ms";
			mean_ms = 0.0;
		}
		std::cout << "\r" << std::flush;
	}

}

bool render_window::init_decoder(int adapter_idx, const char* video_path) {
	kill_decoder();
	decoder = new VPLCodec(MFX_CODEC_HEVC); // use decoder only, no requirements for encoder
	// ATTN: dxgi adapter index is here
	int dxgi_adapter_idx = adapter_idx; // -1 - use oneVPL's default
	ID3D11Device* device = dxgi_adapter_idx >= 0? create_device(dxgi_adapter_idx):nullptr;
	decoder_file.open(video_path, std::ifstream::binary);
	if(!decoder_file) {
		std::cout << "ERROR: Cannot open video file: " << video_path << std::endl;
		kill_decoder();
		if (device != nullptr) {
			device->Release();
			device = nullptr;
		}
		return false;
	}
	if (!decoder->init_decoder(device)) {
		kill_decoder();
		return false;
	}
	tex_format = dunno;
	return true;
}

void render_window::kill_decoder() {

	if (decoder != nullptr) delete decoder;
	decoder = nullptr;
}

bool render_window::init_renderer(ID3D11Device* dx, HWND hWnd, bool lin2srgb) {
	kill_renderer();
	if (dx != nullptr && hWnd != NULL) {
		dx11 = new RenderDX11();
		dx11->init(dx, hWnd, shader_spec, lin2srgb);
	}
	if (!dx11->is_ready()) {
		kill_renderer();
		return false;
	}
	auto qspec = get_img_spec();
	qspec.render_width = width;
	qspec.render_height = height;
	linear2srgb = lin2srgb; // store srgb mode
	return true;
}

bool render_window::init_renderer(ID3D11Texture2D* get_dev_from, HWND hWnd, bool lin2srgb) {
	ID3D11Device* dx = nullptr;
	get_dev_from->GetDevice(&dx);
	auto res = init_renderer(dx, hWnd, lin2srgb);
	if(dx!=nullptr) dx->Release();
	return res;
}

bool render_window::init_renderer(int dxgi_adapter_idx, HWND hWnd, bool lin2srgb) {
	ID3D11Device* dx = RenderDX11::create_device(dxgi_adapter_idx);
	if (dx == nullptr) return false;
	auto res = init_renderer(dx, hWnd, lin2srgb);
	if (dx != nullptr) dx->Release();
	return res;
}

void render_window::kill_renderer() {
	if (dx11 != nullptr) delete dx11;
	dx11 = nullptr;
}

ID3D11Device* render_window::get_device() {
	if (dx11 != nullptr) return dx11->get_device();
	return nullptr;
}

void render_window::set_test_tex(ID3D11Texture2D* tex) {
	test_tex = tex;
	tex_format = dunno; // reset texture format and size
}

// utility function to list all adapters and connected displays
void list_displays() {
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
	LONG isError = ERROR_INSUFFICIENT_BUFFER;

	UINT32 pathCount, modeCount;
	isError = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

	if (isError)
	{
		return;
	}

	// Allocate the path and mode arrays
	paths.resize(pathCount);
	modes.resize(modeCount);

	// Get all active paths and their modes
	isError = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);

	// The function may have returned fewer paths/modes than estimated
	paths.resize(pathCount);
	modes.resize(modeCount);


	if (isError)
	{
		return;
	}

	// For each active path
	const int len = paths.size();
	for (int i = 0; i < len; i++)
	{
		// Find the target (monitor) friendly name
		DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
		targetName.header.adapterId = paths[i].targetInfo.adapterId;
		targetName.header.id = paths[i].targetInfo.id;
		targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		targetName.header.size = sizeof(targetName);
		isError = DisplayConfigGetDeviceInfo(&targetName.header);

		if (isError)
		{
			return;
		}
		std::wstring mon_name = L"Unknown";
		if (targetName.flags.friendlyNameFromEdid)
		{
			mon_name = targetName.monitorFriendlyDeviceName;
		}

		std::wcout << "Monitor: " << targetName.monitorDevicePath << L", " << targetName.monitorFriendlyDeviceName << std::endl;
	}
}

void list_adapters(std::vector<system_display>& displays) {
	std::cout << "Listing Gfx adapters:" << std::endl;
	IDXGIFactory1* pFactory;
	HRESULT hr;
	if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
		std::cout << "Creating DXGI Factory failed!" << std::endl;
		return;

	}

	UINT i = 0;	// adapters counter
	UINT d = 0; // displays counter
	displays.resize(0);

	IDXGIAdapter* pAdapter;
	DXGI_ADAPTER_DESC adapter;
	system_display disp;
	while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
	{
		pAdapter->GetDesc(&adapter);
		std::wcout << i << ": " << adapter.Description << std::endl;
		std::cout << "  video mem: " << adapter.DedicatedVideoMemory / 1024 / 1024 << "MB; ";
		std::cout << "shared sys mem: " << adapter.SharedSystemMemory / 1024 / 1024 << "MB; ";
		std::cout << "sys mem: " << adapter.DedicatedSystemMemory / 1024 / 1024 << "MB" << std::endl;

		// enumerate outputs and find connected displays
		UINT out = 0;
		IDXGIOutput* pOutput;
		while (pAdapter->EnumOutputs(out, &pOutput) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_OUTPUT_DESC output = {};
			pOutput->GetDesc(&output);

			//MONITORINFOEX monitor = {};
			//monitor.cbSize = sizeof(MONITORINFOEX);
			//GetMonitorInfo(output.Monitor, &monitor);
			std::cout << "Display " << d << "::";
			std::wcout << "  out " << out << ": " << output.DeviceName << "; ";
			//std::cout << "monitor: " << monitor.szDevice << "; ";
			std::cout << (output.AttachedToDesktop ? "DT " : "Non-DT ") << "coord: ";
			std::cout << "(" << output.DesktopCoordinates.left << ", " << output.DesktopCoordinates.top << "); ";
			std::cout << "dim: " << "(" << output.DesktopCoordinates.right - output.DesktopCoordinates.left << ", ";
			std::cout << output.DesktopCoordinates.bottom - output.DesktopCoordinates.top << "); " << std::endl;
			disp.adapter = i;
			disp.dt_coord = output.DesktopCoordinates;
			displays.push_back(disp);
			++out;
			++d;
			pOutput->Release();
		}
		++i;
		pAdapter->Release();

	}
	pFactory->Release();
	std::cout << "Listing system display names (different indexing):" << std::endl;
	list_displays();	// different numbering in theory
}

void list_adapters() {
	std::vector<system_display> dummy{};
	list_adapters(dummy);
}

