//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================

#include "shared_resource.hpp"
#include <iostream>
#include <vector>
#include <d3dcompiler.h>
#include <d3d11_3.h>
#include <assert.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

template <typename T> inline void reset_ptr(T** ppObj) {
    if (ppObj[0] != nullptr) {
        ppObj[0]->Release();
        ppObj[0] = nullptr;
    }
}
static inline bool operator == (const LUID & l0, const LUID & l1) {
    return (l0.LowPart == l1.LowPart && l0.HighPart == l1.HighPart);
}

ID3D11Device* create_device(const LUID& luid) {
    ID3D11Device* device = nullptr;
    // find suitable adapter
    IDXGIFactory1* pFactory =nullptr;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return nullptr;

    }
    UINT i = 0;
    IDXGIAdapter* pAdapter=nullptr;
    //std::vector <IDXGIAdapter*> vAdapters;
    DXGI_ADAPTER_DESC adapter{};
    while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        //vAdapters.push_back(pAdapter);
        // here you want to enumerate outputs and find the looking glass you want
        pAdapter->GetDesc(&adapter);

        if (adapter.AdapterLuid == luid) break;
        pAdapter->Release();
        pAdapter = nullptr;
        ++i;
    }
    pFactory->Release();

    if (pAdapter==nullptr) {
        std::cout << "Adapter not found by LUID!" << std::endl;
        return nullptr;
    }

    D3D_FEATURE_LEVEL  FeatureLevelsRequested = D3D_FEATURE_LEVEL_11_0;
    UINT               numLevelsRequested = 1;
    D3D_FEATURE_LEVEL  FeatureLevelsSupported;
    ID3D11DeviceContext* context = nullptr;

    if (FAILED(hr = D3D11CreateDevice(pAdapter, //NULL,
        D3D_DRIVER_TYPE_UNKNOWN, //D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0 | D3D11_CREATE_DEVICE_DEBUG, //, //  ATTN:  
        &FeatureLevelsRequested,
        numLevelsRequested,
        D3D11_SDK_VERSION,
        &device,
        &FeatureLevelsSupported,
        &context)))
    {
        std::cout << "Creating DX device failed!" << std::endl;
        pAdapter->Release(); 
        return nullptr;
    }

    // set multithreading
    ID3D10Multithread* pD10Multithread = nullptr;

    context->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pD10Multithread);
    BOOL mt = pD10Multithread->SetMultithreadProtected(true);
    (void)mt;
    pAdapter->Release(); // we already have DX device
    //if (context) context->Release();
    return device;
}

SIZE_T device_video_memory(const LUID& luid) {
    // find suitable adapter
    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return 0;

    }
    UINT i = 0;
    IDXGIAdapter* pAdapter = nullptr;
    DXGI_ADAPTER_DESC adapter{};
    while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        // here you want to enumerate outputs and find the looking glass you want
        pAdapter->GetDesc(&adapter);

        if (adapter.AdapterLuid == luid) break;
        pAdapter->Release();
        pAdapter = nullptr;
        ++i;
    }
    pFactory->Release();

    if (pAdapter == nullptr) {
        std::cout << "Adapter not found by LUID!" << std::endl;
        return 0;
    }
    pAdapter->Release();

    return adapter.DedicatedVideoMemory;
}

unsigned int enumerate_adapter_ids(unsigned int luids_num, LUID* luids) {
    IDXGIFactory1* pFactory = nullptr;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return 0;

    }
    UINT i = 0;
    IDXGIAdapter* pAdapter = nullptr;
    if (luids == nullptr) {
        while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
        {
            pAdapter->Release();
            pAdapter = nullptr;
            ++i;
        }
    }
    else {
        DXGI_ADAPTER_DESC adapter{};
        while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND && i<luids_num)
        {
            pAdapter->GetDesc(&adapter);
            luids[i]=adapter.AdapterLuid;
            pAdapter->Release();
            pAdapter = nullptr;
            ++i;
        }
    }
    pFactory->Release();
    return i;
}

//  ATTN doesn't work: driver bug
ID3D11Texture2D* share_on_device(shared_resource_t& shared, ID3D11Device* dev) {
    //HANDLE handle = shared.handle;
    ID3D11Texture2D* tex_on_dev = nullptr;
    ID3D11Device1* dev1 = nullptr;
    dev->QueryInterface(__uuidof(ID3D11Device1), (void**)(&dev1));
    if (dev1 == nullptr) return nullptr;
    HRESULT hr = dev1->OpenSharedResource1(shared.handle, __uuidof(ID3D11Texture2D), (void**)&tex_on_dev);
    (void)hr;
    //ID3D11Resource* temp_res = nullptr;
    //HRESULT hr = dev1->OpenSharedResource1(handle, __uuidof(ID3D11Resource), (void**)(&temp_res));
    //if (temp_res == nullptr) return nullptr;
    //temp_res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)(&tex_on_dev));
    //temp_res->Release();
    dev1->Release();
    return tex_on_dev;
}

