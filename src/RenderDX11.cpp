//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================

///
/// A fullscreen quad renderer for NV12 texture
/// using shaders from NV12shaders.cpp
/// NV12pixel.hlsl and NV12vertex.hlsl are copies of shaders from .cpp file
/// to check for bugs at compile time
///

#include "RenderDX11.hpp"
#include <iostream>
#include <vector>
#include <d3dcompiler.h>
#include"NV12LFshaders.hpp"
#include <d3d11_3.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// DX11Render structures

void print_errors(ID3D10Blob* blob_err) {
    std::string err = "Unknown error.";
    if (blob_err != nullptr) err = std::string((char*)blob_err->GetBufferPointer(), blob_err->GetBufferSize());
    std::cout << "Shader compiler errors:" << std::endl << err << std::endl;
}

ID3D11VertexShader* RenderDX11::create_vs(const char* code) {
    ID3D11VertexShader* pVS = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    ID3D10Blob* blob_err = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "vs_5_0", shader_flags, 0, &blob, &blob_err);
    if (hr != S_OK) {
        print_errors(blob_err); crel(blob_err);
        return nullptr;
    }
    hr = dxdevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pVS);
    blob->Release(); blob = nullptr;
    return pVS;

}

ID3D11PixelShader* RenderDX11::create_ps(const char* code) {
    ID3D11PixelShader* pPS = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    ID3D10Blob* blob_err = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "ps_5_0", shader_flags, 0, &blob, &blob_err);
    if (hr != S_OK) {
        print_errors(blob_err); crel(blob_err);
        return nullptr;
    }
    hr = dxdevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pPS);
    blob->Release(); blob = nullptr;
    return pPS;
}

ID3D11ComputeShader* RenderDX11::create_cs(const char* code) {
    ID3D11ComputeShader*  pCS = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    ID3D10Blob* blob_err = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "cs_5_0", shader_flags, 0, &blob, &blob_err);
    if (hr!=S_OK) {
       print_errors(blob_err); crel(blob_err);
       return nullptr;
    }
    hr = dxdevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pCS);
    blob->Release(); blob = nullptr;
    return pCS;
}

ID3D11Buffer* RenderDX11::create_cb(unsigned int size, const void* data) {
    ID3D11Buffer* buffer = nullptr;
    D3D11_BUFFER_DESC cbDesc;
    cbDesc.ByteWidth = size;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    // Fill in the subresource data.
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = data;
    InitData.SysMemPitch = 0;
    InitData.SysMemSlicePitch = 0;

    // Create the buffer.
    HRESULT hr = dxdevice->CreateBuffer(&cbDesc, 
        data==nullptr? nullptr: &InitData,
        &buffer);
    if (hr != S_OK) return nullptr;

    return buffer;
}

void RenderDX11::create_shaders() {
    FS_QUAD_VS = create_vs(FS_QUAD_VS_SRC);
    LF_NV12_PS[mono] = create_ps(MONO_NV12_PS_SRC);
    LF_PS[mono] = create_ps(MONO_PS_SRC);
    // create shader parameter cbuffers
    lf_parameters_render lf_params = {};
    LF_PS_cbuf = create_cb(sizeof(lf_parameters_render), &lf_params);
}

//--------------------------------------------------------------------------------------
// Create Structured Buffer
//--------------------------------------------------------------------------------------
HRESULT RenderDX11::CreateStructuredBuffer(UINT uElementSize, UINT uCount, void* pInitData, ID3D11Buffer** ppBufOut)
{
    *ppBufOut = nullptr;

    D3D11_BUFFER_DESC desc = {};
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.ByteWidth = uElementSize * uCount;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;// | D3D11_RESOURCE_MISC_SHARED_NTHANDLE; //| D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    desc.StructureByteStride = uElementSize;

    if (pInitData)
    {
        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pInitData;
        return dxdevice->CreateBuffer(&desc, &InitData, ppBufOut);
    }
    else
        return dxdevice->CreateBuffer(&desc, nullptr, ppBufOut);
}

