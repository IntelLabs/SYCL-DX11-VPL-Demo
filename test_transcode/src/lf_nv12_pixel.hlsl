// glsl style ops
//#define mod(x, y) ((x) - (y) * floor((x) / (y)))
#define mix(x, y, a) ((x)*(1.0-(a))+(y)*(a))

float mod(float x, float y) {
	return x - y * floor(x / y);
}
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
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};

cbuffer LFcalib : register(b0)
{
	float   pitch;
	float   tilt;
	float   center;
	float   subp;

	float   displayAspect;
	float   quiltAspect;
	float2	junk0; // to please Bill

	int   invView;
	int   ri;
	int   bi;
	int   quiltInvert;
};

cbuffer LFquilt : register(b1)
{
	float3  tile; //qs_columns, qs_rows, qs_totalViews
	float  MiddleView;

	float2  viewPortion;
	float2  centerXY;

	float  QuiltPixel;
	float  zoom;
	float   focus;
	float	junk1;

	int  overscan;
	int   debug;
	int2	junk2; // to please Bill
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

//float4 main(VertexOut input) : SV_TARGET
//{
//	float y = luminanceChannel.Sample(sampleLinear, input.tex);
//	float2 uv = chrominanceChannel.Sample(sampleLinear, input.tex);
//
//	return float4(ConvertYUVtoRGB(float3(y, uv)), 1.f);
//}


float2 texArr(float3 uvz)
{
	// decide which section to take from based on the z.
	//float x = (mod(uvz.z, tile.x) + uvz.x) / tile.x;
	float x = (mod(uvz.z, tile.x) + uvz.x) / tile.x + (abs(uvz.z) - MiddleView) * focus * QuiltPixel;
	x = clamp(x, mod(uvz.z + 0.005, tile.x) / tile.x, mod(uvz.z + 0.995, tile.x) / tile.x);
	// the 1.0 - is for quilt textures that have not been flipped before uploading them to OpenGL 
	//float y = 1.0 - (floor(uvz.z / tile.x) + uvz.y) / tile.y;
	float y = (floor(uvz.z / tile.x) + uvz.y) / tile.y;
	return (float2(x, y) * viewPortion.xy);
}

// recreate CG clip function (clear pixel if any component is negative)
void clip(float3 toclip)
{
	if (any(toclip < float3(0, 0, 0))) discard;
}
bool aeq(float x, float c) {
	float d0 = (c != 0) ? (x - c) / c : x;
	return (abs(d0) <= 0.5e-05);
}
float4 main(VertexOut input) : SV_TARGET
{
	float4 retColor;
	if (debug == 1)
	{
		retColor = sample_nv12(input.tex);
	}
	else {
		float invert = 1.0;
		if (invView + quiltInvert == 1) invert = -1.0;
		float3 nuv = float3(input.tex.x,input.tex.y, 0.0);
		nuv -= 0.5;
		float modx = clamp(step(quiltAspect, displayAspect) * step(float(overscan), 0.5) + step(displayAspect, quiltAspect) * step(0.5, float(overscan)), 0.0, 1.0);
		nuv.x = modx * nuv.x * displayAspect / quiltAspect + (1.0 - modx) * nuv.x;
		nuv.y = modx * nuv.y + (1.0 - modx) * nuv.y * quiltAspect / displayAspect;
		nuv *= zoom;
		nuv = nuv + float3(centerXY.x, centerXY.y, 0.5);
		//nuv += 0.5;
		//nuv = clip0 (nuv);
		//nuv = 1.0 - clip0 (1.0-nuv);
		//nuv = clamp (nuv, 0, 1);

		if (any(nuv < float3(0,0,0)) || any(nuv > float3(1,1,1))) {
			// debug tile.z != 45.0 !aeq(pitch, 1034.84) || !aeq(subp,4.34028e-05) || 
			// || !aeq(tilt, -0.0731832) || !aeq(center, -0.392222)  
			retColor = float4(0.5, 0.0, 0.0, 1.0);
		}
		else
		{
			float4 rgb[3];
			[unroll] for (int i = 0; i < 3; i++)
			{
				nuv.z = ((input.tex.x) + subp * float(i) + (input.tex.y) * tilt) * pitch - center;
				nuv.z = mod(nuv.z + ceil(abs(nuv.z)), 1.0);
				nuv.z *= invert;
				nuv.z *= tile.z;
				//nuv.z = 1.5; // DEBUG ATTN: 
				float3 coords1 = nuv;
				float3 coords2 = nuv;
				coords2.y = clamp(nuv.y, 0.005, 0.995);
				coords1.y = coords2.y;
				coords1.z = floor(nuv.z);
				coords2.z = ceil(nuv.z);
				float4 col1 = sample_nv12(texArr(coords1));
				float4 col2 = sample_nv12(texArr(coords2));
				rgb[i] = mix(col1, col2, nuv.z - coords1.z);
			}
			retColor = float4(rgb[ri].r, rgb[1].g, rgb[bi].b, 1.0);
		}
	}
	return retColor;
}