HRESULT create_rwtexture(ID3D11Device* device, ID3D11Texture2D* asTex, void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth = 0, UINT newHeight = 0) {
    *ppTexOut = nullptr;
    D3D11_TEXTURE2D_DESC asDesc{};
    asTex->GetDesc(&asDesc);

    D3D11_TEXTURE2D_DESC desc{};
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//| D3D11_RESOURCE_MISC_SHARED;//  
    desc.CPUAccessFlags = 0;
    desc.Format = asDesc.Format; // Change this to what you need
    desc.Height = (newHeight == 0) ? asDesc.Height : newHeight;
    desc.Width = (newWidth == 0) ? asDesc.Width : newWidth;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (pInitData) {
        UINT elemSize = shared_resource_t::p0_texel_size(desc.Format); //ATTN: either 1, 2 bytes or 4 per texel
        D3D11_SUBRESOURCE_DATA InitData{};
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = desc.Width * elemSize; //ATTN: elemSize
        return device->CreateTexture2D(&desc, &InitData, ppTexOut);
    }
    else
        return device->CreateTexture2D(&desc, nullptr, ppTexOut);

}

bool channel_format(DXGI_FORMAT& ch_fmt, DXGI_FORMAT tex_fmt) {
    switch (tex_fmt) {
    case DXGI_FORMAT_NV12:
        ch_fmt = DXGI_FORMAT_R8_UNORM;
        return true;
    case DXGI_FORMAT_P010:
        [[fallthrough]];
    case DXGI_FORMAT_P016:
        ch_fmt = DXGI_FORMAT_R16_UNORM;
        return true;
    default:
        ch_fmt = DXGI_FORMAT_R8_UNORM; // return something
        break;
    }
    return false;
}

HRESULT create_rwtexture_Y(ID3D11Device* device, ID3D11Texture2D* asTex, void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth = 0, UINT newHeight = 0) {
    *ppTexOut = nullptr;
    D3D11_TEXTURE2D_DESC asDesc{};
    asTex->GetDesc(&asDesc);

    D3D11_TEXTURE2D_DESC desc{};
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//| D3D11_RESOURCE_MISC_SHARED;//  
    desc.CPUAccessFlags = 0;
    bool convertible = channel_format(desc.Format, asDesc.Format); // Change this to what you need
    desc.Height = (newHeight == 0) ? asDesc.Height : newHeight;
    desc.Width = (newWidth == 0) ? asDesc.Width : newWidth;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (!convertible) {
        std::cout << "Error: non-planar input texture for Y!" << std::endl;
        exit(-1);
    }

    if (pInitData) {
        UINT elemSize = shared_resource_t::p0_texel_size(desc.Format); //ATTN: either 1, 2 bytes or 4 per texel
        D3D11_SUBRESOURCE_DATA InitData{};
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = desc.Width * elemSize; //ATTN: elemSize
        return device->CreateTexture2D(&desc, &InitData, ppTexOut);
    }
    else
        return device->CreateTexture2D(&desc, nullptr, ppTexOut);

}


HRESULT create_rwtexture(ID3D11Device* device, ID3D11Texture2D* asTex, shared_copy_mode_t mode,  void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth = 0, UINT newHeight = 0) {
    *ppTexOut = nullptr;
    D3D11_TEXTURE2D_DESC asDesc{};
    asTex->GetDesc(&asDesc);

    D3D11_TEXTURE2D_DESC desc{};
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//| D3D11_RESOURCE_MISC_SHARED;//  
    desc.CPUAccessFlags = 0;
    if (mode == source_Y_UV) {
        desc.Format = asDesc.Format;
    } else {
        bool convertible = channel_format(desc.Format, asDesc.Format); // Change this to what you need
        if (!convertible) {
            std::cout << "Error: non-planar input texture for Y_UV!" << std::endl;
            exit(-1);
        }
    }
    newHeight = (newHeight == 0) ? asDesc.Height : newHeight;
    newWidth = (newWidth == 0) ? asDesc.Width : newWidth;
    desc.Height = (mode == split_Y_U_V) ? newHeight + newHeight / 2 : newHeight;
    desc.Width = newWidth;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (pInitData) {
        UINT elemSize = shared_resource_t::p0_texel_size(desc.Format); //ATTN: either 1 or 2, or 4 bytes per texel
        D3D11_SUBRESOURCE_DATA InitData{};
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = desc.Width * elemSize; //ATTN: elemSize
        return device->CreateTexture2D(&desc, &InitData, ppTexOut);
    }
    else
        return device->CreateTexture2D(&desc, nullptr, ppTexOut);

}


shared_resource_t::shared_resource_t(const shared_resource_t& src) {
    keyed_mutex = src.keyed_mutex;
    if(keyed_mutex!=nullptr) keyed_mutex->AddRef();
    texture = src.texture;
    if (texture != nullptr) texture->AddRef();
    handle = nullptr;
    if (src.handle != nullptr) {
        DuplicateHandle(GetCurrentProcess(),
            src.handle, GetCurrentProcess(), &handle, 
            0, TRUE, DUPLICATE_SAME_ACCESS);
    }
}

// Shared dx11 texture methods and functions 
void shared_resource_t::init(ID3D11Texture2D* tex, bool newref) {
    reset();

    tex->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&keyed_mutex);


    IDXGIResource1* pResource;
    tex->QueryInterface(__uuidof(IDXGIResource1), (void**)&pResource);
    HRESULT hr = pResource->CreateSharedHandle(NULL,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        NULL,
        &handle);
    if (hr != S_OK) std::cout << "ERROR: CreateSharedHandle failed!" << std::endl;
    pResource->Release();

    if (newref) tex->AddRef();
    texture = tex;

}

//// assume there is texture tex that needs updating
//IDXGIKeyedMutex* keyed_mutex = nullptr;
//tex->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&keyed_mutex);
//int key = 0; // key for writing gpu
//int key_next = 0; // key for the next gpu having access to tex, 0 here since we use the same gpu
//
//keyed_mutex->AcquireSync((UINT64)key, INFINITE);
//
//// texture writing code here (rendering, compute, copy, etc.)
//
//keyed_mutex->ReleaseSync((UINT64)key_next); // the write results are visible after the call