//--------------------------------------------------------------------------------------
// Create Raw Buffer
//--------------------------------------------------------------------------------------
HRESULT RenderDX11::CreateRawBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut)
{
    *ppBufOut = nullptr;

    D3D11_BUFFER_DESC desc = {};
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = uSize;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;// | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    if (pInitData)
    {
        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pInitData;
        return dxdevice->CreateBuffer(&desc, &InitData, ppBufOut);
    }
    else
        return dxdevice->CreateBuffer(&desc, nullptr, ppBufOut);
}

HRESULT RenderDX11::CreateRWTexture(UINT width, UINT height, void* pInitData, ID3D11Texture2D** ppTexOut) {
    *ppTexOut = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;// | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    desc.CPUAccessFlags = 0;
    desc.Format = DXGI_FORMAT_R32_UINT; // Change this to what you need
    desc.Height = height;
    desc.Width = width;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (pInitData) {
        D3D11_SUBRESOURCE_DATA InitData{};
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = width * sizeof(UINT); //ATTN: 4
        return dxdevice->CreateTexture2D(&desc, &InitData, ppTexOut);
    } else
        return dxdevice->CreateTexture2D(&desc, nullptr, ppTexOut);

}

HRESULT RenderDX11::CreateRWTexture(ID3D11Texture2D* asTex, void* pInitData, ID3D11Texture2D** ppTexOut, UINT newWidth, UINT newHeight) {
    *ppTexOut = nullptr;
    D3D11_TEXTURE2D_DESC asDesc{};
    asTex->GetDesc(&asDesc);

    D3D11_TEXTURE2D_DESC desc{};
    desc.ArraySize = 1;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;// | D3D11_RESOURCE_MISC_SHARED;// | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//
    desc.CPUAccessFlags = 0;
    desc.Format = asDesc.Format; // Change this to what you need
    desc.Height = (newHeight==0)? asDesc.Height : newHeight;
    desc.Width = (newWidth==0)? asDesc.Width : newWidth;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;

    if (pInitData) {
        UINT elemSize = (desc.Format == DXGI_FORMAT_NV12) ? 1u : 4u; //ATTN: either 1 byte or 4 per texel
        D3D11_SUBRESOURCE_DATA InitData{};
        InitData.pSysMem = pInitData;
        InitData.SysMemPitch = desc.Width * elemSize; //ATTN: elemSize
        return dxdevice->CreateTexture2D(&desc, &InitData, ppTexOut);
    }
    else
        return dxdevice->CreateTexture2D(&desc, nullptr, ppTexOut);

}


HRESULT RenderDX11::CreateConstBuffer(UINT uSize, void* pInitData, ID3D11Buffer** ppBufOut) {
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
        return dxdevice->CreateBuffer(&cbDesc, &InitData, ppBufOut);
    } else
        return dxdevice->CreateBuffer(&cbDesc, nullptr, ppBufOut);

}

HRESULT RenderDX11::CreateBufferSRV(ID3D11Buffer* pBuffer, ID3D11ShaderResourceView** ppSRVOut, UINT firstElement, UINT numElements)
{
    D3D11_BUFFER_DESC descBuf = {};
    pBuffer->GetDesc(&descBuf);

    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.FirstElement = 0;

    if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
    {
        // This is a Raw Buffer, firstElement and numElements must be always 4-byte aligned or it won't work

        desc.Format = DXGI_FORMAT_R32_TYPELESS;
        desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        desc.BufferEx.NumElements = (numElements==0)? descBuf.ByteWidth / 4 : numElements/4;
        desc.BufferEx.FirstElement = firstElement/4;
    }
    else if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
    {
        // This is a Structured Buffer
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.BufferEx.NumElements = (numElements==0)? descBuf.ByteWidth / descBuf.StructureByteStride : numElements;
        desc.BufferEx.FirstElement = firstElement;
    }
    else
    {
        return E_INVALIDARG;
    }

    return dxdevice->CreateShaderResourceView(pBuffer, &desc, ppSRVOut);
}

