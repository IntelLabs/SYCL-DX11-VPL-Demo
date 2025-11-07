//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

///
/// An offscreen quad renderer to convert NV12 texture to RGBA 32bpp texture
/// using shaders from NV12shaders.cpp
/// NV12pixel.hlsl and NV12vertex.hlsl are copies of shaders from .cpp file
/// to check for bugs at compile time
///

#include "OffscreenDX11.h"
#include <iostream>
#include <vector>
#include <d3dcompiler.h>


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

//#define MTX_RT
ID3D11Texture2D* create_texture(ID3D11Device* dxdev, int w, int h) {
    D3D11_TEXTURE2D_DESC desc;
    desc.Width = w;
    desc.Height = h;
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

IDXGIKeyedMutex* acq_mtx(ID3D11Texture2D* tex) {
    IDXGIKeyedMutex* pDXGIKeyedMutex = nullptr;
    // Acquire a reference to the keyed mutex.
    tex->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**) & pDXGIKeyedMutex);
    if (pDXGIKeyedMutex == nullptr) return nullptr;
    // Acquire a lock to the resource.
    pDXGIKeyedMutex->AcquireSync(0, INFINITE);
    return pDXGIKeyedMutex;
}

void release_mtx(IDXGIKeyedMutex* pDXGIKeyedMutex) {
    if (pDXGIKeyedMutex == nullptr) return;
    // Release the lock and specify a key.
    pDXGIKeyedMutex->ReleaseSync(0);
    pDXGIKeyedMutex->Release();
}

/// Create y and uv resource views from NV12 texture
bool create_srv_nv12(ID3D11Device* dxdev, ID3D11Texture2D* nv12, ID3D11ShaderResourceView* y_uv[2]) {
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
    // Chrominance has 4 bits for U and 4 bits for V per pixel. DirectX will handle converting 4-bit
    // integers into normalized floats for use in the shader.
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

/// From NV12shaders.cpp
extern char* NV12_PS_source;
extern char* NV12_VS_source;

void RenderOffscreen::init(ID3D11Device* dxdev) {
    reset();
    // TODO: check if hs==S_OK and call reset if not
    HRESULT hs;


    //setup viewport
    viewport.Width = (FLOAT)128;
    viewport.Height = (FLOAT)128;
    viewport.MaxDepth = D3D11_MAX_DEPTH; // needs to be set

    // compile and create shaders
    UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3; //; //
    ID3D10Blob* blob = nullptr;
    hs = ::D3DCompile(NV12_VS_source, strlen(NV12_VS_source), NULL, NULL, NULL, "main", "vs_5_0", shader_flags, 0, &blob, NULL);
    hs = dxdev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &NV12_VS);
    blob->Release(); blob = nullptr;

    hs = ::D3DCompile(NV12_PS_source, strlen(NV12_PS_source), NULL, NULL, NULL, "main", "ps_5_0", shader_flags, 0, &blob, NULL);
    hs = dxdev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &NV12_PS);
    blob->Release(); blob = nullptr;

    // linear sampler
    D3D11_SAMPLER_DESC sdesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    sdesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hs = dxdev->CreateSamplerState(&sdesc, &sampler_linear);

    // create state with depth and stencil disabled
    D3D11_DEPTH_STENCIL_DESC dsdesc = CD3D11_DEPTH_STENCIL_DESC(CD3D11_DEFAULT());
    dsdesc.DepthEnable = FALSE;
    dsdesc.StencilEnable = FALSE;
    dxdev->CreateDepthStencilState(&dsdesc, &disable_depth);

    device = dxdev;
    device->AddRef(); // keep reference to the device
}

// actual rendering
ID3D11Texture2D* RenderOffscreen::convert_NV12(ID3D11Texture2D* nv12) {
    if (nv12 == nullptr) return nullptr;
    ID3D11Device* dxdev = nullptr;
    nv12->GetDevice(&dxdev);
    if (device != dxdev) init(dxdev);
    dxdev->Release();
    D3D11_TEXTURE2D_DESC desc = {};
    nv12->GetDesc(&desc);
    ID3D11Texture2D* dst = create_texture(device, desc.Width, desc.Height);

    planar_from_NV12(dst, nv12);

    return dst;
}

void RenderOffscreen::convert_NV12(ID3D11Texture2D* dst, ID3D11Texture2D* nv12) {
    if (nv12 == nullptr) return;
    ID3D11Device* dxdev = nullptr;
    nv12->GetDevice(&dxdev);
    if (device != dxdev) init(dxdev);
    dxdev->Release();

    planar_from_NV12(dst, nv12);
}

// actual rendering
void RenderOffscreen::planar_from_NV12(ID3D11Texture2D* dst, ID3D11Texture2D* nv12) {
    D3D11_TEXTURE2D_DESC desc = {};
    dst->GetDesc(&desc);

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);

    // create RTV of target texture to render to
    device->CreateRenderTargetView(dst, nullptr, &view);

    // got new input texture, map it to shader views
    create_srv_nv12(device, nv12, y_uv_view);

    // set viewport
    viewport.Width = (FLOAT)desc.Width;
    viewport.Height = (FLOAT)desc.Height;
    ctx->RSSetViewports(1, &viewport);

    // set shaders
    ctx->VSSetShader(NV12_VS, nullptr, 0);
    ctx->PSSetShader(NV12_PS, nullptr, 0);
    
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

    ID3D11RenderTargetView* null_rtv[1] = { nullptr };
    ID3D11ShaderResourceView* null_srv[2] = { nullptr, nullptr };
    ctx->OMSetRenderTargets(1, null_rtv, nullptr); // set output merger RT
    ctx->PSSetShaderResources(0, 2, null_srv);

    ctx->Flush(); // trigger rendering, call IDXGISwapchain::Present(); for the onscreen

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
    view->Release(); view = nullptr;
}

RenderOffscreen::~RenderOffscreen() {
    reset();
}