void shared_resource_t::lock(uint64_t key) {
    if (keyed_mutex != nullptr) {
        keyed_mutex->AcquireSync((UINT64)key, INFINITE);
    }
}

void shared_resource_t::unlock(uint64_t key) {
    if (keyed_mutex != nullptr) {
        keyed_mutex->ReleaseSync((UINT64)key);
    }
}

void shared_resource_t::reset() {
    reset_ptr(&keyed_mutex);
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
    reset_ptr(&texture);
}

shared_input_batch_t::shared_input_batch_t(unsigned int num_res) {
    init(num_res);
}

shared_input_batch_t::~shared_input_batch_t() {
    reset();
}

void shared_input_batch_t::init(unsigned int num_res) {
    reset();
    if (num_res > 0) {
        resources = new shared_resource_t[num_res];
    }
    num = num_res;
}

void shared_input_batch_t::reset() {
    if (resources!=nullptr) {
        for (auto i = 0; i < num; i++) {
            resources[i].reset();
        }
        delete[] resources;
        resources = nullptr;
    }
    num = 0;
    reset_ptr(&done_event);
    reset_ptr(&cs_cbuffer);
    reset_ptr(&sampler_linear);
    reset_ptr(&cs_nv12_luma_shader);
    reset_ptr(&cs_nv12_chroma_shader);
    reset_ptr(&device);
    cs_param = {};

}
// DEBUG
//static bool same_device_texture(ID3D11Device* device, ID3D11Texture2D* tex) {
//    ID3D11Device* tex_device = nullptr;
//    tex->GetDevice(&tex_device);
//    bool same_device = (device == tex_device);
//    tex_device->Release();
//    if (!same_device) std::cout << "ERROR: multiple devices in the same texture batch!" << std::endl;
//    return same_device;
//}

// copy textures using dx11 into shared "staging" textures
// there is no size checking for non-empty dst textures
// all textures are assumed to belong to the same device
// the call will wait until copies finished
unsigned int shared_input_batch_t::copy(ID3D11Texture2D** src, unsigned int src_num, unsigned int start) {
    if (src_num == 0 || start >= num) return 0;
    unsigned int cpy_num = std::min(num - start, src_num);
    if (cpy_num == 0) return 0;
    // get device
    {
        ID3D11Device* dev = nullptr;
        src[0]->GetDevice(&dev);
        if (device == nullptr) {
            device = dev;
            device->AddRef();
        }
        if (device != dev) {
            dev->Release();
            std::cout << "Error: Device changed." << std::endl;
            return 0;
        }
        dev->Release();
        dev = nullptr;

    }
    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    //ATTN: TODO: check for size change
    for (unsigned int i = 0; i < cpy_num; i++) {
        assert(same_device_texture(device, src[i]));
        shared_resource_t& resource = resources[start + i];
        if (resource.empty()) {
            ID3D11Texture2D* copy = nullptr;
            create_rwtexture(device, src[i], nullptr, &copy, 0, 0);
            if(copy==nullptr) {
                printf("Failed to create gpu copy resource.");
            }
            resource.init(copy); // shared texture
            copy->Release();
        }

        resource.lock(0);

        ctx->CopyResource(resource.texture, src[i]);    // GPU copies from video memory to new resource
    }
    ctx->Flush();
    // finish query
    if (done_event == nullptr) {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->CreateQuery(&queryDesc, &done_event);
        assert(done_event != nullptr);
    }
    ctx->End(done_event);
    while (ctx->GetData(done_event, 0, 0, 0) == S_FALSE) {};

    for (unsigned int i = 0; i < cpy_num; i++) {
        resources[i + start].unlock(0); // important for copy to be correct
        resources[i + start].lock(0);   // make it avaialbale to DX
    }
    ctx->Release();

    return cpy_num;
}


HRESULT create_cbuffer(ID3D11Device* device, UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut) {
    *ppBufOut = nullptr;

    D3D11_BUFFER_DESC cbDesc;
    cbDesc.ByteWidth = uSize;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    if (pInitData) {
        // Fill in the subresource data.
        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = 0;
        InitData.SysMemSlicePitch = 0;

        // Create the buffer.
        return device->CreateBuffer(&cbDesc, &InitData, ppBufOut);
    }
    else
        return device->CreateBuffer(&cbDesc, nullptr, ppBufOut);

}

void update_cbuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, void* pData, UINT dataBytes) {
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
    memcpy(MappedResource.pData, pData, dataBytes);
    ctx->Unmap(cb, 0);
}

void print_errors_blob(ID3D10Blob* blob_err) {
    if (blob_err == nullptr) return;
    std::string err((char*)blob_err->GetBufferPointer(), blob_err->GetBufferSize());
    std::cout << "Shader compiler errors:" << std::endl << err << std::endl;
}

HRESULT create_cshader(ID3D11Device* device, const char* code, ID3D11ComputeShader** ppCS) {
    *ppCS = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    ID3D10Blob* blob_err = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "cs_5_0", shader_flags, 0, &blob, &blob_err);
    if (blob_err != nullptr) {
        if (hr != S_OK) print_errors_blob(blob_err);
        blob_err->Release(); blob_err = nullptr;
    }
    hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, ppCS);
    blob->Release(); blob = nullptr;
    return hr;
}

