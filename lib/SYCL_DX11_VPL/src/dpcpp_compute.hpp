//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
#ifndef DPCPP_COMPUTE_HPP
#define DPCPP_COMPUTE_HPP
#ifndef NOMINMAX
#define NOMINMAX
#endif

/////////////////////////////////////////////////////
// Level-zero GPU device exposing SYCL structures
// facilitating interop between level-zero and SYCL
// Level-zero functionality is used to import/export
// DX11 resources and perform de-tiling/tiling if
// linear format is used for SYCL processing
/////////////////////////////////////////////////////

#include <algorithm>
#include <sycl/sycl.hpp>

#include "level_zero/ze_api.h"
#include "sycl/ext/oneapi/backend/level_zero.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>




///
/// DPCPP L0 compute device example
///
using namespace sycl;


struct shared_resource_t;
struct shared_rect;

struct dev_id {
	dev_id(int p=-1, int d=-1) : p(p), d(d) {}
	int p;
	int d;
};


// ideally need class
auto l0_arc_gpu_selector = [](const sycl::device& d)
{
	std::string name = "Intel(R) Arc(TM)";
	if (d.is_gpu() && 
		d.get_backend()== sycl::backend::ext_oneapi_level_zero &&
		d.get_info<info::device::name>().find(name) != std::string::npos )
	{
		return 1;
	}
	else
	{
		return -1;
	}
};

struct l0_gpu_selector {
	l0_gpu_selector(const std::string name = "Intel(R) Arc(TM)", const int dev_idx = 0)
	: name(name), dev_idx(dev_idx), found(-1) {
	}

	auto operator()(const sycl::device& d) {
		if (d.is_gpu() &&
			d.get_backend() == sycl::backend::ext_oneapi_level_zero &&
			d.get_info<info::device::name>().find(name) != std::string::npos &&
			++found==dev_idx)
		{
			return 1;
		}
		else
		{
			return -1;
		}

	}
private:
	std::string name;
	const int dev_idx;
	int found;
};

// just a bookkeeping of byte size
struct l0_allocation_t {
	void*	data = nullptr;
	size_t	byte_size;
	//TODO: do we need to destroy shared allocations?
};

struct l0_image_t {
	ze_image_handle_t	image=nullptr;
	ze_image_format_layout_t	format;
	size_t	width;
	size_t	height;
	HANDLE	shared_handle = nullptr; // shared resource handle 
	// linear layout helpers (internal representation can vary)
	unsigned int planes() const {
		switch (format) {
		case ZE_IMAGE_FORMAT_LAYOUT_NV12:
		case ZE_IMAGE_FORMAT_LAYOUT_P010:
		case ZE_IMAGE_FORMAT_LAYOUT_P016:
			return 2;
		default:
			return 1;
		}
		assert(0);
		return 0;
	}

	unsigned int planes(unsigned int& c, unsigned int& c_sz) const {
		c = 0;
		c_sz = 1; // channel size in bytes
		switch (format) {
		case ZE_IMAGE_FORMAT_LAYOUT_P010:
		case ZE_IMAGE_FORMAT_LAYOUT_P016:
			c_sz = 2;
		case ZE_IMAGE_FORMAT_LAYOUT_NV12:
			c = 3;
			return 2;
		case ZE_IMAGE_FORMAT_LAYOUT_8_8_8_8:
			c += 2;
		case ZE_IMAGE_FORMAT_LAYOUT_8_8:
			c += 1;
		case ZE_IMAGE_FORMAT_LAYOUT_8:
			c += 1;
			return 1;
		case ZE_IMAGE_FORMAT_LAYOUT_16_16:
			c += 1;
		case ZE_IMAGE_FORMAT_LAYOUT_16:
			c += 1;
			c_sz = 2;
			return 1;
		default: // 
			assert(0); // unsupported format
			return 0;
		}
		assert(0);
		return 0;
	}

	size_t plane_size(unsigned int pln) {
		unsigned int c = 0; // number of channels
		unsigned int c_sz = 0;
		unsigned int p_cnt = planes(c, c_sz); // planes and ch size in bytes
		if(p_cnt == 1 && pln == 0) // non planar format plane 0
			return width * height*c_sz * c;
		if (p_cnt == 2 && pln < 2)
			return (width* height*c_sz) >> pln; // w*h/2 for UV
		return 0; // invalid args
	}

	// number of bytes in *image* roi region in plane pln
	// roi_w, roi_h - region of interest of *image* in pixels (not roi of the plane!)
	// for planar formats roi_w, roi_h must be even numbers
	size_t plane_size(unsigned int pln, size_t roi_w, size_t roi_h) {
		unsigned int c = 0; // number of channels
		unsigned int c_sz = 0;
		unsigned int p_cnt = planes(c, c_sz); // planes and ch size in bytes
		if (p_cnt == 1 && pln == 0) // non planar format plane 0
			return roi_w * roi_h * c_sz * c;
		if (p_cnt == 2 && pln < 2)
			return (roi_w * roi_h * c_sz) >> pln; // w*h/2 for UV
		return 0; // invalid args
	}

