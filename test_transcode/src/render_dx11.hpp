//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================
#ifndef RENDERDX11_HPP
#define RENDERDX11_HPP



#include <d3d11.h>
#include <memory>
#include <iostream>
///
/// A fullscreen quad renderer for NV12 textures
/// using shaders for a light filed display
/// NV12pixel.hlsl and NV12vertex.hlsl are copies of shaders from .cpp file
/// to check for bugs at compile time
///

struct PS_LFCALIB_DATA;
struct PS_LFQUILT_DATA;



template <typename ptr_type> class ref_ptr {
public:
    ptr_type ptr = nullptr;
    ref_ptr(ptr_type ptr, bool incref = true ) :ptr(ptr) {
        if (ptr != nullptr && incref) ptr->AddRef();
    }
    ref_ptr(ref_ptr& p) :ptr(p.ptr) {
        if (ptr != nullptr) ptr->AddRef();
    }
    ~ref_ptr() {
        if (ptr != 0) ptr->Release();
    }
};

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
    void init(ID3D11Device* dxdev, HWND window,
        PS_LFCALIB_DATA& calib, PS_LFQUILT_DATA& quilt
    );


    bool render_NV12(ID3D11Device* dxdev, ID3D11Texture2D* nv12);
    inline bool is_ready() // check if initialized and ready to render
    {
        return (swap_chain != nullptr);
    }

    // ATTN: the function to pass parameters to the shader
    bool update_quilt_parameters(PS_LFQUILT_DATA& quilt);

    // keeping it in the header to
    // track of added/removed members
    inline void reset() {
        crel(factory);
        crel(view);
        crel(swap_chain);
        for (int i = 0; i < 2; i++) crel(y_uv_view[i]);
        crel(NV12_VS);
        crel(NV12_PS);
        for (int i = 0; i < 2; i++) crel(NV12_PS_cbuf[i]);
        crel(sampler_linear);
        crel(sampler_nearest);
        crel(disable_depth);
        crel(copy_event);

        dxdevice = nullptr;
    }

    virtual ~RenderDX11();

    // some helpers
    static ID3D11Texture2D* create_texture(ID3D11Device* dxdev, int w, int h);

    static IDXGIKeyedMutex* acq_mtx(ID3D11Texture2D* tex);
    static void release_mtx(IDXGIKeyedMutex* pDXGIKeyedMutex);

    typedef ref_ptr<ID3D11Buffer*>              buffer;
    typedef ref_ptr<ID3D11Texture2D*>           texture2d;
    typedef ref_ptr<ID3D11ShaderResourceView*>  view_ro;
    typedef ref_ptr<ID3D11UnorderedAccessView*> view_rw;
    typedef ref_ptr<ID3D11VertexShader*>        vshader;
    typedef ref_ptr<ID3D11PixelShader*>         pshader;
    typedef ref_ptr<ID3D11ComputeShader*>       cshader;

    template<typename T> buffer create_buffer(size_t numel, const T* src_data_host = nullptr) {
        ID3D11Buffer* buf = nullptr;
        // First we create a buffer in GPU memory
        D3D11_BUFFER_DESC desc_buffer;
        ZeroMemory(&desc_buffer, sizeof(desc_buffer));
        desc_buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS |
            D3D11_BIND_SHADER_RESOURCE;
        desc_buffer.ByteWidth = numel * sizeof(T);
        desc_buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc_buffer.StructureByteStride = sizeof(T);

        D3D11_SUBRESOURCE_DATA init_subresource;
        D3D11_SUBRESOURCE_DATA* init_data = nullptr;
        if (src_data_host != nullptr) {
            init_subresource.pSysMem = src_data_host;
            init_data = &init_subresource;
        }
        if (FAILED(dxdev->CreateBuffer(&desc_buffer, init_data, &buf)))
            buf = nullptr;
        buffer b(buf, false);
        return b;
    }

    // constant buffer
    template<typename T> buffer create_cbuffer(size_t numel = 1, const T* src_data_host = nullptr) {
        ID3D11Buffer* buf = nullptr;
        // Prepare 2 constant buffers with shader parameters
        // Fill in a buffer description.
        D3D11_BUFFER_DESC cbDesc;
        cbDesc.ByteWidth = sizeof(T) * numel;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cbDesc.MiscFlags = 0;
        cbDesc.StructureByteStride = 0;

        // Fill in the subresource data.
        D3D11_SUBRESOURCE_DATA init_subresource;
        D3D11_SUBRESOURCE_DATA* init_data = nullptr;
        if (src_data_host != nullptr) {
            init_subresource.pSysMem = src_data_host;
            init_subresource.SysMemPitch = 0;
            init_subresource.SysMemSlicePitch = 0;
            init_data = &init_subresource;
        }

        // Create the buffer.
        if (FAILED(dxdev->CreateBuffer(&cbDesc, init_data, &buf)))
            buf = nullptr;
        buffer b(buf, false);
        return b;
    }

    vshader create_vs(char* code);
    pshader create_ps(char* code);
    cshader create_cs(char* code);

    ID3D11Device* get_device() { return dxdevice; }


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
    ID3D11RenderTargetView* view = nullptr; // view of target
    D3D11_VIEWPORT viewport = {};
    ID3D11VertexShader* NV12_VS = nullptr;
    ID3D11PixelShader* NV12_PS = nullptr;
    ID3D11Buffer* NV12_PS_cbuf[2] = {};
    ID3D11ShaderResourceView* y_uv_view[2] = {};
    ID3D11SamplerState* sampler_linear = nullptr;
    ID3D11SamplerState* sampler_nearest = nullptr;
    ID3D11DepthStencilState* disable_depth = nullptr;

    // cleaning com interface pointer
    template <typename ICOM> void crel(ICOM* &com) {
        if (com != nullptr) {
            com->Release();
            com = nullptr;
        }
    }
    bool create_srv_nv12(ID3D11Device* dxdev, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]);
    HRESULT CreateStructuredBuffer(UINT uElementSize, UINT uCount, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateRawBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateConstBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut);
    HRESULT CreateRWTexture(UINT width, UINT height, void* pInitData, ID3D11Texture2D** ppTexOut);
    HRESULT CreateRWTexture(ID3D11Texture2D* asTex, void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth=0, UINT newHeight=0);
    HRESULT CreateBufferSRV(ID3D11Buffer* pBuffer, ID3D11ShaderResourceView** ppSRVOut, UINT firstElement = 0, UINT numElements = 0);
    HRESULT CreateBufferUAV(ID3D11Buffer* pBuffer, ID3D11UnorderedAccessView** ppUAVOut, UINT firstElement = 0, UINT numElements = 0);
    HRESULT CreateRWTextureUAV(ID3D11Texture2D* pTexture, ID3D11UnorderedAccessView** ppUAVOut, UINT numUAV=1);
    HRESULT CreateCS(char* code, ID3D11ComputeShader** ppCS);
};

#endif //RENDERDX11_HPP
