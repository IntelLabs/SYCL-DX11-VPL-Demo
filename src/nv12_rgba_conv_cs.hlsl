//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

Texture2D<float> y_in : register(t0);
Texture2D<float2> uv_in : register(t1);
RWTexture2D<float4> rgba_out : register(u0);

SamplerState sampleLinear : register(s0)
{
    Filter = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};

// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit YUV to RGB888

static const float3x3 yuv2rgb_mat =
{
    1.164383f, 1.164383f, 1.164383f,
	0.000000f, -0.391762f, 2.017232f,
	1.596027f, -0.812968f, 0.000000f
};

float3 yuv2rgb(float3 yuv)
{
	// These values are calculated from (16 / 255) and (128 / 255)
    yuv -= float3(0.062745f, 0.501960f, 0.501960f);
    yuv = mul(yuv, yuv2rgb_mat);

    return saturate(yuv);
}

float4 sample_nv12(float2 tex)
{
    float y = y_in.SampleLevel(sampleLinear, tex, 0);
    float2 uv = uv_in.SampleLevel(sampleLinear, tex, 0);

    return float4(yuv2rgb(float3(y, uv)), 1.f);
}

// don't need any sampler
static const float3x3 rgb2yuv_mat =
{
    0.256788f, -0.148223f, 0.439216f,
    0.504129f, -0.290993f, -0.367788f,
    0.097906f,  0.439216f, -0.071427f
};

float3 rgb2yuv(float3 rgb) {
    float3 yuv =    mul(rgb, rgb2yuv_mat) +
                    float3(0.062745f, 0.501960f, 0.501960f); // 16/255, 128/255
    return saturate(yuv);
}

// TODO: optimize if critical to compute 4Y and 1 uv of mean
// shader must be run over (width, height)/16/2
[numthreads(16, 16, 1)]
void main_cs( uint3 DTid : SV_DispatchThreadID )
{
    uint width;
    uint height;
    rgba_out.GetDimensions(width, height);
    uint2 c = DTid.xy;
    if( any(c>=uint2(width, height)) ) return;
    float2 tc = c;
    tc += 0.5;
    tc /= uint2(width, height);
    rgba_out[DTid.xy] = sample_nv12(tc);
}