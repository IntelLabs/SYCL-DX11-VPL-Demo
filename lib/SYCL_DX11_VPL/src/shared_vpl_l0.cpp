//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================

#include "shared_vpl_l0.hpp"


template <typename T> inline void reset_ptr(T** ppObj) {
    if (ppObj[0] != nullptr) {
        ppObj[0]->Release();
        ppObj[0] = nullptr;
    }
}

shared_image_batch_t::shared_image_batch_t(unsigned int num) {
	init(num);
}

void shared_image_batch_t::init(unsigned int num) {
	reset();
	dx11.init(num);
	l0 = new l0_image_t[num];
	tmp_tex_buffer.resize(num, nullptr);
}

void shared_image_batch_t::reset() {
	unsigned int num = dx11.size();
	if (l0 != nullptr) {
		for (unsigned int i=0; i < num; i++)
			l0[i].reset();
		delete[] l0;
		l0 = nullptr;
	}
	tmp_tex_buffer.resize(0);
	dx11.reset();
}

shared_image_batch_t::~shared_image_batch_t() {
	reset();
}

// casting vpl to DX11 or nullptr if non-castable
ID3D11Texture2D* get_texture(mfxFrameSurface1* surface) {
	mfxHDL resource;
	mfxResourceType resource_type;
	mfxStatus s = surface->FrameInterface->GetNativeHandle(surface, &resource, &resource_type);
	if (MFX_ERR_NONE == s && resource_type == MFX_RESOURCE_DX11_TEXTURE)
		return (ID3D11Texture2D*)resource;
	std::cout << "Error: can't get DX11 texture from video surface!" << std::endl;
	return nullptr;
}

void shared_image_batch_t::copy_out(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl) {
	ID3D11Texture2D* tex = get_texture(dst_vpl);
	assert(tex != nullptr);
	assert(dpc != nullptr);
	shared_resource_t resource(tex);
	l0_image_t l0_image;
	dpc->shared_to_l0(resource, l0_image);
	dpc->linear_to_image_l0(src_dpc_data, l0_image);
}

void shared_image_batch_t::copy_out(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl, const shared_rect& dst_roi) {
	ID3D11Texture2D* tex = get_texture(dst_vpl);
	assert(tex != nullptr);
	assert(dpc != nullptr);
	shared_resource_t resource(tex);
	l0_image_t l0_image;
	dpc->shared_to_l0(resource, l0_image);
	dpc->linear_to_image_l0(src_dpc_data, l0_image, dst_roi);
}

void shared_image_batch_t::copy_out_crop(DPCPPCompute* dpc, void* src_dpc_data, mfxFrameSurface1* dst_vpl) {
	ID3D11Texture2D* tex = get_texture(dst_vpl);
	assert(tex != nullptr);
	assert(dpc != nullptr);
	shared_resource_t resource(tex);
	l0_image_t l0_image;
	dpc->shared_to_l0(resource, l0_image);
	shared_rect dst_roi = { dst_vpl->Info.CropX,dst_vpl->Info.CropY,dst_vpl->Info.CropW, dst_vpl->Info.CropH };
	dpc->linear_to_image_l0(src_dpc_data, l0_image, dst_roi);
}

// plain copy
unsigned int shared_image_batch_t::copy(std::vector<mfxFrameSurface1*>& vpl, DPCPPCompute* dpc, unsigned int start, void* dpc_data, size_t dpc_data_size) {
	if (dx11.size() < start + vpl.size()) init(start + vpl.size()); // ATTN: old content is destroyed
	unsigned int num_cpy = std::min((unsigned int)vpl.size(), dx11.size() - start);
	if (num_cpy == 0) return 0; // nothing to do
	assert(tmp_tex_buffer.size() == dx11.size());

	// extract texture pointers from vpl
	for (unsigned int i = 0; i < num_cpy; i++) {
		ID3D11Texture2D* tex = get_texture(vpl[i]);
		assert(tex != nullptr);
		tmp_tex_buffer[start + i] = tex;
	}
	unsigned int dx11_cpy = dx11.copy(&(tmp_tex_buffer.data()[start]), num_cpy, start);
	assert(num_cpy == dx11_cpy);
	if (dpc == nullptr) return dx11_cpy;

	return copy(dx11_cpy, dpc, start, dpc_data, dpc_data_size);
}