HRESULT create_srv_nv12(ID3D11Device* device, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]) {
    // DirectX specifies the view format to be DXGI_FORMAT_R8_UNORM for NV12 luminance channel.
    // Luminance is 8 bits per pixel. DirectX will handle converting 8-bit integers into normalized
    // floats for use in the shader.
    // We think that similar approach is taken for P010/P016, we just need 16-bit formats
    D3D11_TEXTURE2D_DESC desc;
    nv12->GetDesc(&desc);
    DXGI_FORMAT luma_fmt = DXGI_FORMAT_R8_UNORM;
    DXGI_FORMAT chroma_fmt = DXGI_FORMAT_R8G8_UNORM;
    if (desc.Format == DXGI_FORMAT_P010 || desc.Format == DXGI_FORMAT_P016) {
        luma_fmt = DXGI_FORMAT_R16_UNORM;
        chroma_fmt = DXGI_FORMAT_R16G16_UNORM;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC const luminancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
        nv12,
        D3D11_SRV_DIMENSION_TEXTURE2D,
        luma_fmt
    );

    HRESULT hr = device->CreateShaderResourceView(
        nv12,
        &luminancePlaneDesc,
        &y_uv[0]
    );
    if (FAILED(hr))
    {
        std::cout << "Failed to create texture SRV" << "Error " << hr << std::endl;
        return hr;
    }

    // DirectX specifies the view format to be DXGI_FORMAT_R8G8_UNORM for NV12 chrominance channel.
    // Chrominance has 8 bits for U and 8 bits for V per pixel, but with 1/2 resolution in each dimension.
    // DirectX will handle converting 8-bit integers into normalized floats for use in the shader.
    D3D11_SHADER_RESOURCE_VIEW_DESC const chrominancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
        nv12,
        D3D11_SRV_DIMENSION_TEXTURE2D,
        chroma_fmt
    );

    hr = device->CreateShaderResourceView(
        nv12,
        &chrominancePlaneDesc,
        &y_uv[1]
    );
    if (FAILED(hr))
    {
        std::cout << "Failed to create texture SRV" << "Error " << hr << std::endl;
    }
    return hr;
}

HRESULT create_uav_rwtexture(ID3D11Device* device, ID3D11Texture2D* pTexture, ID3D11UnorderedAccessView** ppUAVOut, UINT numUAV) {
    if (numUAV < 1) return S_OK;
    D3D11_TEXTURE2D_DESC descTex;
    pTexture->GetDesc(&descTex);
    bool planar = false;  // texture layout is like NV12 (P010, P016)
    DXGI_FORMAT luma_fmt = DXGI_FORMAT_R8_UNORM;
    DXGI_FORMAT chroma_fmt = DXGI_FORMAT_R8G8_UNORM;
    switch (descTex.Format) {
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        luma_fmt = DXGI_FORMAT_R16_UNORM;
        chroma_fmt = DXGI_FORMAT_R16G16_UNORM;
        [[fallthrough]];
    case DXGI_FORMAT_NV12: 
        planar = true;
        break;
    default:
        break;
    }
    if (planar) {
        ppUAVOut[0] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC descY{};
        descY.Format = luma_fmt;
        descY.Texture2D.MipSlice = 0;
        descY.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        HRESULT hr = device->CreateUnorderedAccessView(pTexture, &descY, &ppUAVOut[0]);
        if (numUAV < 2 || hr != S_OK) return hr;
        ppUAVOut[1] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC descUV{};
        descUV.Format = chroma_fmt;
        descUV.Texture2D.MipSlice = 0;
        descUV.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        return device->CreateUnorderedAccessView(pTexture, &descUV, &ppUAVOut[1]);
    } else {
        ppUAVOut[0] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = descTex.Format;
        desc.Texture2D.MipSlice = 0;
        desc.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        return device->CreateUnorderedAccessView(pTexture, &desc, &ppUAVOut[0]);
    }
}


// shaders cource is in the end of this file
extern const char* cs_nv12_luma_source;
extern const char* cs_nv12_chroma_source;
extern const char* cs_nv12_Y_copy_source;
extern const char* cs_nv12_UV_copy_source;
extern const char* cs_nv12_UV_source;

// check if parameters have changed and re-create/update resources if needed
bool shared_input_batch_t::update_crop_scale(ID3D11Texture2D* src0, const shared_rect& crop, UINT dw, UINT dh) {
    if (src0==nullptr) return false; // nothing to process
    // check if constant buffer exists
    if (cs_cbuffer == nullptr) create_cbuffer(device, sizeof(crop_scale_t), &cs_param, &cs_cbuffer);
    if (cs_nv12_luma_shader == nullptr) create_cshader(device, cs_nv12_luma_source, &cs_nv12_luma_shader);
    if (cs_nv12_chroma_shader == nullptr) create_cshader(device, cs_nv12_chroma_source, &cs_nv12_chroma_shader);
    if (cs_nv12_Y_copy_shader == nullptr) create_cshader(device, cs_nv12_Y_copy_source, &cs_nv12_Y_copy_shader);
    if (cs_nv12_UV_copy_shader == nullptr) create_cshader(device, cs_nv12_UV_copy_source, &cs_nv12_UV_copy_shader);
    if (cs_nv12_UV_shader == nullptr) create_cshader(device, cs_nv12_UV_source, &cs_nv12_UV_shader);

    D3D11_TEXTURE2D_DESC desc{};
    src0->GetDesc(&desc);
    assert(desc.Format == DXGI_FORMAT_NV12 || 
        desc.Format == DXGI_FORMAT_P010 ||
        desc.Format == DXGI_FORMAT_P016); // this code doesn't check format change, shaders don't support other formats
    // update default values
    shared_rect ncrop = crop; // new crop
    ncrop.w = (ncrop.w == 0) ? desc.Width : ncrop.w;
    ncrop.h = (ncrop.h == 0) ? desc.Height : ncrop.h;
    dw = (dw == 0) ? ncrop.w : dw;
    dh = (dh == 0) ? ncrop.h : dh;

    bool dst_tex_changed = cs_param.dst.w != dw || cs_param.dst.h != dh; //TODO: add format check

    if (cs_param.src.w != desc.Width || cs_param.src.h != desc.Height ||
        cs_param.src.crop != ncrop || dst_tex_changed) {
        cs_param.set(desc.Width, desc.Height, ncrop, dw, dh);
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        update_cbuffer(ctx, cs_cbuffer, &cs_param, sizeof(cs_param));
        ctx->Release(); ctx = nullptr;
    }
    return dst_tex_changed;
}