//--------------------------------------------------------------------------------------
// Create Unordered Access View for Structured or Raw Buffers
//-------------------------------------------------------------------------------------- 
HRESULT RenderDX11::CreateBufferUAV(ID3D11Buffer* pBuffer, ID3D11UnorderedAccessView** ppUAVOut, UINT firstElement, UINT numElements)
{
    D3D11_BUFFER_DESC descBuf = {};
    pBuffer->GetDesc(&descBuf);

    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = 0;

    if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
    {
        // This is a Raw Buffer

        desc.Format = DXGI_FORMAT_R32_TYPELESS; // Format must be DXGI_FORMAT_R32_TYPELESS, when creating Raw Unordered Access View
        desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        desc.Buffer.NumElements = (numElements == 0) ? descBuf.ByteWidth / 4 : numElements / 4;
        desc.Buffer.FirstElement = firstElement / 4;
    }
    else if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
    {
        // This is a Structured Buffer

        desc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
        desc.Buffer.NumElements = (numElements == 0) ? descBuf.ByteWidth / descBuf.StructureByteStride : numElements;
        desc.Buffer.FirstElement = firstElement;
    }
    else
    {
        return E_INVALIDARG;
    }

    return dxdevice->CreateUnorderedAccessView(pBuffer, &desc, ppUAVOut);
}

HRESULT RenderDX11::CreateRWTextureUAV(ID3D11Texture2D* pTexture, ID3D11UnorderedAccessView** ppUAVOut, UINT numUAV) {
    if (numUAV < 1) return S_OK;
    D3D11_TEXTURE2D_DESC descTex;
    pTexture->GetDesc(&descTex);
    if (descTex.Format != DXGI_FORMAT_NV12) {
        ppUAVOut[0] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = descTex.Format;
        desc.Texture2D.MipSlice = 0;
        desc.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        return dxdevice->CreateUnorderedAccessView(pTexture, &desc, &ppUAVOut[0]);
    } else {
        ppUAVOut[0] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC descY{};
        descY.Format = DXGI_FORMAT_R8_UNORM;
        descY.Texture2D.MipSlice = 0;
        descY.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        HRESULT hr = dxdevice->CreateUnorderedAccessView(pTexture, &descY, &ppUAVOut[0]);
        if (numUAV < 2 || hr != S_OK) return hr;
        ppUAVOut[1] = nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC descUV{};
        descUV.Format = DXGI_FORMAT_R8G8_UNORM;
        descUV.Texture2D.MipSlice = 0;
        descUV.ViewDimension = D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D;
        return dxdevice->CreateUnorderedAccessView(pTexture, &descUV, &ppUAVOut[1]);
    }
}

//#define MTX_RT
ID3D11Texture2D* RenderDX11::create_texture(ID3D11Device* dxdev, int w, int h) {
    D3D11_TEXTURE2D_DESC desc;
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;//DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
#ifndef MTX_RT
    desc.MiscFlags = 0; // ATTN: D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX; //
#else
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX; // | D3D11_RESOURCE_MISC_SHARED;// 
#endif
    ID3D11Texture2D* pTexture = nullptr;
    dxdev->CreateTexture2D(&desc, NULL, &pTexture);
    return pTexture;
}

IDXGIKeyedMutex* RenderDX11::acq_mtx(ID3D11Texture2D* tex) {
    IDXGIKeyedMutex* pDXGIKeyedMutex = nullptr;
    // Acquire a reference to the keyed mutex.
    tex->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**) & pDXGIKeyedMutex);
    if (pDXGIKeyedMutex == nullptr) return nullptr;
    // Acquire a lock to the resource.
    pDXGIKeyedMutex->AcquireSync(0, INFINITE);
    return pDXGIKeyedMutex;
}

void RenderDX11::release_mtx(IDXGIKeyedMutex* pDXGIKeyedMutex) {
    if (pDXGIKeyedMutex == nullptr) return;
    // Release the lock and specify a key.
    pDXGIKeyedMutex->ReleaseSync(0);
    pDXGIKeyedMutex->Release();
}

