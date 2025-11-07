//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================
#ifndef RENDERDX11_HPP
#define RENDERDX11_HPP


#include "lf_basics.hpp"
#include <d3d11.h>
#include <memory>
#include <iostream>
///
/// A fullscreen quad renderer for NV12 textures
/// using shaders from NV12shaders.cpp
/// NV12pixel.hlsl and NV12vertex.hlsl are copies of shaders from .cpp file
/// to check for bugs at compile time
///

struct fence {
    fence() : query(nullptr), ctx(nullptr) {}
    fence(ID3D11Device* device) :query(nullptr), ctx(nullptr) {
        init(device);
    }
    bool init(ID3D11Device* device) {
        clear();
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->CreateQuery(&queryDesc, &query);
        return true;
    }
    bool insert(ID3D11DeviceContext* inctx) {
        if (ctx != nullptr || query == nullptr)
            return false;
        inctx->AddRef();
        ctx = inctx;
        ctx->End(query);
        return true;
    }

    bool wait() {
        if (ctx == nullptr || query==nullptr) return false;
        BOOL done = FALSE; // This data type is different depending on the query type

        while (S_OK != ctx->GetData(query, &done, sizeof(BOOL), 0))
        {
        }
        ctx->Release();
        ctx = nullptr;
        return true;
    }

    void clear() {
        if (query != nullptr) {
            query->Release();
            query = nullptr;
        }
        if (ctx != nullptr) {
            ctx->Release();
            ctx = nullptr;
        }
    }

    bool empty() {
        return (query==nullptr);
    }
    ID3D11Query* query = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
};


class RenderDX11 {
public:
    void init(ID3D11Device* dxdev, HWND window, lf_shader_type display = mono, bool mode_sRGB = false); // Note: adds reference to dx device
    void resize(); // window resize and full screen/windowed change

    bool render_texture(ID3D11Texture2D* texture);  // render single-plane texture 
    bool render_texture_planar(ID3D11Texture2D* nv12);        // render planar Y, UV texture

    inline bool is_ready() // check if initialized and ready to render
    {
        return (swap_chain != nullptr);
    }

    // keeping it in the header to
    // track of added/removed members
    inline void reset() {
        crel(factory);
        crel(rt_view);
        crel(swap_chain);
        for (int i = 0; i < 2; i++) crel(y_uv_view[i]);
        crel(FS_QUAD_VS);
        for (int i = 0; i < shader_types_total; i++) {
            crel(LF_NV12_PS[i]);
            crel(LF_PS[i]);
        }
        crel(LF_NV12_CS);
        crel(LF_CS);
        crel(LF_PS_cbuf);
        crel(sampler_linear);
        crel(sampler_nearest);
        crel(disable_depth);
        crel(copy_event);

        crel(dxdevice);
    }

    virtual ~RenderDX11();

    // utility functions
    static ID3D11Device* create_device(unsigned int adapter_num);
    static ID3D11Texture2D* create_texture(ID3D11Device* dxdev, int w, int h);
    static bool create_srv_nv12(ID3D11Device* dxdev, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]);
    static bool create_srv(ID3D11Device* dxdev, ID3D11Texture2D* texture, ID3D11ShaderResourceView** view);

    static IDXGIKeyedMutex* acq_mtx(ID3D11Texture2D* tex);
    static void release_mtx(IDXGIKeyedMutex* pDXGIKeyedMutex);


    ID3D11VertexShader* create_vs(const char* code);
    ID3D11PixelShader* create_ps(const char* code);
    ID3D11ComputeShader* create_cs(const char* code);

    ID3D11Buffer* create_cb(unsigned int size, const void* data = nullptr);

    ID3D11Device* get_device() { return dxdevice; }

    // create shaders with necessary paramter cbuffers
    virtual void create_shaders();

    virtual void update_lf_parameters(const lf_parameters_render& lf_params);

    ID3D11Query* create_event() {
        ID3D11Query* pQuery = nullptr;
        D3D11_QUERY_DESC pQueryDesc = {};
        pQueryDesc.Query = D3D11_QUERY_EVENT;
        pQueryDesc.MiscFlags = 0;
        dxdevice->CreateQuery(&pQueryDesc, &pQuery);
        return pQuery;
    }

    ID3D11Query* copy_event = nullptr;
protected:
    ID3D11Device* dxdevice = nullptr; // created externally and passed at init

    IDXGIFactory1* factory = nullptr;
    IDXGISwapChain* swap_chain;
    //ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rt_view = nullptr; // view of target
    D3D11_VIEWPORT viewport = {};

    ID3D11VertexShader* FS_QUAD_VS = nullptr;
    ID3D11PixelShader* LF_NV12_PS[shader_types_total] = {};
    ID3D11PixelShader* LF_PS[shader_types_total] = {};
    ID3D11ComputeShader* LF_NV12_CS = nullptr;
    ID3D11ComputeShader* LF_CS = nullptr;
    ID3D11Buffer* LF_PS_cbuf = nullptr;

    ID3D11ShaderResourceView* y_uv_view[2] = {};
    ID3D11SamplerState* sampler_linear = nullptr;
    ID3D11SamplerState* sampler_nearest = nullptr;
    ID3D11DepthStencilState* disable_depth = nullptr;

    lf_shader_type display_shader = mono; // shader selector

    bool RT_mode_sRGB = false;  // convert final render target colors to sRGB (liner rendering)

    // cleaning com interface pointer
    template <typename ICOM> inline void crel(ICOM* &com) {
        if (com != nullptr) {
            com->Release();
            com = nullptr;
        }
    }
    HRESULT CreateStructuredBuffer(UINT uElementSize, UINT uCount, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateRawBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateConstBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateRWTexture(UINT width, UINT height, void* pInitData, ID3D11Texture2D** ppTexOut);
    HRESULT CreateRWTexture(ID3D11Texture2D* asTex, void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth=0, UINT newHeight=0);
    HRESULT CreateBufferSRV(ID3D11Buffer* pBuffer, ID3D11ShaderResourceView** ppSRVOut, UINT firstElement = 0, UINT numElements = 0);
    HRESULT CreateBufferUAV(ID3D11Buffer* pBuffer, ID3D11UnorderedAccessView** ppUAVOut, UINT firstElement = 0, UINT numElements = 0);
    HRESULT CreateRWTextureUAV(ID3D11Texture2D* pTexture, ID3D11UnorderedAccessView** ppUAVOut, UINT numUAV=1);
};

#endif //RENDERDX11_HPP