// go over textures and make sure their parameters match
// cs_params, reset all textures not matching params
void shared_input_batch_t::check_size(shared_copy_mode_t mode_Y_UV) {
    for (unsigned int i = 0; i < num; i++) {
        shared_resource_t& resource = resources[i];
        if (!resource.empty()) {
            D3D11_TEXTURE2D_DESC desc{};
            resource.texture->GetDesc(&desc);
            // TODO: format check
            int dst_tex_h = mode_Y_UV==split_Y_U_V ? cs_param.dst.h + cs_param.dst.h / 2 : cs_param.dst.h;
            if (desc.Width != cs_param.dst.w || desc.Height != dst_tex_h) {
                resource.reset();
            }
        }
    }
}

unsigned int shared_input_batch_t::crop_scale(ID3D11Texture2D** src, unsigned int src_num,
    const shared_rect& crop, unsigned int dst_w, unsigned int dst_h,
    unsigned int start)
{
    if (src_num == 0 || start >= num) return 0;
    unsigned int cpy_num = std::min(num - start, src_num);

    // get device
    {
        ID3D11Device* dev = nullptr;
        src[0]->GetDevice(&dev);
        if (device == nullptr) {
            device = dev;
            device->AddRef();
        }
        if (device != dev) {
            dev->Release();
            std::cout << "Error: Device changed." << std::endl;
            return 0;
        }
        dev->Release();
        dev = nullptr;
    }

    bool tex_changed = update_crop_scale(src[0], crop, dst_w, dst_h);
    if(tex_changed) check_size(); // kill all non-matching textures

    if (sampler_linear == nullptr) {
        // linear sampler
        D3D11_SAMPLER_DESC linsdesc{};
        linsdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;//D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        linsdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        linsdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        linsdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

        HRESULT hs = device->CreateSamplerState(&linsdesc, &sampler_linear);
        (void)hs;
    }

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);

    y_uv_views.resize(cpy_num*2); // reserve space for 2 views/input texture
    lc_uaviews.resize(cpy_num * 2); // reserve space for 2 views/output texture

    for (unsigned int i = 0; i < cpy_num; i++) {
        shared_resource_t& resource = resources[start + i];
        if (resource.empty()) {
            ID3D11Texture2D* texture = nullptr;
            create_rwtexture(device, src[i], nullptr, &texture, cs_param.dst.w, cs_param.dst.h);
            resource.init(texture);
            if(texture!=nullptr) texture->Release();
        }

        y_uv_views[i*2] = nullptr; y_uv_views[i*2+1] = nullptr; 
        create_srv_nv12(device, src[i], &y_uv_views[i*2]);
        resource.lock(0);

        lc_uaviews[i * 2] = nullptr; lc_uaviews[i * 2 + 1] = nullptr;
        create_uav_rwtexture(device, resource.texture, &lc_uaviews[i*2], 2);             // y plane, uv plane

        ctx->CSSetShader(cs_nv12_luma_shader, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &y_uv_views[i*2]);
        ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i*2], nullptr);
        ctx->CSSetSamplers(0, 1, &sampler_linear);

        assert(cs_cbuffer != nullptr);
        ID3D11Buffer* ppCB[1] = { cs_cbuffer };
        ctx->CSSetConstantBuffers(0, 1, ppCB);

        ctx->Dispatch(cs_param.dst.w / 4, cs_param.dst.h / 4, 1);

        ctx->CSSetShader(cs_nv12_chroma_shader, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &y_uv_views[i*2+1]);
        ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i*2+1], nullptr);
        ctx->CSSetSamplers(0, 1, &sampler_linear);

        ctx->Dispatch(cs_param.dst.w / 8, cs_param.dst.h / 8, 1);

        // clean up
        ctx->CSSetShader(nullptr, nullptr, 0);

        ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, ppUAViewnullptr, nullptr);

        ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
        ctx->CSSetShaderResources(0, 2, ppSRVnullptr);

        ID3D11SamplerState* ppSSnullptr[1] = { nullptr };
        ctx->CSSetSamplers(0, 1, ppSSnullptr);

        ID3D11Buffer* ppCBnullptr[1] = { nullptr };
        ctx->CSSetConstantBuffers(0, 1, ppCBnullptr);

    }

    ctx->Flush();

    // finish query
    if (done_event == nullptr) {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->CreateQuery(&queryDesc, &done_event);
        assert(done_event != nullptr);
    }
    ctx->End(done_event);
    while (ctx->GetData(done_event, 0, 0, 0) == S_FALSE) {};

    // cleanup views when done with textures
    for (auto y_uv_view : y_uv_views) {
        if (y_uv_view != nullptr) y_uv_view->Release();
    }
    y_uv_views.resize(0);
    for(auto lc_uaview: lc_uaviews) {
        if(lc_uaview!=nullptr) lc_uaview->Release();
    }
    lc_uaviews.resize(0);

    for (unsigned int i = 0; i < cpy_num; i++) {
        shared_resource_t& resource = resources[start + i];
        resource.unlock(0); // super important for results to be available via L0 ptr
        resource.lock(0);
    }
    if (ctx != nullptr) ctx->Release();
    return cpy_num;
}

