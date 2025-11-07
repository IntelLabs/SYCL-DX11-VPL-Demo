//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
#ifndef SHARED_RESOURCE_HPP
#define SHARED_RESOURCE_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <iostream>
#include <vector>

// some basic device creation/query functions
// create DX device from LUID
ID3D11Device* create_device(const LUID& luid);
// query video memory size, 0 would most likely indicate iGPU
SIZE_T device_video_memory(const LUID& luid);
// just an example function to get ids of all adapters 
// for real usage just implement your own search loop
// if luids==nullptr, returns total number of adapters found, num - ignored
// if luids != nullptr, writes adapter luids_num first adpater luids into luids array,
//                      returns number of luids actually written
unsigned int enumerate_adapter_ids(unsigned int luids_num=0, LUID* luids=nullptr);

// all data/methods necessary to share DX11 texture
// between DX devices or other APIs
struct shared_resource_t {
    ID3D11Texture2D* texture = nullptr;
    IDXGIKeyedMutex* keyed_mutex = nullptr;
    HANDLE handle = nullptr;
    void init(ID3D11Texture2D* tex, bool newref = true);
    void reset();
    void lock(uint64_t key = 0);
    void unlock(uint64_t key = 0);
    bool empty() { return (texture == nullptr); }

    shared_resource_t() {
    }
    
    // makes a copy of the sharing structures for the same texture
    shared_resource_t(const shared_resource_t& src); 

    shared_resource_t(ID3D11Texture2D* tex) {
        init(tex, true); // ref is always added in constructor
    }

    ~shared_resource_t() {
        reset();
    }
    // our best guess on plane 0 texel size
    static inline unsigned int p0_texel_size(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_NV12:
            return 1;
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_R8G8_UNORM :
            return 2;
        default: // R16G16, RGBA, BGRA
            return 4;
        }
        return 0;
    }
    // our best guess on linear memory size
    static inline unsigned int linear_size(const D3D11_TEXTURE2D_DESC& desc) {
        switch (desc.Format) {
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return desc.Height*desc.Width*p0_texel_size(desc.Format)*3/2;
        default:
            return desc.Height * desc.Width * p0_texel_size(desc.Format);
        }
        return 0;
    }
};

ID3D11Texture2D* share_on_device(shared_resource_t& shared, ID3D11Device* dev);

struct shared_rect {
    shared_rect(int x, int y, int w, int h) :
        x(x), y(y), w(w), h(h)
    {}
    shared_rect() :
        x(0), y(0), w(0), h(0)
    {}
    bool operator !=(const shared_rect& r1) const {
        return (w != r1.w || h != r1.h || x != r1.x || y != r1.y);
    }
    inline bool operator ==(const shared_rect& r1) const {
        return (w == r1.w && h == r1.h && x == r1.x && y == r1.y);
    }
    int x;
    int y;
    int w;
    int h;
};
#define safe_rcp(a) (((a)!=0)?(1.0f/(a)):0.f)

enum shared_copy_mode_t {
    source_Y_UV,        // original packing of Y_UV in planar textures
    split_Y_U_V,        // 1-channel texture(s) with Y(s), U(s), V(s) separated
    split_Y             // 1-channel texture with Y-only, UV-ignored
};


class shared_input_batch_t {
public:
    shared_input_batch_t() {}
    shared_input_batch_t(unsigned int num_res);
    void init(unsigned int num_res);
    void reset();
    unsigned int size() const {
        return num;
    };
    shared_resource_t* operator[](unsigned int i) {
        return (i < num) ? &resources[i] : nullptr;
    }
    const shared_resource_t* operator[](unsigned int i) const {
        return (i < num) ? &resources[i] : nullptr;
    }

    unsigned int copy(ID3D11Texture2D** src, unsigned int src_num, unsigned int start = 0);
    unsigned int crop_scale(ID3D11Texture2D** src, unsigned int src_num,
        const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
        unsigned int start = 0);
    unsigned int copy_Y_UV(ID3D11Texture2D** src, unsigned int src_num, shared_copy_mode_t mode , unsigned int start = 0);
    unsigned int crop_scale_Y_UV(ID3D11Texture2D** src, unsigned int src_num,
        const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
        shared_copy_mode_t mode,
        unsigned int start = 0);
    virtual ~shared_input_batch_t();

protected:
    shared_resource_t*  resources=nullptr;
    unsigned int    num = 0;
    ID3D11Device*   device = nullptr;
    ID3D11Query*    done_event = nullptr; // copy complete event
    ID3D11Buffer* cs_cbuffer = nullptr; // crop scale shader params const buffer
    ID3D11ComputeShader* cs_nv12_luma_shader = nullptr;
    ID3D11ComputeShader* cs_nv12_chroma_shader = nullptr;
    // channel splitting shaders
    ID3D11ComputeShader* cs_nv12_Y_copy_shader = nullptr; // copy
    ID3D11ComputeShader* cs_nv12_UV_copy_shader = nullptr; 
    ID3D11ComputeShader* cs_nv12_UV_shader = nullptr; // crop/scale for UV (Y uses nv12_luma)

    ID3D11SamplerState* sampler_linear = nullptr;
    std::vector<ID3D11ShaderResourceView*> y_uv_views; // in-flight views for the input textures
    std::vector<ID3D11UnorderedAccessView*> lc_uaviews;   // in-flight views for intermediate copy targets

    // checks if crop/scale parameters have changed and updates them
    // returns true if textures need to be re-created
    bool update_crop_scale(ID3D11Texture2D* src0, const shared_rect& crop, UINT dw, UINT dh);
    // go over textures and make sure their parameters match
    // cs_params, reset all textures not matching params
    void check_size(shared_copy_mode_t mode_Y_UV=source_Y_UV);

    struct crop_scale_t
    {
        void set(int sw, int sh, int dw = 0, int dh = 0) {
            src.w = sw; src.h = sh;
            sw_rcp = safe_rcp(sw);
            sh_rcp = safe_rcp(sh);
            dst.w = (dw == 0) ? src.w : dw;
            dw_rcp = safe_rcp(dst.w);
            dst.h = (dh == 0) ? src.h : dh;
            dh_rcp = safe_rcp(dst.h);
            src.crop = { 0, 0, sw, sh };
        }

        void set(int sw, int sh, const shared_rect& scrop,
            int dw = 0, int dh = 0) {
            src.w = sw; src.h = sh;
            sw_rcp = safe_rcp(sw);
            sh_rcp = safe_rcp(sh);
            src.crop = { std::max(0, std::min(scrop.x, sw - 1)),
                std::max(0, std::min(scrop.y, sh - 1)),
                std::max(0, std::min(scrop.w, sw)),
                std::max(0, std::min(scrop.h, sh)) };

            dst.w = (dw == 0) ? scrop.w : dw;
            dw_rcp = safe_rcp(dst.w);
            dst.h = (dh == 0) ? scrop.h : dh;
            dh_rcp = safe_rcp(dst.h);
        }

        struct {
            int     w;      //src width
            int     h;     // src height
            shared_rect    crop;
        } src;
        struct {
            int     w;      //dst width
            int     h;     // dst height
        } dst;
        float   sw_rcp = 0.0f;
        float   sh_rcp = 0.0f;
        float   dw_rcp = 0.0f;
        float   dh_rcp = 0.0f;

    };

    crop_scale_t    cs_param = {};

};

#endif //SHARED_RESOURCE_HPP

