#include "NV12LFshaders.hpp"
const char* FS_QUAD_VS_SRC = R"###(
//////////////////////////
//       OUTPUT

struct VertexOut
{
    float4 pos     : SV_POSITION;
    float2 tex      : TEXCOORD;
};

//////////////////////////
//     Screen Quad

VertexOut main(uint id : SV_VertexId)
{
    VertexOut vout;
    vout.tex = float2(id % 2, (id % 4) >> 1);
    vout.pos = float4((vout.tex.x - 0.5f) * 2, -(vout.tex.y - 0.5f) * 2, 0, 1);
    //vout.tex.y = 1.0 - vout.tex.y;
    return vout;
}
)###";

const char* MONO_NV12_PS_SRC = R"###( 
//////////////////////////
//       INPUT

struct VertexOut
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

//////////////////////////
//        NV12 to RGBA 
Texture2D<float> luminanceChannel : t0;
Texture2D<float2> chrominanceChannel : t1;
SamplerState sampleLinear : s0
{
    Filter = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};

cbuffer ConstantBuffer : register(b0) // b0 is the slot to which the buffer is bound in the C++ code
{
    uint render_width; // 7680
    uint render_height; // 4320

    float2 centerXY; // {0.5f, 0.5f}
};

// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit YUV to RGB888
static const float3x3 YUVtoRGBCoeffMatrix =
{
    1.164383f, 1.164383f, 1.164383f,
	0.000000f, -0.391762f, 2.017232f,
	1.596027f, -0.812968f, 0.000000f
};

float3 ConvertYUVtoRGB(float3 yuv)
{
	// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
	// Section: Converting 8-bit YUV to RGB888

	// These values are calculated from (16 / 255) and (128 / 255)
    yuv -= float3(0.062745f, 0.501960f, 0.501960f);
    yuv = mul(yuv, YUVtoRGBCoeffMatrix);

    return saturate(yuv);
}

float4 sample_nv12(float2 tex)
{
    float y = luminanceChannel.Sample(sampleLinear, tex);
    float2 uv = chrominanceChannel.Sample(sampleLinear, tex);

    return float4(ConvertYUVtoRGB(float3(y, uv)), 1.f);
}

float4 sample_nv12fl(float2 tex0)
{
    float2 tex = float2(tex0.x, 1.0 - tex0.y);
    float y = luminanceChannel.Sample(sampleLinear, tex);
    float2 uv = chrominanceChannel.Sample(sampleLinear, tex);

    return float4(ConvertYUVtoRGB(float3(y, uv)), 1.f);
}

float4 main(VertexOut input) : SV_TARGET
{
    float2 vc = input.tex.xy;

    // Sample the input texture at the pixel location
    float4 pixelValue = float4(0, 0, 0, 1);
    float3 texelValue = sample_nv12(input.tex);
    pixelValue.rgb = texelValue;
    return pixelValue;
}
)###";

const char* MONO_PS_SRC = R"###( 
//////////////////////////
//       INPUT

struct VertexOut
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

//////////////////////////
//        Non-planar texture 
Texture2D<float4> inputTexture : register(t0);
SamplerState sampleLinear : s0
{
    Filter = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};

cbuffer ConstantBuffer : register(b0) // b0 is the slot to which the buffer is bound in the C++ code
{
    uint render_width; // 7680
    uint render_height; // 4320

    float2 centerXY; // {0.5f, 0.5f}

};

float4 main(VertexOut input) : SV_TARGET
{
    // Sample the input texture at the pixel location
    float4 pixelValue = float4(0, 0, 0, 1);
    float4 texelValue = inputTexture.Sample(sampleLinear, input.tex);
    pixelValue.rgb = texelValue.rgb;
    return pixelValue;
}
)###";