	size_t size() {
		unsigned int c = 0; // number of channels
		unsigned int c_sz = 0;
		unsigned int p_cnt = planes(c, c_sz); // planes and ch size in bytes
		if (p_cnt == 1)
			return width * height *c_sz * c;
		if(p_cnt == 2) // NV12-like
			return (width * height * c_sz)*3/2; // w*h/2 for UV
		assert(0);
		return 0;
	}

	// number of bytes in image roi region
	size_t size(size_t roi_w, size_t roi_h) {
		unsigned int c = 0; // number of channels
		unsigned int c_sz = 0;
		unsigned int p_cnt = planes(c, c_sz); // planes and ch size in bytes
		if (p_cnt == 1)
			return roi_w * roi_h * c_sz * c;
		if (p_cnt == 2) // NV12-like
			return (roi_w * roi_h * c_sz) * 3 / 2; // w*h/2 for UV
		assert(0);
		return 0;
	}


	void reset() {
		if (image != nullptr) {
			zeImageDestroy(image);
			image = nullptr;
		}
		shared_handle = nullptr;
	}
	bool empty() {
		return (image == nullptr);
	}

	//TODO: disable copy constructor or add reference mechanism
	~l0_image_t() {
		reset();
	}
};

sycl::queue l0_queue(sycl::queue& q0);
sycl::queue l0_to_sycl(ze_command_queue_handle_t& q0);

class DPCPPCompute {
public:
    DPCPPCompute(unsigned int L0_GPU_ID = 0)
	{
		InitGPU(L0_GPU_ID);
		get_mem_prop();
	}
	DPCPPCompute(const ze_device_luid_ext_t& luid)
	{
		InitGPU(luid);
		get_mem_prop();
	}

	DPCPPCompute(const ze_device_luid_ext_t& luid, unsigned int max_multi_devices)
	{
		InitMultiGPU(luid, max_multi_devices);
		get_mem_prop();
	}

	DPCPPCompute(const std::vector<ze_device_luid_ext_t>& luids) {
		InitMultiGPU(luids);
		get_mem_prop();
	}

	DPCPPCompute(ID3D11Device* dx);
	DPCPPCompute(ID3D11Device* dx, unsigned int max_multi_devices);
	DPCPPCompute(const std::vector<ID3D11Device*>& dx);

	int InitGPU(unsigned int L0_GPU_ID);
	int InitGPU(const ze_device_luid_ext_t& luid);

	// initializes device with luid and it's peers on the same driver
	// supporting communication with the requested deivce
	// max_devices - max number of devices to init including requested device
	//		= 0 - init requested device and all available peers
	//		= 1 - init requested device only
	// returns number of devices actually initialized
	int InitMultiGPU(const ze_device_luid_ext_t& luid, unsigned int max_devices=0);
	// attempt to init multi-GPU context from given luids
	int InitMultiGPU(const std::vector<ze_device_luid_ext_t>& luids);

	static int ListGPUs(bool with_connectivity = true);

	int get_luid(ze_device_luid_ext_t& luid, unsigned int dev_idx = 0);
	int get_luid(LUID& luid, unsigned int dev_idx = 0);
	//sycl::queue q0;
	sycl::context ctx;
	sycl::device dev;
	sycl::queue q;

protected:
	ze_driver_handle_t l0_driver = nullptr;
	ze_context_handle_t l0_ctx = nullptr;
	ze_device_handle_t l0_dev = nullptr;
	ze_command_queue_handle_t l0_q = nullptr;
	ze_command_list_handle_t l0_commands = nullptr; // l0 copy commands for data conversion
	std::vector<ze_image_handle_t>	tmp_views = {}; // storage for views for batch conversion

public:
	// multi-GPU structures
	std::vector<sycl::device> devs;
	std::vector<sycl::queue> qs;
	std::vector<ze_device_handle_t>			l0_devs;
	// compute qs and command lists
	std::vector<ze_command_queue_handle_t>	l0_qs;
	std::vector<ze_command_list_handle_t>	l0_coms; 
	// dma copy qs and command lists
	std::vector<ze_command_queue_handle_t>	l0_dma_qs;
	std::vector<ze_command_list_handle_t>	l0_dma_coms;

	uint32_t dev_mem_count = 0;
	