bool update_cb(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const void* pData, UINT dataBytes) {
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource))) {
        memcpy(MappedResource.pData, pData, dataBytes);
        ctx->Unmap(cb, 0);
        return true;
    }
    return false;
}

/// Create y and uv resource views from NV12 texture
bool RenderDX11::create_srv_nv12(ID3D11Device* dxdev, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]) {
    // DirectX specifies the view format to be DXGI_FORMAT_R8_UNORM for NV12 luminance channel.
    // Luminance is 8 bits per pixel. DirectX will handle converting 8-bit integers into normalized
    // floats for use in the shader.
    D3D11_SHADER_RESOURCE_VIEW_DESC const luminancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
        nv12,
        D3D11_SRV_DIMENSION_TEXTURE2D,
        DXGI_FORMAT_R8_UNORM
    );

    HRESULT hr = dxdev->CreateShaderResourceView(
        nv12,
        &luminancePlaneDesc,
        &y_uv[0]
    );
    if (FAILED(hr))
    {
        std::cout << "Failed to create texture SRV" << "Error " << hr << std::endl;
        return false;
    }

    // DirectX specifies the view format to be DXGI_FORMAT_R8G8_UNORM for NV12 chrominance channel.
    // Chrominance has 8 bits for U and 8 bits for V per pixel, but with 1/2 resolution in each dimension.
    // DirectX will handle converting 8-bit integers into normalized floats for use in the shader.
    D3D11_SHADER_RESOURCE_VIEW_DESC const chrominancePlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
        nv12,
        D3D11_SRV_DIMENSION_TEXTURE2D,
        DXGI_FORMAT_R8G8_UNORM
    );

    hr = dxdev->CreateShaderResourceView(
        nv12,
        &chrominancePlaneDesc,
        &y_uv[1]
    );
    if (FAILED(hr))
    {
        std::cout << "Failed to create texture SRV" << "Error " << hr << std::endl;
        return false;
    }
    return true;
}

bool RenderDX11::create_srv(ID3D11Device* dxdev, ID3D11Texture2D* texture, ID3D11ShaderResourceView** view) {
    D3D11_SHADER_RESOURCE_VIEW_DESC const colorPlaneDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(
        texture,
        D3D11_SRV_DIMENSION_TEXTURE2D
    );

    HRESULT hr = dxdev->CreateShaderResourceView(
        texture,
        &colorPlaneDesc,
        view
    );
    if (FAILED(hr))
    {
        std::cout << "Failed to create texture SRV" << "Error " << hr << std::endl;
        *view = nullptr;
        return false;
    }
    return true;
}