// copy textures using dx11 into shared "staging" textures
// there is no size checking for non-empty dst textures
// all textures are assumed to belong to the same device
// the call will wait until copies finished
unsigned int shared_input_batch_t::copy_Y_UV(ID3D11Texture2D** src, unsigned int src_num, shared_copy_mode_t mode, unsigned int start) {
    
    if (mode == source_Y_UV) return copy(src, src_num, start); // copy NV12->NV12
    // mode: split_Y, split_Y_U_V
    if (src_num == 0 || start >= num) return 0;
    unsigned int cpy_num = std::min(num - start, src_num);
    if (cpy_num == 0) return 0;
    // get device
    {
        ID3D11Device* dev = nullptr;
        src[0]->GetDevice(&dev);
        if (device == nullptr) {
            device = dev;
            device->AddRef();
        }
        if (device != dev) {
            dev->Release();
            std::cout << "Error: Device changed." << std::endl;
            return 0;
        }
        dev->Release();
        dev = nullptr;

    }

    bool tex_changed = update_crop_scale(src[0], { 0, 0, 0, 0 }, 0, 0);
    if (tex_changed) check_size(mode); // kill all non-matching textures

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);

    y_uv_views.resize(cpy_num * 2);
    lc_uaviews.resize(cpy_num);
    for (unsigned int i = 0; i < cpy_num; i++) {
        assert(same_device_texture(device, src[i]));
        shared_resource_t& resource = resources[start + i];
        if (resource.empty()) {
            ID3D11Texture2D* copy = nullptr;
            create_rwtexture(device, src[i], mode, nullptr, &copy, 0, 0);
            if (copy == nullptr) {
                printf("Failed to create gpu copy resource.");
            }
            resource.init(copy); // shared texture, addref inside
            copy->Release();
        }

        y_uv_views[i * 2] = nullptr; y_uv_views[i * 2+1]=nullptr;
        create_srv_nv12(device, src[i], &y_uv_views[i*2]);
        resource.lock(0);

        lc_uaviews[i] = nullptr;
        create_uav_rwtexture(device, resource.texture, &lc_uaviews[i], 1);             // y_uv 1-channel
        
        ctx->CSSetShader(cs_nv12_Y_copy_shader, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &y_uv_views[2*i]);
        ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i], nullptr);
        ctx->CSSetSamplers(0, 1, &sampler_linear);

        assert(cs_cbuffer != nullptr);
        ID3D11Buffer* ppCB[1] = { cs_cbuffer };
        ctx->CSSetConstantBuffers(0, 1, ppCB);

        ctx->Dispatch(cs_param.dst.w / 4, cs_param.dst.h / 4, 1);


        if (mode == split_Y_U_V) {// uv part
            ctx->CSSetShader(cs_nv12_UV_copy_shader, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &y_uv_views[i*2+1]);
            ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i], nullptr);
            ctx->CSSetSamplers(0, 1, &sampler_linear);

            ctx->Dispatch(cs_param.dst.w / 8, cs_param.dst.h / 8, 1);
        }

        // clean up
        ctx->CSSetShader(nullptr, nullptr, 0);

        ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, ppUAViewnullptr, nullptr);

        ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
        ctx->CSSetShaderResources(0, 2, ppSRVnullptr);

        ID3D11SamplerState* ppSSnullptr[1] = { nullptr };
        ctx->CSSetSamplers(0, 1, ppSSnullptr);

        ID3D11Buffer* ppCBnullptr[1] = { nullptr };
        ctx->CSSetConstantBuffers(0, 1, ppCBnullptr);

    }

    ctx->Flush();
    // finish query
    if (done_event == nullptr) {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->CreateQuery(&queryDesc, &done_event);
        assert(done_event != nullptr);
    }
    ctx->End(done_event);
    while (ctx->GetData(done_event, 0, 0, 0) == S_FALSE) {};

    // cleanup views when done with textures
    for (auto y_uv_view : y_uv_views) {
        if (y_uv_view != nullptr) y_uv_view->Release();
    }
    y_uv_views.resize(0);
    for (auto lc_uaview : lc_uaviews) {
        if (lc_uaview != nullptr) lc_uaview->Release();
    }
    lc_uaviews.resize(0);

    for (unsigned int i = 0; i < cpy_num; i++) {
        resources[i + start].unlock(0); // important for copy to be correct
        resources[i + start].lock(0);   // make it avaialbale to DX
    }
    ctx->Release();

    return cpy_num;
}

