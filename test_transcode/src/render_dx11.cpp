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
RenderDX11::vshader RenderDX11::create_vs(char* code) {
    ID3D11VertexShader* pvs = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "vs_5_0", shader_flags, 0, &blob, NULL);
    hr = dxdevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pvs);
    blob->Release(); blob = nullptr;
    vshader vs(pvs, false);
    return vs;
}

RenderDX11::pshader RenderDX11::create_ps(char* code) {
    ID3D11PixelShader* pps = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "ps_5_0", shader_flags, 0, &blob, NULL);
    hr = dxdevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pps);
    blob->Release(); blob = nullptr;
    pshader ps(pps, false);
    return ps;
}

RenderDX11::cshader RenderDX11::create_cs(char* code) {
    ID3D11ComputeShader* pcs = nullptr;
    // compile and create shaders
    HRESULT hr = RenderDX11::CreateCS(code, &pcs);
    cshader cs(pcs, false);
    return cs;
}

void print_errors(ID3D10Blob* blob_err) {
    if (blob_err == nullptr) return;
    std::string err((char*)blob_err->GetBufferPointer(), blob_err->GetBufferSize());
    std::cout << "Shader compiler errors:" << std::endl << err << std::endl;
}

HRESULT RenderDX11::CreateCS(char* code, ID3D11ComputeShader* *ppCS) {
    *ppCS = nullptr;
    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    ID3D10Blob* blob_err = nullptr;
    HRESULT hr = ::D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "cs_5_0", shader_flags, 0, &blob, &blob_err);
    if (blob_err != nullptr) {
        if (hr != S_OK) print_errors(blob_err);
        blob_err->Release(); blob_err = nullptr;
    }
    hr = dxdevice->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, ppCS);
    blob->Release(); blob = nullptr;
    return hr;
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
        UINT elemSize = (desc.Format = DXGI_FORMAT_NV12) ? 1 : 4; //ATTN: either 1 byte or 4 per texel
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

void update_cb(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, void* pData, UINT dataBytes) {
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
    memcpy(MappedResource.pData, pData, dataBytes);
    ctx->Unmap(cb, 0);
}

/// Create y and uv resource views from NV12 texture
bool RenderDX11::create_srv_nv12(ID3D11Device* device, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]) {
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
        return false;
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
    return hr==S_OK;
}

void RenderDX11::init(ID3D11Device* dxdev, HWND window, PS_LFCALIB_DATA& calib, PS_LFQUILT_DATA& quilt) {
    // TODO: check if hs==S_OK and call reset if not
    HRESULT hs;

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
    UINT w = WindowRect.right - WindowRect.left;
    UINT h = WindowRect.bottom - WindowRect.top;



    // Create swapchain for window
    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    RtlZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));
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

    // Create a render target view
    hr = dxdev->CreateRenderTargetView(BackBuffer, nullptr, &view);
    BackBuffer->Release();
    if (FAILED(hr))
    {
        std::cout << "Failed to create render target view" << std::endl;
    }



    //setup viewport
    viewport.Width = (FLOAT)w;
    viewport.Height = (FLOAT)h;
    viewport.MaxDepth = D3D11_MAX_DEPTH; // needs to be set

    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;  // ;// 
    ID3D10Blob* blob = nullptr;
    hs = ::D3DCompile(NV12_LF_VS_source, strlen(NV12_LF_VS_source), NULL, NULL, NULL, "main", "vs_5_0", shader_flags, 0, &blob, NULL);
    hs = dxdev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &NV12_VS);
    blob->Release(); blob = nullptr;

    hs = ::D3DCompile(NV12_LF_PS_source, strlen(NV12_LF_PS_source), NULL, NULL, NULL, "main", "ps_5_0", shader_flags, 0, &blob, NULL);
    hs = dxdev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &NV12_PS);
    blob->Release(); blob = nullptr;

    // Prepare 2 constant buffers with shader parameters
    // Fill in a buffer description.
    D3D11_BUFFER_DESC cbDesc;
    cbDesc.ByteWidth = sizeof(PS_LFCALIB_DATA);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    // Fill in the subresource data.
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = &calib;
    InitData.SysMemPitch = 0;
    InitData.SysMemSlicePitch = 0;

    // Create the buffer.
    hr = dxdev->CreateBuffer(&cbDesc, &InitData,
        &NV12_PS_cbuf[0]);

    cbDesc.ByteWidth = sizeof(PS_LFQUILT_DATA);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.MiscFlags = 0;
    cbDesc.StructureByteStride = 0;

    // Fill in the subresource data.
    InitData.pSysMem = &quilt;
    InitData.SysMemPitch = 0;
    InitData.SysMemSlicePitch = 0;

    hr = dxdev->CreateBuffer(&cbDesc, &InitData,
        &NV12_PS_cbuf[1]);


    // linear sampler
    D3D11_SAMPLER_DESC sdesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    sdesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;//D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sdesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sdesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

    hs = dxdev->CreateSamplerState(&sdesc, &sampler_linear);

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

    dxdevice = dxdev;
}

bool RenderDX11::update_quilt_parameters(PS_LFQUILT_DATA& quilt) {
    if (dxdevice == nullptr) return false;
    ID3D11DeviceContext* ctx = nullptr;
    dxdevice->GetImmediateContext(&ctx);
    bool res = false;
    if (NV12_PS_cbuf[1] && ctx) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (SUCCEEDED(ctx->Map(NV12_PS_cbuf[1], 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
            memcpy(mappedResource.pData, &quilt, sizeof(PS_LFQUILT_DATA));
            ctx->Unmap(NV12_PS_cbuf[1], 0);
            res = true;
        }
        else std::cout << "Failed to map shader buffer: quilt." << std::endl;
    }
    crel(ctx);
    return res;
}

// actual rendering
bool RenderDX11::render_NV12(ID3D11Device* dxdev, ID3D11Texture2D* nv12) {
    bool res = true;
    if (dxdevice != dxdev) {
        dxdevice = dxdev;
        std::cout << "DX11 device has changed." << std::endl;
    }
    ID3D11DeviceContext* ctx = nullptr;
    dxdev->GetImmediateContext(&ctx);

    // got new input texture, map it to shader views
    create_srv_nv12(dxdev, nv12, y_uv_view);

    // set viewport
    ctx->RSSetViewports(1, &viewport);

    // set shaders
    ctx->VSSetShader(NV12_VS, nullptr, 0);
    ctx->PSSetShader(NV12_PS, nullptr, 0);
    
    // pass PS parameters
    ctx->PSSetConstantBuffers(0, 2, NV12_PS_cbuf);
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
    ctx->OMSetRenderTargets(1, &view, nullptr); // set output merger RT

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


RenderDX11::~RenderDX11() {
    reset();
}