void RenderDX11::init(ID3D11Device* dxdev, HWND window, lf_shader_type display, bool mode_sRGB) {
    if (dxdev == nullptr) return;
    display_shader = display;

    dxdevice = dxdev;   // TODO: ideally need to release existing dxdevice
    dxdevice->AddRef();
    // TODO: check if hs==S_OK and call reset if not
    //

    // Get DXGI factory
    IDXGIDevice* DxgiDevice = nullptr;
    HRESULT hr = dxdev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        std::cout << "Failed to QI for DXGI Device" <<std::endl;
        exit(-1);
    }

    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        std::cout << "Failed to get parent DXGI Adapter" << std::endl;
        exit(-1);
    }

    hr = DxgiAdapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        std::cout<<"Failed to get parent DXGI Factory1" << std::endl;
        exit(-1);
    }


    // Register for occlusion status windows message
    //hr = factory->RegisterOcclusionStatusWindow(window, OCCLUSION_STATUS_MSG, &OcclusionCookie);
    //if (FAILED(hr))
    //{
    //    return ProcessFailure(m_Device, L"Failed to register for occlusion message", L"Error", hr, SystemTransitionsExpectedErrors);
    //}

    // Get window size
    RECT WindowRect;
    GetClientRect(window, &WindowRect);
    UINT w = (UINT)(WindowRect.right - WindowRect.left);
    UINT h = (UINT)(WindowRect.bottom - WindowRect.top);

    // Create swapchain for window
    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
    // format
    SwapChainDesc.BufferDesc.Width = w;
    SwapChainDesc.BufferDesc.Height = h;
    //SwapChainDesc.BufferDesc.RefreshRate = ;
    SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    //SwapChainDesc.BufferDesc.ScanlineOrdering = ;
    //SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    SwapChainDesc.BufferCount = 2;
    SwapChainDesc.OutputWindow = window;
    SwapChainDesc.Windowed = TRUE;//
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.SampleDesc.Count = 1;
    SwapChainDesc.SampleDesc.Quality = 0;
    hr = factory->CreateSwapChain(dxdev, &SwapChainDesc, &swap_chain);
    if (FAILED(hr))
    {
        std::cout << "Failed to create window swapchain" << std::endl;
        exit(-1);
    }
    // Detect if newly created full-screen swap chain isn't actually full screen.
    IDXGIOutput* pTarget=nullptr; BOOL bFullscreen;
    if (SUCCEEDED(swap_chain->GetFullscreenState(&bFullscreen, &pTarget)))
    {
        if(pTarget) pTarget->Release();
    }
    else
        bFullscreen = FALSE;
    // If not full screen, enable full screen again.
 /*   if (!bFullscreen)
    {
        ShowWindow(window, SW_MINIMIZE);
        ShowWindow(window, SW_RESTORE);
        swap_chain->SetFullscreenState(TRUE, NULL);
    }
 */   // Disable the ALT-ENTER shortcut for entering full-screen mode
    //hr = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    //if (FAILED(hr))
    //{
    //    return ProcessFailure(m_Device, L"Failed to make window association", L"Error", hr, SystemTransitionsExpectedErrors);
    //}

    // render target texture 
    // can be pipelined
    //target = create_texture(dxdev, w, h);

    // create RTV of target texture to render to
    // Get backbuffer
    ID3D11Texture2D* BackBuffer = nullptr;
    hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&BackBuffer));
    if (FAILED(hr))
    {
        std::cout << "Failed to get backbuffer for making render target view" << std::endl;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = mode_sRGB? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.Texture2D.MipSlice = 0;
    RT_mode_sRGB = mode_sRGB;

    // Create a render target view
    if (BackBuffer != nullptr) {
        hr = dxdev->CreateRenderTargetView(BackBuffer, &rtvDesc, &rt_view);
        BackBuffer->Release();
    }
    if (FAILED(hr))
    {
        std::cout << "Failed to create render target view" << std::endl;
    }



    //setup viewport
    viewport.Width = (FLOAT)w;
    viewport.Height = (FLOAT)h;
    viewport.MaxDepth = D3D11_MAX_DEPTH; // needs to be set

    // compile and create shaders
    create_shaders();

    // linear sampler
    D3D11_SAMPLER_DESC sdesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    sdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;//D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

    HRESULT hs = dxdev->CreateSamplerState(&sdesc, &sampler_linear);

    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;//D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

    hs = dxdev->CreateSamplerState(&sdesc, &sampler_nearest);

    // create state with depth and stencil disabled
    D3D11_DEPTH_STENCIL_DESC dsdesc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT());
    dsdesc.DepthEnable = FALSE;
    dsdesc.StencilEnable = FALSE;
    dxdev->CreateDepthStencilState(&dsdesc, &disable_depth);

}


