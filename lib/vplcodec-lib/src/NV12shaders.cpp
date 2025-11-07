
char* NV12_VS_source = R"###(
//////////////////////////
//       OUTPUT

struct VertexOut
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

//////////////////////////
//     Screen Quad

VertexOut main(uint id : SV_VertexId)
{
    VertexOut vout;
    vout.tex = float2(id % 2, (id % 4) >> 1);
    vout.pos = float4((vout.tex.x - 0.5f) * 2, -(vout.tex.y - 0.5f) * 2, 0, 1);
    //vout.tex.y = 1.0-vout.tex.y;
    return vout;
}
)###";

char* NV12_PS_source = R"###(
//////////////////////////
//       INPUT

struct VertexOut
{
    float4 pos     : SV_POSITION;
    float2 tex      : TEXCOORD;
};

//////////////////////////
//        NV12 to RGBA 
Texture2D<float>  luminanceChannel   : t0;
Texture2D<float2> chrominanceChannel : t1;
SamplerState      sampleLinear     : s0
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};


// Derived from https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
// Section: Converting 8-bit YUV to RGB888
static const float3x3 YUVtoRGBCoeffMatrix =
{
    1.164383f,  1.164383f, 1.164383f,
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

float4 main(VertexOut input) : SV_TARGET
{
    float y = luminanceChannel.Sample(sampleLinear, input.tex);
    float2 uv = chrominanceChannel.Sample(sampleLinear, input.tex);

    return float4(ConvertYUVtoRGB(float3(y, uv)), 1.f);
}
)###";