unsigned int shared_input_batch_t::crop_scale_Y_UV(ID3D11Texture2D** src, unsigned int src_num,
    const shared_rect& crop, unsigned int dst_w, unsigned int dst_h, shared_copy_mode_t mode,
    unsigned int start)
{
    if (mode == source_Y_UV) return crop_scale(src, src_num, crop, dst_w, dst_h, start); // NV12->NV12

    if (src_num == 0 || start >= num) return 0;
    unsigned int cpy_num = std::min(num - start, src_num);

    // get device
    {
        ID3D11Device* dev = nullptr;
        src[0]->GetDevice(&dev);
        if (device == nullptr) {
            device = dev;
            device->AddRef();
        }
        if (device != dev) {
            dev->Release();
            std::cout << "Error: Device changed." << std::endl;
            return 0;
        }
        dev->Release();
        dev = nullptr;
    }

    bool tex_changed = update_crop_scale(src[0], crop, dst_w, dst_h);
    if (tex_changed) check_size(mode); // kill all non-matching textures

    if (sampler_linear == nullptr) {
        // linear sampler
        D3D11_SAMPLER_DESC linsdesc{};
        linsdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;//D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        linsdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        linsdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        linsdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

        HRESULT hs = device->CreateSamplerState(&linsdesc, &sampler_linear);
        (void)hs;
    }

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);

    y_uv_views.resize(cpy_num * 2);
    lc_uaviews.resize(cpy_num);
    for (unsigned int i = 0; i < cpy_num; i++) {
        shared_resource_t& resource = resources[start + i];
        if (resource.empty()) {
            ID3D11Texture2D* texture = nullptr;
            create_rwtexture(device, src[i], mode, nullptr, &texture, cs_param.dst.w, cs_param.dst.h);
            resource.init(texture);
            if (texture != nullptr) texture->Release();
        }

        y_uv_views[i * 2] = nullptr; y_uv_views[i * 2+1] = nullptr;
        create_srv_nv12(device, src[i], &y_uv_views[i*2]);
        resource.lock(0);

        lc_uaviews[i] = nullptr;
        create_uav_rwtexture(device, resource.texture, &lc_uaviews[i], 1);             // 1-channel y_uv

        ctx->CSSetShader(cs_nv12_luma_shader, nullptr, 0);  // use scaling/croping version for Y
        ctx->CSSetShaderResources(0, 1, &y_uv_views[i*2]);
        ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i], nullptr);
        ctx->CSSetSamplers(0, 1, &sampler_linear);

        assert(cs_cbuffer != nullptr);
        ID3D11Buffer* ppCB[1] = { cs_cbuffer };
        ctx->CSSetConstantBuffers(0, 1, ppCB);

        ctx->Dispatch(cs_param.dst.w / 4, cs_param.dst.h / 4, 1);
        if (mode == split_Y_U_V) { // uv part
            ctx->CSSetShader(cs_nv12_UV_shader, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &y_uv_views[i*2+1]);
            ctx->CSSetUnorderedAccessViews(0, 1, &lc_uaviews[i], nullptr);
            ctx->CSSetSamplers(0, 1, &sampler_linear);

            ctx->Dispatch(cs_param.dst.w / 8, cs_param.dst.h / 8, 1);
        }
        // clean up
        ctx->CSSetShader(nullptr, nullptr, 0);

        ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
        ctx->CSSetUnorderedAccessViews(0, 1, ppUAViewnullptr, nullptr);

        ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
        ctx->CSSetShaderResources(0, 2, ppSRVnullptr);

        ID3D11SamplerState* ppSSnullptr[1] = { nullptr };
        ctx->CSSetSamplers(0, 1, ppSSnullptr);

        ID3D11Buffer* ppCBnullptr[1] = { nullptr };
        ctx->CSSetConstantBuffers(0, 1, ppCBnullptr);

    }

    ctx->Flush();

    // finish query
    if (done_event == nullptr) {
        D3D11_QUERY_DESC queryDesc = { D3D11_QUERY_EVENT, 0 };
        device->CreateQuery(&queryDesc, &done_event);
        assert(done_event != nullptr);
    }
    ctx->End(done_event);
    while (ctx->GetData(done_event, 0, 0, 0) == S_FALSE) {};

    // cleanup views when done with textures
    for (auto y_uv_view : y_uv_views) {
        if (y_uv_view != nullptr) y_uv_view->Release();
    }
    y_uv_views.resize(0);
    for (auto lc_uaview : lc_uaviews) {
        if (lc_uaview != nullptr) lc_uaview->Release();
    }
    lc_uaviews.resize(0);

    for (unsigned int i = 0; i < cpy_num; i++) {
        shared_resource_t& resource = resources[start + i];
        resource.unlock(0); // super important for results to be available via L0 ptr
        resource.lock(0);
    }
    if (ctx != nullptr) ctx->Release();
    return cpy_num;
}


///////////////////////////////////
// 
// Shaders source code

const char* cs_nv12_luma_source = R"###(

Texture2D<float>  luminanceChannel   : t0;
SamplerState      samplerLinear     : s0
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};
cbuffer tex_dim : register(b0)
{
        int     src_w;      //src width
        int     src_h;     // src height
        int		src_crop_x;
		int		src_crop_y;
		int		src_crop_w;
		int		src_crop_h;
        int     dst_w;      //dst width
        int     dst_h;     // dst height
		float   sw_rcp;
		float   sh_rcp;
		float   dw_rcp;
		float   dh_rcp;

}

RWTexture2D<float> out_tex : register(u0);

[numthreads(4,4,1)]
void main(uint3 i:SV_DispatchThreadID)
{
    if(i.x<dst_w && i.y<dst_h) {
	    // normalized dst coord
	    float2 nd = (float2(i.x, i.y)+float2(0.5,0.5))/float2(dst_w, dst_h) ;//*float2(dw_rcp, dh_rcp);
	    // non-normalized source coordinate
	    float2 s=nd*float2(src_crop_w, src_crop_h) + float2(src_crop_x, src_crop_y);
	    float2 ns = s/float2(src_w, src_h);		// normalized source coordinate
	    float y=luminanceChannel.SampleLevel(samplerLinear, ns, 0); // read luminance value
	    out_tex[i.xy]=y; // write sampled luma
    }
}
)###";