void RenderDX11::resize() {
    if (dxdevice == nullptr || swap_chain == nullptr) return;
    ID3D11DeviceContext* ctx = nullptr;
    dxdevice->GetImmediateContext(&ctx);
    
    ctx->OMSetRenderTargets(0, 0, 0);

    // Release all outstanding references to the swap chain's buffers.
    crel(rt_view);
    ctx->Flush();

    HRESULT hr;
    // Preserve the existing buffer count and format.
    // Automatically choose the width and height to match the client rect for HWNDs.
    hr = swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    // Perform error handling here!

    // Get buffer and create a render-target-view.
    ID3D11Texture2D* pBuffer = nullptr;
    hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
        (void**)&pBuffer);
    // Perform error handling here!

    hr = dxdevice->CreateRenderTargetView(pBuffer, NULL,
        &rt_view);
    // Perform error handling here!
    pBuffer->Release();

    // get new client area dims
    DXGI_SWAP_CHAIN_DESC sc_desc = {};
    hr = swap_chain->GetDesc(&sc_desc);
    unsigned int w = sc_desc.BufferDesc.Width;
    unsigned int h = sc_desc.BufferDesc.Height;

    // Set up the viewport.
    //setup viewport
    viewport.Width = (FLOAT)w;
    viewport.Height = (FLOAT)h;
    viewport.MaxDepth = D3D11_MAX_DEPTH; // needs to be set
    // TODO: check if hs==S_OK and call reset if not
}

void RenderDX11::update_lf_parameters(const lf_parameters_render& lf_params) {
    if (dxdevice == nullptr) return;
    ID3D11DeviceContext* ctx = nullptr;
    dxdevice->GetImmediateContext(&ctx);
    if (LF_PS_cbuf && ctx) {
        if(!update_cb(ctx, LF_PS_cbuf, &lf_params, sizeof(lf_parameters_render)))
            std::cout << "Failed to map shader buffer: lf parameters." << std::endl;
    }
    crel(ctx);
}

// actual rendering for NV12
bool RenderDX11::render_texture_planar(ID3D11Texture2D* nv12) {
    bool res = true;
    ID3D11DeviceContext* ctx = nullptr;
    dxdevice->GetImmediateContext(&ctx);

    // got new input texture, map it to shader views
    create_srv_nv12(dxdevice, nv12, y_uv_view);

    // set viewport
    ctx->RSSetViewports(1, &viewport);

    // set shaders
    ctx->VSSetShader(FS_QUAD_VS, nullptr, 0);
    ctx->PSSetShader(LF_NV12_PS[display_shader], nullptr, 0);
    
    // pass PS parameters
    ctx->PSSetConstantBuffers(0, 1, &LF_PS_cbuf);
    // Bind the NV12 channels to the shader.
    ctx->PSSetShaderResources(
        0, 2, y_uv_view);

    // bind linear sampler
    ctx->PSSetSamplers(0, 1, &sampler_linear);

#ifdef MTX_RT
    auto mtx = acq_mtx(target);
#endif

    // disable blending and z-tests
    FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    ctx->OMSetDepthStencilState(disable_depth, 0);
    ctx->OMSetRenderTargets(1, &rt_view, nullptr); // set output merger RT

    // prep for sending 4 vertex indices as a triangle strip
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); // stripped quad
    ctx->IASetVertexBuffers(0, 0, NULL, NULL, NULL);
    ctx->IASetInputLayout(NULL);

    // render
    ctx->Draw(4, 0);

    // cleanup
    ID3D11ShaderResourceView* SRVnullptr[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(0, 2, SRVnullptr);

    //ctx->Flush(); // trigger rendering, call IDXGISwapchain::Present(); for the onscreen
    HRESULT hr = swap_chain->Present(0, 0); // ATTN
    if (FAILED(hr))
    {
        std::cout << "Failed to present" << std::endl;
        res = false;
    }



#ifdef MTX_RT
    release_mtx(mtx);
#endif
    // no need to hold views into nv12 texture
    // it will change next frame
    // can clean y_uv_view, right?
    for (int i = 0; i < 2; i++) {
        y_uv_view[i]->Release();
        y_uv_view[i] = nullptr;
    }
    if(ctx!=nullptr) ctx->Release();
    return res;
}

