//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================
#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <iostream>
#include <fstream>
#include <windows.h>

#include "lf_basics.hpp"

#include "shared_vpl_l0.hpp"

void list_adapters(); // utility function to display all adapters&monitors

class VPLCodec;
class RenderDX11;
class DPCPPCompute;

class render_window {
public:
	static const bool time_me = true;
	render_window() {}
	render_window(int x, int y, int w, int h, HINSTANCE hInstance = NULL) {
		init(x, y, w, h, hInstance);
	}

	bool init(int x = 0, int y = 0, int w = 7680, int h = 4320, HINSTANCE hInstance=NULL);

	// add new display types here
	enum display_type {
		generic = 0
	};

	enum compute_type {
		none = 0,
		filter = 1,
	};
	// finding required display coordinates
	// implement your own search algo
	virtual bool init_display(int& x, int& y, int& w, int& h, lf_shader_type shading=mono, int idx = -1);

	// message loop (calls render)
	virtual int run();
	//virtual void update(); // use this for gaming and alike apps
	virtual void render();

	bool init_decoder(int adapter_idx, const char* video_path);
	void kill_decoder();

	// init_renderer overrides default linear2srgb mode with lin2srgb
	bool init_renderer(ID3D11Device* dx, HWND hWnd, bool lin2srgb = false); // init only when window exists
	bool init_renderer(ID3D11Texture2D* get_dev_from, HWND hWnd, bool lin2srgb = false);
	bool init_renderer(int dxgi_adapter_idx, HWND hWnd, bool lin2srgb = false); // init only when window exists
	void kill_renderer();

	virtual ~render_window();

	HWND get_hwnd() { return hwnd; }
	ID3D11Device* get_device();
	void set_test_tex(ID3D11Texture2D* tex); // no ref added, caller passes ownership

	lf_parameters_render& get_img_spec() {// shader-specific quilt paramters
		img_spec_changed = true; // expecting parameters update
		return img_spec;
	}
	const lf_parameters_render& get_img_spec() const {// shader-specific quilt paramters
		return img_spec;
	}

	// accesses default sRGB mode for future automatic renderer inits
	bool& linear2srgb_default() { return linear2srgb;}
	const bool& linear2srgb_default() const { return linear2srgb; }

	// use compute
	compute_type processing = none;

protected: 
	// window characteristics
	HWND	hwnd=NULL; // window handle
	int		x0=0;
	int		y0=0;
	int		width=0;
	int		height=0;
	display_type	display = generic;
	
	bool	linear2srgb = false; // passed to automatic renderer init internally, no effect on current state

	// decoder & renderer
	VPLCodec* decoder = nullptr;
	RenderDX11* dx11 = nullptr;
	DPCPPCompute* dpc = nullptr;

	//vpl-dx11-sycl image sharing structure
	shared_image_batch_t	shared_img;	// info on a batch of shared vpl frames (w/underlying dx textures)
	std::vector<mfxFrameSurface1*> vpl_frm; // vpl frames vector to pass to batch sharing
	unsigned char* image_data = nullptr; // gpu ptr to stc image data for processing
	unsigned char* processed_data = nullptr; // gpu ptr to processed image data
	size_t image_data_size = 0;
	float* sobel_kernel = nullptr;
	// sycl processing kernel, results are written to dest, or back to frm if dest=nullptr
	void process_frame(mfxFrameSurface1* frm, ID3D11Texture2D* dest=nullptr); 
	ID3D11Texture2D* processed_tex = nullptr; // this will be the dest for above


	// some state variables
	enum image_format {
		dunno = 0, nv12 = 1, other = 2
	};
	image_format tex_format = dunno; // currently detected quilt format
	lf_shader_type			shader_spec = mono;
	lf_parameters_render	img_spec = {};	// shader-specific parameters
	bool					img_spec_changed = false;	// need to update shader's cbuffer(s)?

	void update_image_format(ID3D11Texture2D* t); // check if texture size/fmt chnaged and update state

	void render_texture(ID3D11Texture2D* tex); // send texture for rendering parsing state variables

	// test texture when decoder stream is not available
	ID3D11Texture2D* test_tex=nullptr;

	// decoder input stream
	std::ifstream decoder_file = {};
};