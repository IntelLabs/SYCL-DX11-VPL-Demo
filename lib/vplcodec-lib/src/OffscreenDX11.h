//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef OFFSCREENDX11_H
#define OFFSCREENDX11_H

#include <d3d11.h>
#include <memory>

///
/// An offscreen quad renderer to convert NV12 texture to RGBA 32bpp texture
/// using shaders from NV12shaders.cpp
/// NV12pixel.hlsl and NV12vertex.hlsl are copies of shaders from .cpp file
/// to check for bugs at compile time
///

/// <summary>
/// Create RGBA 32bpp texture of wxh dimensions
/// </summary>
/// <param name="dxdev">Device</param>
/// <param name="w">width</param>
/// <param name="h">height</param>
/// <returns>texture ptr if successful or nullptr otherwise</returns>
ID3D11Texture2D* create_texture(ID3D11Device* dxdev, int w, int h);

IDXGIKeyedMutex* acq_mtx(ID3D11Texture2D* tex);
void release_mtx(IDXGIKeyedMutex* pDXGIKeyedMutex);

class RenderOffscreen {
public:
    void init(ID3D11Device* dxdev);
    // create new texture and convert
    ID3D11Texture2D* convert_NV12(ID3D11Texture2D* nv12);

    // convert into provided texture
    // no format, size or device checking of dst
    void convert_NV12(ID3D11Texture2D* dst, ID3D11Texture2D* nv12);

    inline bool is_ready() // check if initialized and ready to render
    {
        return (device != nullptr);
    }

    // keeping it in the header to
    // track of added/removed members
    inline void reset() {
        crel(view);
        for (int i = 0; i < 2; i++) crel(y_uv_view[i]);
        crel(NV12_VS);
        crel(NV12_PS);
        crel(sampler_linear);
        crel(disable_depth);
        crel(device);
    }

    virtual ~RenderOffscreen();

protected:
    ID3D11Device* device = nullptr;
    ID3D11RenderTargetView* view = nullptr; // view of target
    D3D11_VIEWPORT viewport = {};
    ID3D11VertexShader* NV12_VS = nullptr;
    ID3D11PixelShader* NV12_PS = nullptr;
    ID3D11ShaderResourceView* y_uv_view[2] = {};
    ID3D11SamplerState* sampler_linear = nullptr;
    ID3D11DepthStencilState* disable_depth = nullptr;

    // cleaning com interface pointer
    template <typename ICOM> void crel(ICOM* &com) {
        if (com != nullptr) {
            com->Release();
            com = nullptr;
        }
    }

    // convert into provided planar texture
    // no format, size or device checking of dst
    // no format, size or device checking of nv12
    void planar_from_NV12(ID3D11Texture2D* dst, ID3D11Texture2D* nv12);

};
#endif //OFFSCREENDX11_H