// actual rendering for a single-plane
bool RenderDX11::render_texture(ID3D11Texture2D* texture) {
    bool res = true;
    //if (dxdevice != dxdev) {
    //    dxdevice = dxdev;
    //    std::cout << "DX11 device has changed." << std::endl;
    //}
    ID3D11DeviceContext* ctx = nullptr;
    dxdevice->GetImmediateContext(&ctx);

    // got new input texture, map it to shader views
    create_srv(dxdevice, texture, &y_uv_view[0]);

    // set viewport
    ctx->RSSetViewports(1, &viewport);

    // set shaders
    ctx->VSSetShader(FS_QUAD_VS, nullptr, 0);
    ctx->PSSetShader(LF_PS[display_shader], nullptr, 0);

    // pass PS parameters
    ctx->PSSetConstantBuffers(0, 1, &LF_PS_cbuf);
    // Bind the NV12 channels to the shader.
    ctx->PSSetShaderResources(
        0, 1, &y_uv_view[0]);

    // bind linear sampler
    ctx->PSSetSamplers(0, 1, &sampler_linear);

#ifdef MTX_RT
    auto mtx = acq_mtx(target);
#endif

    // disable blending and z-tests
    FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
    ctx->OMSetDepthStencilState(disable_depth, 0);
    ctx->OMSetRenderTargets(1, &rt_view, nullptr); // set output merger RT

    // prep for sending 4 vertex indices as a triangle strip
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); // stripped quad
    ctx->IASetVertexBuffers(0, 0, NULL, NULL, NULL);
    ctx->IASetInputLayout(NULL);

    // render
    ctx->Draw(4, 0);

    // cleanup
    ID3D11ShaderResourceView* SRVnullptr[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(0, 2, SRVnullptr);

    //ctx->Flush(); // trigger rendering, call IDXGISwapchain::Present(); for the onscreen
    HRESULT hr = swap_chain->Present(0, 0); // ATTN
    if (FAILED(hr))
    {
        std::cout << "Failed to present" << std::endl;
        res = false;
    }



#ifdef MTX_RT
    release_mtx(mtx);
#endif
    // no need to hold views into texture
    // it will change next frame
    // can clean y_uv_view, right?
    y_uv_view[0]->Release();
    y_uv_view[0] = nullptr;

    if (ctx != nullptr) ctx->Release();
    return res;
}


RenderDX11::~RenderDX11() {
    reset();
}

ID3D11Device* RenderDX11::create_device(unsigned int adapter_num) {
    ID3D11Device* dxdev = nullptr;
    // find suitable adapter
    IDXGIFactory1* pFactory;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return nullptr;

    }
    UINT i = 0;
    IDXGIAdapter* pAdapter;
    std::vector <IDXGIAdapter*> vAdapters;
    DXGI_ADAPTER_DESC adapter;
    while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        vAdapters.push_back(pAdapter);
        ++i;
        // here you want to enumerate outputs and find the display you want
        pAdapter->GetDesc(&adapter);
    }
    pFactory->Release();

    if (vAdapters.size() <= 0) {
        std::cout << "No adapters found!" << std::endl;
        return nullptr;
    }
    if (adapter_num >= vAdapters.size()) adapter_num = 0;

    D3D_FEATURE_LEVEL  FeatureLevelsRequested = D3D_FEATURE_LEVEL_11_0;
    UINT               numLevelsRequested = 1;
    D3D_FEATURE_LEVEL  FeatureLevelsSupported;
    ID3D11DeviceContext* dev_ctx = nullptr;

    if (FAILED(hr = D3D11CreateDevice(vAdapters[adapter_num], //NULL,
        D3D_DRIVER_TYPE_UNKNOWN, //D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0, //| D3D11_CREATE_DEVICE_DEBUG, // ATTN:  
        &FeatureLevelsRequested,
        numLevelsRequested,
        D3D11_SDK_VERSION,
        &dxdev,
        &FeatureLevelsSupported,
        &dev_ctx)))
    {
        std::cout << "Creating DX device failed!" << std::endl;
        return nullptr;
    }

    // set multithreading
    ID3D10Multithread* pD10Multithread = nullptr;

    dev_ctx->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pD10Multithread);
    //BOOL mt = 
    pD10Multithread->SetMultithreadProtected(true);
    return dxdev;
}


