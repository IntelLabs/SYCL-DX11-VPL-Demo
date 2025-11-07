//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================
#ifndef SHARED_VPL_L0_HPP
#define SHARED_VPL_L0_HPP
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "dpcpp_compute.hpp"
#include "shared_resource.hpp"
#include "vpl/mfxstructures.h"
#include <vector>

// casting vpl to DX11 or nullptr if non-castable
ID3D11Texture2D* get_texture(mfxFrameSurface1* surface);

// all data/methods necessary to share mfxFrameSurface1 texture
// with L0 image and/or copy data to DPC++ pointers on CPU or GPU
class shared_image_batch_t {
public:
	shared_image_batch_t() {
	}
	shared_image_batch_t(unsigned int num);

	void init(unsigned int num);

	void reset();
	
	// copy vpl frames to shareable textures and 
	// optionally share with l0 if dpc is not nullptr and
	// optionally copy images to dpc_data (if not nullptr)
	// copy/linearize planar textures as is one-by-one ([Yi, UVi]), same as source_Y_UV mode below
	// using dpc_data_size as a guardband
	// returnes number of images actually copied, shared or fully copied to dpc++
	// start is an offset into the batch (can be used for multi-buffering/pipelining) 
	unsigned int copy(std::vector<mfxFrameSurface1*>& vpl, DPCPPCompute* dpc = nullptr, unsigned int start = 0, void* dpc_data = nullptr, size_t dpc_data_size = 0);

	// configurable copy of vpl frames to shareable textures and 
	// optionally share with l0 if dpc is not nullptr and
	// optionally copy images to dpc_data (if not nullptr)
	// using dpc_data_size as a guardband
	// mode[in]
	//	source_Y_UV - copy/linearize planar textures as is ([Yi, UVi])
	//	split_Y_U_V - copy/linearize planar textures splitting all Ys, all Us, all Vs ([Yi], [Ui], [Vi])
	//	split_Y - copy/linearize planar textures extracting Ys only ([Yi])
	// returnes number of images actually copied, shared or fully copied to dpc++
	// start is an offset into the batch (can be used for multi-buffering/pipelining) 

	unsigned int copy(std::vector<mfxFrameSurface1*>& vpl, shared_copy_mode_t mode,
		DPCPPCompute* dpc, unsigned int start, void* dpc_data, size_t dpc_data_size);

	unsigned int crop_scale(std::vector<mfxFrameSurface1*>& vpl,
		const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
		DPCPPCompute* dpc = nullptr,
		unsigned int start = 0, 
		void* dpc_data = nullptr, size_t dpc_data_size = 0);

	unsigned int crop_scale(std::vector<mfxFrameSurface1*>& vpl, shared_copy_mode_t mode,
		const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
		DPCPPCompute* dpc = nullptr,
		unsigned int start = 0,
		void* dpc_data = nullptr, size_t dpc_data_size = 0);

	// share cpy_num internal textures (starting at start) with l0 and
	// optionally copy images to dpc_data (if not nullptr)
	// using dpc_data_size as a guardband
	// returns number of images actually copied, shared or fully copied to dpc++
	// start is an offset into the batch (can be used for multi-buffering/pipelining) 
	unsigned int copy(unsigned int cpy_num, DPCPPCompute* dpc, unsigned int start = 0, void* dpc_data = nullptr, size_t dpc_data_size = 0);
	unsigned int copy(unsigned int cpy_num, shared_copy_mode_t mode, DPCPPCompute* dpc, unsigned int start = 0, void* dpc_data = nullptr, size_t dpc_data_size = 0);

	// copy linear buffer to *entire* surface regardless of CropX/Y/Width/Height values
	static void copy_out(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl);
	// copy linear buffer to surface roi
	static void copy_out(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl, const shared_rect& dst_roi);
	// copy linear buffer to surface roi defined by CropX/Y/Width/Height values
	static void copy_out_crop(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl);

	shared_input_batch_t dx11;
	l0_image_t*				l0=nullptr;

	virtual ~shared_image_batch_t();
protected:
	std::vector<ID3D11Texture2D*> tmp_tex_buffer;
};


// class for direct copy of vpl images
// to the buffer with linear versions
// mainly to minimize new/delete for 
// the bookkeeping info
class passthrough_image_batch_t {
public:
	passthrough_image_batch_t() {
	}
	passthrough_image_batch_t(unsigned int num) {
		init(num);
	}
	void init(unsigned int num) {
		shared_res.resize(num);
		shared_img.resize(num);
	}

	void reset();

	// detile&copy vpl frames contents to to dpc_data (if not nullptr)
	// using dpc_data_size as a guard 
	// returns number of images fully copied to dpc++
	// start is an offset into the batch (can be used for multi-buffering/pipelining) 
	unsigned int copy(std::vector<mfxFrameSurface1*>& vpl, DPCPPCompute* dpc, void* dpc_data = nullptr, size_t dpc_data_size = 0);

	std::vector<shared_resource_t>	shared_res;
	std::vector<l0_image_t>			shared_img;
protected:
	void reset(unsigned int start, unsigned int num);
};
#endif //SHARED_VPL_L0_HPP