	// just 1-dim for now
	template<typename T>
	sycl::buffer<T, 1> import_buffer(l0_allocation_t& l0_mem) {
		sycl::buffer<T,1> buf = sycl::make_buffer< sycl::backend::ext_oneapi_level_zero, T>({ l0_mem.data, ext::oneapi::level_zero::ownership::keep }, q.get_context());
		return buf;
	}
	void shared_to_l0(shared_resource_t& src, l0_image_t& dst);
	// convert GPU image to linear format for processing
	void image_to_linear_l0(l0_image_t& src, void* dst);
	// convert GPU image to linear format for processing
	void image_to_linear_l0(l0_image_t* src, unsigned int src_num, void* dst);
	void image_Y_UV_to_linear_l0(l0_image_t* src, unsigned int src_num, void* dst);
	// convert linear row-major format to GPU image
	void linear_to_image_l0(const void* src, l0_image_t& dst);
	void linear_to_image_l0(const void* src, l0_image_t& dst, const shared_rect& dst_roi); // with ROI in dst image
	size_t linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst);
	size_t linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst, const shared_rect& dst_roi); // with the same ROI in all dst images
	size_t linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst, const shared_rect* dst_rois); // with different ROIs in dst images
	// convert linear row-major format to GPU DX11 texture
	void linear_to_texture(void* src_dpc_linear, ID3D11Texture2D* dst_tex);
	// convert linear row-major format to GPU DX11 texture roi
	void linear_to_texture(void* src_dpc_linear, ID3D11Texture2D* dst_tex, const shared_rect& dst_roi);
	// convert 2 images in linear row-major format to 2 GPU DX11 textures
	// source data size is derived from textures format and resolution
	void linear_planes_to_textures(void* src_dpc_linear, ID3D11Texture2D* dst_tex[2]);
	// convert 2 images in linear row-major format to 2 GPU DX11 texture ROIs
	// source data size is derived from textures format and ROI
	void linear_planes_to_textures(void* src_dpc_linear, ID3D11Texture2D* dst_tex[2], const shared_rect dst_rois[2]);

	// new experimental, comment if doesn't compile
	sycl::image<2> image_to_compute(l0_image_t& src, unsigned int pln = 0);
	// l0 malloc
	void* malloc_dev_shared(size_t size); // malloc exportable buffer on current device
	void* get_shared_handle(void* alloc_ptr); // returns handle of an allocation created with malloc_dev_shared
	void* malloc_dev(unsigned int dev_idx, size_t size);
	void* malloc_dev_shared(unsigned int dev_idx, size_t size); // malloc exportable buffer on dev_idx device
	void* import_dev_shared(unsigned int dev_idx, void* peer_ptr, size_t size);
	void* malloc_host(size_t size);
	void* malloc_shared( size_t size);
	void* malloc_shared(unsigned int dev_idx, size_t size);
	void make_resident(unsigned int dev_idx, void* ptr, size_t size);
	void evict(unsigned int dev_idx, void* ptr, size_t size);
	// memcopy using device dev_idx
	void memcopy(void* dstptr, const void* srcptr, size_t size, unsigned int dev_idx = 0);

	// future calls for future usage
	l0_allocation_t texture_to_l0(ID3D11Texture2D* src); // to l0 linear mem, implement to image in future
	l0_allocation_t buffer_to_l0(ID3D11Buffer* src);
	l0_image_t texture_to_image_l0(ID3D11Texture2D* src); // to l0 linear mem, implement to image in future
	bool share_image_l0(unsigned int dst_dev, l0_image_t& dst, const l0_image_t& src);

	void get_mem_prop();

	static dev_id dev_level_zero(const std::string name = "Intel(R) Arc(TM)", const int dev_idx = 0) {
		int found = -1;

		auto platforms = platform::get_platforms();
		for (int p = 0; p < platforms.size(); p++) {
			if (platforms[p].get_backend() == sycl::backend::ext_oneapi_level_zero) {
				auto devices = platforms[p].get_devices();
				for (int d = 0; d < devices.size(); d++) {
					const std::string devname = devices[d].get_info<info::device::name>();
					if (devices[d].is_gpu() && devname.find(name) != std::string::npos) {
						if (++found == dev_idx) return dev_id(p, d);
					}
				}
			}
		}
		return dev_id();
	}

	virtual ~DPCPPCompute();
	void reset();


	// used only for testing/debugging
	l0_image_t		l0;
	void* gpu_linear = nullptr;
	void* test_kernel_dst = nullptr;
	void* gpu1_linear = nullptr;
	void* gpu01_linear = nullptr; // imported shared buffer

	void test_copy_usm(const void* src_gpu, void* dst_gpu, size_t szbytes, unsigned int dev_idx);
	void test_copy(const void* src_gpu, void* dst, size_t szbytes, unsigned int dev_idx = 0);
	void test_copy(l0_image_t& src, void* dst=nullptr);
	void test_copy(l0_allocation_t& src, std::vector<char>& dst);
	void test_convert_dpcpp(l0_allocation_t& src, std::vector<char>& dst);
	void test_convert_cpp(const std::vector<char>& src, std::vector<char>& dst);
	void test_copy(l0_image_t& src, std::vector<char>& dst, bool via_gpu=true);
	// end of testing/debugging

};



#endif //DPCPP_COMPUTE_HPP