// configurable copy
unsigned int shared_image_batch_t::copy(std::vector<mfxFrameSurface1*>& vpl, shared_copy_mode_t mode,
	DPCPPCompute* dpc, unsigned int start, void* dpc_data, size_t dpc_data_size) {

	if (mode == source_Y_UV) return copy(vpl, dpc, start, dpc_data, dpc_data_size);
	
	if (dx11.size() < start+vpl.size()) init(start+vpl.size()); // ATTN: old content is destroyed
	unsigned int num_cpy = std::min((unsigned int)vpl.size(), dx11.size() - start);
	if (num_cpy == 0) return 0; // nothing to do
	assert(tmp_tex_buffer.size() == dx11.size());
	
	// extract texture pointers from vpl
	for (unsigned int i = 0; i < num_cpy; i++) {
		ID3D11Texture2D* tex = get_texture(vpl[i]);
		assert(tex != nullptr);
		tmp_tex_buffer[start + i] = tex;
	}
	unsigned int dx11_cpy = dx11.copy_Y_UV(&(tmp_tex_buffer.data()[start]), num_cpy, mode, start);
	assert(num_cpy == dx11_cpy);
	if (dpc == nullptr) return dx11_cpy;

	return copy(dx11_cpy, mode, dpc, start, dpc_data, dpc_data_size);
}

unsigned int shared_image_batch_t::copy(unsigned int num_cpy, DPCPPCompute* dpc, unsigned int start, void* dpc_data, size_t dpc_data_size) {
	if (dpc == nullptr) return 0;
	num_cpy = min(num_cpy, dx11.size() - start);
	if (num_cpy == 0) return 0;

	size_t bytes_left = dpc_data_size;
	unsigned int fit_cpy = 0;
	// dpc is non-zero share into l0 images
	for (unsigned int i = 0; i < num_cpy; i++) {
		if (l0[start + i].empty()) {
			shared_resource_t* resource = dx11[start + i];
			assert(resource != nullptr);
			dpc->shared_to_l0(*resource, l0[start + i]);
		}
		size_t size = l0[start + i].size();
		if (size <= bytes_left && bytes_left > 0) {
			fit_cpy++;
			bytes_left -= size;
		} else {
			bytes_left = 0; // stop counting
		}
	}

	if (dpc_data == nullptr) return num_cpy; // sharing only, no copy

	// run image copy
	dpc->image_to_linear_l0(&l0[start], fit_cpy, dpc_data);
	return fit_cpy;
}

unsigned int shared_image_batch_t::copy(unsigned int num_cpy, shared_copy_mode_t mode, DPCPPCompute* dpc, unsigned int start, void* dpc_data, size_t dpc_data_size) {
	if (dpc == nullptr) return 0;
	num_cpy = min(num_cpy, dx11.size() - start);
	if (num_cpy == 0) return 0;

	size_t bytes_left = dpc_data_size;
	unsigned int fit_cpy = 0;
	// dpc is non-zero share into l0 images
	for (unsigned int i = 0; i < num_cpy; i++) {
		if (l0[start + i].empty()) {
			shared_resource_t* resource = dx11[start + i];
			assert(resource != nullptr);
			dpc->shared_to_l0(*resource, l0[start + i]);
		}
		size_t size = l0[start + i].size();
		if (size <= bytes_left && bytes_left > 0) {
			fit_cpy++;
			bytes_left -= size;
		}
		else {
			bytes_left = 0; // stop counting
		}
	}

	if (dpc_data == nullptr) return num_cpy; // sharing only, no copy

	// run image copy
	if(mode==split_Y_U_V) dpc->image_Y_UV_to_linear_l0(&l0[start], fit_cpy, dpc_data);
	else dpc->image_to_linear_l0(&l0[start], fit_cpy, dpc_data);

	return fit_cpy;
}

