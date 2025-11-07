//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifdef WIN32
#pragma warning(disable : 4464 4820 4514 5045 4201 5039 4061 4710 4458 4626 5027 4365 4312)
#endif

#include "render_window.hpp"
#include "opencv2\opencv.hpp"
#include <assert.h>

using namespace std;

#define UNUSED(x) [&x]{}()

ID3D11Texture2D* create_texture(ID3D11Device* device, cv::Mat& src);

bool is_video(std::string& fname) {
	if (fname.length() < 5) return false;
	return fname.substr(fname.length() - 5, std::string::npos) == ".hevc";
}

// disable auto-scaling for high-dpi monitors
bool SetDPIAwareness() {
	auto res = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
	if (!res) {
		auto err = GetLastError();
		std::cout << "WARNING: Set DPI awareness failed." << std::endl;
		switch (err) {
		case ERROR_ACCESS_DENIED:
			std::cout << "Mode is set elsewhere. Check manifest/exe properties." << std::endl;
			break;
		case ERROR_INVALID_PARAMETER:
			std::cout << "Mode is not supported." << std::endl;
			break;
		default:
			std::cout << "Unknown error. Something is totally wrong." << std::endl;
			break;
		}
	}
	return res;
}

void get_int(int& dst, const char* s) {
	std::istringstream iss(s);
	iss >> dst;
	if (iss.fail()) std::cout << "Error: expected int, got " << s << std::endl;
}

void get_rc(vec2<int>& rc, const char* s) {
	std::istringstream iss(s);
	iss >> rc.y;
	if (iss.fail()) goto error;
	char xc;
	iss >> xc;
	if (iss.fail() || xc != 'x') goto error;
	iss >> rc.x;
	if (iss.fail()) goto error;
	return;
error:
	std::cout << "Error: expected NxM, got " << s << std::endl;
}

struct app_parameters {
	std::string input_file = "..\\..\\content\\7200x8100_10x5_Orange.jpg";
	bool linear_source = false;
	int display_idx = -1;
	lf_shader_type shader = mono;
};

bool parse_args(app_parameters& p, int argc, const char* argv[]) {
	int ia = 1;
	std::string cmd, shd;
	while (ia < argc) {
		cmd = argv[ia++];
		if (cmd == "--i" && ia < argc) p.input_file = argv[ia++];
		else if (cmd == "--l") p.linear_source = true;
		else if (cmd == "--d" && ia < argc) get_int(p.display_idx, argv[ia++]);
		else if (cmd == "--s" && ia < argc) {
			shd = argv[ia++];
			const std::string shd_cmd[shader_types_total] = { "mono" };
			int i;
			for (i = 0; i < shader_types_total; i++) {
				if (shd != shd_cmd[i]) continue;
				p.shader = static_cast<lf_shader_type>(i);
				break;
			}
			if (i < shader_types_total) continue;
			std::cout << "Error: unknown shader " << shd << std::endl;
			goto error;
		} else {
			std::cout << "Error: unknown option " << cmd << std::endl;
			goto error;
		}
	}
	return true;
error:
	return false;
}

void help() {
	std::cout << "Usage: [--i <filepath>] [--quilt_rc <q_r>x<q_c>] [--s multiview|mono|stereo] [--d <display_num>]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "--i <filepath>	- input video/image, default: ..\\..\\content\\7200x8100_10x5_Orange.jpg" << std::endl;
	std::cout << "--s mono(default) - type of shader" << std::endl;
	std::cout << "--d <display_num>	- display number to run on or -1 - autodetect, default is -1." << std::endl;
}

int main(int argc, const char *argv[])
{
	SetDPIAwareness();
	
	// parameters
	app_parameters params{};

	// parse arguments
	if (!parse_args(params, argc, argv)) {
		help();
		exit(-1);
	}

	render_window	main_window;
	int x0, y0, w, h;
	if (!main_window.init_display(x0, y0, w, h, params.shader, params.display_idx)) {
		std::cout << "Display initialization failed." << std::endl;
		exit(-1);
	}

	main_window.init(x0, y0, w, h);
	main_window.linear2srgb_default() = params.linear_source; // set conversion mode before init_renderer

	bool success = false;
	if (is_video(params.input_file)) {
		success = main_window.init_decoder(0, params.input_file.c_str());
		// renderer will be created from the first decoded texture
		if (!success) std::cout << "Create decoder failed. Fallback to test image." << std::endl;
	}
	if (!success) { // try image mode
		cv::Mat image = cv::imread(params.input_file, cv::IMREAD_UNCHANGED);
		if (image.empty()) {
			std::cout << "Could not read the image: " << params.input_file << std::endl;
			return -1;
		}
		cv::cvtColor(image, image, cv::COLOR_BGR2RGBA);

		success = main_window.init_renderer(0, main_window.get_hwnd(), params.linear_source);
		if (!success) {
			std::cout << "Create renderer failed. Terminating." << std::endl;
			return -1;
		}
		auto tex = create_texture(main_window.get_device(), image);
		main_window.set_test_tex(tex);
	}
	
	main_window.run();

  return 0;
}

ID3D11Texture2D* create_texture(ID3D11Device* device, cv::Mat& src) {
	assert(src.elemSize() == 4); // expecting converted to rgba
	if (!src.isContinuous()) {
		std::cout << "Failed to create a texture from non-continuous cv::Mat." << std::endl;
		return nullptr;
	}
	HRESULT hr;

	ID3D11Texture2D* text = nullptr;
	DXGI_FORMAT new_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = src.cols;
	desc.Height = src.rows;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = new_format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0; // D3D11_CPU_ACCESS_WRITE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA	initialData = {};
	initialData.SysMemPitch = src.cols * src.elemSize();
	initialData.SysMemSlicePitch = src.rows*src.cols * src.elemSize();
	initialData.pSysMem = src.data;

	hr = device->CreateTexture2D(&desc, &initialData, &text);
	if (FAILED(hr))
		return nullptr;

	//ID3D11DeviceContext* ctx;
	//device->GetImmediateContext(&ctx);

	//D3D11_MAPPED_SUBRESOURCE mappedResource;
	//ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));

	//// Lock the texture to allow writing.
	//hr = ctx->Map(text, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	//if (FAILED(hr)) {
	//	// Handle error
	//	return 0;
	//}

	//// Get the pointer to the texture's buffer
	//unsigned char* texBuffer = reinterpret_cast<unsigned char*>(mappedResource.pData);

	//// Copy the data from the vector buffer to the texture
	//assert(mappedResource.DepthPitch >= src.rows * src.cols * src.elemSize());
	//if (mappedResource.RowPitch == src.cols * src.elemSize()) {
	//	memcpy(texBuffer, src.data, src.cols * src.rows * src.elemSize());
	//}
	//else
	//{
	//	unsigned char* rowBuffer = texBuffer;
	//	for (int r = 0; r < src.rows; r++, rowBuffer += mappedResource.RowPitch) {
	//		memcpy(rowBuffer, src.ptr<unsigned char>(r), src.cols * src.elemSize());
	//	}
	//}

	//// Unlock the texture
	//ctx->Unmap(text, 0);
	return text;
}