const char* cs_nv12_chroma_source = R"###(

Texture2D<float2> chrominanceChannel : t0;
SamplerState      samplerLinear     : s0
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};
cbuffer tex_dim : register(b0)
{
        int     src_w;      //src width
        int     src_h;     // src height
        int		src_crop_x;
		int		src_crop_y;
		int		src_crop_w;
		int		src_crop_h;
        int     dst_w;      //dst width
        int     dst_h;     // dst height
		float   sw_rcp;
		float   sh_rcp;
		float   dw_rcp;
		float   dh_rcp;

}

RWTexture2D<float2> out_buffer : register(u0);

[numthreads(4,4,1)]
void main(uint3 i:SV_DispatchThreadID)
{
    if(i.x*2<dst_w && i.y*2<dst_h) {
	    // note that input coordinates here are in half-size UV channel
	    // but dimensions in CB are for full-size Y channel
	    // so we convert half-size dest coords into full-size
	    float2 nd = (float2(i.x*2, i.y*2)+float2(1,1))/float2(dst_w, dst_h) ;//*float2(dw_rcp, dh_rcp);
	    // non-normalized full-size source coordinate
	    float2 s=nd*float2(src_crop_w, src_crop_h) + float2(src_crop_x, src_crop_y);
	    float2 ns = s/float2(src_w, src_h);	//*float2(sw_rcp, sh_rcp);	// normalized source coordinate
	    float2 uv=chrominanceChannel.SampleLevel(samplerLinear, ns, 0);
	    out_buffer[i.xy]=uv;
    }
}
)###";

// shaders for Y, U, V splitting
const char* cs_nv12_Y_copy_source = R"###(

Texture2D<float>  luminanceChannel   : t0;
RWTexture2D<float> out_tex : register(u0);
cbuffer tex_dim : register(b0)
{
        int     src_w;      //src width (original Y size)
        int     src_h;     // src height 
        int		src_crop_x;
		int		src_crop_y;
		int		src_crop_w;
		int		src_crop_h;
        int     dst_w;      //dst width (chroma size)
        int     dst_h;     // dst height
		float   sw_rcp;
		float   sh_rcp;
		float   dw_rcp;
		float   dh_rcp;

}

[numthreads(4,4,1)]
void main(uint3 i:SV_DispatchThreadID)
{
    if(i.x<src_w && i.y<src_h) {
        float y = luminanceChannel.Load(uint3(i.x, i.y, 0));
	    out_tex[i.xy]=y; // write sampled luma
    }
}
)###";

const char* cs_nv12_UV_copy_source = R"###(

Texture2D<float2>  chrominanceChannel   : t0;
RWTexture2D<float> out_tex : register(u0);
cbuffer tex_dim : register(b0)
{
        int     src_w;      //src width (original Y size)
        int     src_h;     // src height 
        int		src_crop_x;
		int		src_crop_y;
		int		src_crop_w;
		int		src_crop_h;
        int     dst_w;      //dst width (chroma size)
        int     dst_h;     // dst height
		float   sw_rcp;
		float   sh_rcp;
		float   dw_rcp;
		float   dh_rcp;

}

[numthreads(4,4,1)]
void main(uint3 i:SV_DispatchThreadID)
{
    uint uv_w = src_w/2;
    if(i.x<uv_w && i.y*2<src_h) {
        float2 uv = chrominanceChannel.Load(uint3(i.x, i.y, 0));
        uint2 o = uint2(i.x, i.y+src_h);
	    out_tex[o]=uv.r; // write sampled luma
        out_tex[uint2(o.x+uv_w, o.y)]=uv.g;
    }
}
)###";

// crop scale UV extraction and splitting
const char* cs_nv12_UV_source = R"###(

Texture2D<float2> chrominanceChannel : t0;
SamplerState      samplerLinear     : s0
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};
cbuffer tex_dim : register(b0)
{
        int     src_w;      //src width
        int     src_h;     // src height
        int		src_crop_x;
		int		src_crop_y;
		int		src_crop_w;
		int		src_crop_h;
        int     dst_w;      //dst width
        int     dst_h;     // dst height
		float   sw_rcp;
		float   sh_rcp;
		float   dw_rcp;
		float   dh_rcp;

}

RWTexture2D<float2> out_buffer : register(u0);

[numthreads(4,4,1)]
void main(uint3 i:SV_DispatchThreadID)
{
    if(i.x*2<dst_w && i.y*2<dst_h) {
	    // note that input coordinates here are in half-size UV channel
	    // but dimensions in CB are for full-size Y channel
	    // so we convert half-size dest coords into full-size
	    float2 nd = (float2(i.x*2, i.y*2)+float2(1,1))/float2(dst_w, dst_h) ;//*float2(dw_rcp, dh_rcp);
	    // non-normalized full-size source coordinate
	    float2 s=nd*float2(src_crop_w, src_crop_h) + float2(src_crop_x, src_crop_y);
	    float2 ns = s/float2(src_w, src_h);	//*float2(sw_rcp, sh_rcp);	// normalized source coordinate
	    float2 uv=chrominanceChannel.SampleLevel(samplerLinear, ns, 0);
        uint2 o = uint2(i.x, i.y+dst_h);
	    out_buffer[o]=uv.r;
        out_buffer[uint2(i.x+dst_w, o.y)]=uv.g;
    }
}
)###";