// regular crop-in
unsigned int shared_image_batch_t::crop_scale(std::vector<mfxFrameSurface1*>& vpl,
	const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
	DPCPPCompute* dpc,
	unsigned int start,
	void* dpc_data, size_t dpc_data_size) {
	if (dx11.size() < start+vpl.size()) init(start+vpl.size()); // ATTN: old content is destroyed
	unsigned int num_cpy = std::min((unsigned int)vpl.size(), dx11.size() - start);
	if (num_cpy == 0) return 0; // nothing to do
	assert(tmp_tex_buffer.size() == dx11.size());

	// extract texture pointers from vpl
	for (unsigned int i = 0; i < num_cpy; i++) {
		ID3D11Texture2D* tex = get_texture(vpl[i]);
		assert(tex != nullptr);
		tmp_tex_buffer[start + i] = tex;
	}
	unsigned int dx11_cpy = dx11.crop_scale(&(tmp_tex_buffer.data()[start]), num_cpy, crop, dst_w, dst_h, start);
	assert(num_cpy == dx11_cpy);
	if (dpc == nullptr) return dx11_cpy;

	return copy(dx11_cpy, dpc, start, dpc_data, dpc_data_size);

}

// configurable crop-in
unsigned int shared_image_batch_t::crop_scale(std::vector<mfxFrameSurface1*>& vpl, shared_copy_mode_t mode,
	const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
	DPCPPCompute* dpc,
	unsigned int start,
	void* dpc_data, size_t dpc_data_size) {

	if (mode == source_Y_UV) return crop_scale(vpl, crop, dst_w, dst_h, dpc, start, dpc_data, dpc_data_size);

	if (dx11.size() < start + vpl.size()) init(start + vpl.size()); // ATTN: old content is destroyed
	unsigned int num_cpy = std::min((unsigned int)vpl.size(), dx11.size() - start);
	if (num_cpy == 0) return 0; // nothing to do
	assert(tmp_tex_buffer.size() == dx11.size());

	// extract texture pointers from vpl
	for (unsigned int i = 0; i < num_cpy; i++) {
		ID3D11Texture2D* tex = get_texture(vpl[i]);
		assert(tex != nullptr);
		tmp_tex_buffer[start + i] = tex;
	}
	unsigned int dx11_cpy = dx11.crop_scale_Y_UV(&(tmp_tex_buffer.data()[start]), num_cpy, crop, dst_w, dst_h, mode, start);
	assert(num_cpy == dx11_cpy);
	if (dpc == nullptr) return dx11_cpy;

	return copy(dx11_cpy, mode, dpc, start, dpc_data, dpc_data_size);
}

void passthrough_image_batch_t::reset() {
	for (int i = 0; i < shared_img.size(); i++) {
		shared_img[i].reset();
	}
	for (int i = 0; i < shared_res.size(); i++) {
		shared_res[i].reset();
	}
}

void passthrough_image_batch_t::reset(unsigned int start, unsigned int num) {
	unsigned int end = std::min(start + num, (unsigned int)shared_img.size());
	for (unsigned int i = start; i < end; i++) {
		shared_img[i].reset();
	}
	end = std::min(start + num, (unsigned int)shared_res.size());
	for (int i = start; i < end; i++) {
		shared_res[i].reset();
	}
}
// detile&copy vpl frames contents to to dpc_data (if not nullptr)
// using dpc_data_size as a guard 
// returns number of images fully copied to dpc++
// start is an offset into the batch (can be used for multi-buffering/pipelining) 
unsigned int passthrough_image_batch_t::copy(std::vector<mfxFrameSurface1*>& vpl, DPCPPCompute* dpc, void* dpc_data, size_t dpc_data_size) {
	assert(dpc!=nullptr);
	// for now assume everything is reset, we don't keep shared structures
	if(shared_res.size() < vpl.size()) shared_res.resize(vpl.size());
	if(shared_img.size() < vpl.size()) shared_img.resize(vpl.size());
	unsigned int processed = 0;
	size_t bytes_left = dpc_data_size;
	//share with l0
	for (int i = 0; i < vpl.size(); i++) {
		assert(vpl[i] != nullptr);
		ID3D11Texture2D* tex = get_texture(vpl[i]);
		assert(tex != nullptr);
		shared_res[i].init(tex);
		dpc->shared_to_l0(shared_res[i], shared_img[i]);

		size_t size = shared_img[i].size();
		if (size <= bytes_left && bytes_left > 0) {
			processed++;
			bytes_left -= size;
		}
		else {
			bytes_left = 0; // stop counting
		}

	}
	// now we are copying images (synchronously)
	dpc->image_to_linear_l0(shared_img.data(), processed, dpc_data);
	// copy is done, can free up resources
	reset(0, vpl.size());
	return processed;
}
