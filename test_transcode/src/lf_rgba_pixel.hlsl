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
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD;
};

Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);
cbuffer ConstantBuffer : register(b0) // b0 is the slot to which the buffer is bound in the C++ code
{
    uint render_width; // 7680
    uint render_height; // 4320

    float lineNum; //16.7195
    float tg_lens; //0.10379

    float bias; //3.5
    bool isRGB;
};

cbuffer ConstantBuffer2 : register(b1) // b1 is the slot to which the buffer is bound in the C++ code
{
    uint input_width; //7200
    uint input_height; //8100
    uint row; //10
    uint col; //5
};

cbuffer ConstantBuffer2 : register(b2) // b2 is the slot to which the buffer is bound in the C++ code
{
    float zeroPlaneTuning;
    float centerShift;
};

float4 main(VertexOut input) : SV_TARGET
{
    float D; //distance between a subpixel(i,j,k) to left side of *left most* lens
    float A; //distance between a subpixel(i,j,k) to left side of *its corresponding* lens. 

    int img_num = row * col;
    int mid = img_num / 2;
    int viewp;
    
    uint inputWidth;
    uint inputHeight;
    inputTexture.GetDimensions(inputWidth, inputHeight);

    uint subTexWidth = inputWidth / col; // the width of each sub video
    uint subTexHeight = inputHeight / row; // the height of each sub video

    uint outputWidth = render_width;
    uint outputHeight = render_height;
    //outputTexture.GetDimensions(outputWidth, outputHeight);

    // Map the Dispatch Thread ID to the pixel location
    // i and j should be thought as the outputTex location. 
    //int i = DTid.x;
    //int j = DTid.y;

    float x = input.tex.x * subTexWidth; //i * (1.0 * subTexWidth) / outputWidth; // x is the horizontal location relative to a given sub video
    float y = input.tex.y * subTexHeight; //j * (1.0 * subTexHeight) / outputHeight; // y is the vertical location relative to a given sub video

    // Sample the input texture at the pixel location
    float4 pixelValue = int4(0, 0, 0, 1);
    float wid = zeroPlaneTuning * subTexWidth / 1000;

    int i = int(input.tex.x * outputWidth); // possibly -0.5
    int j = int(input.tex.y * outputHeight); // possibly -0.5
    for (int k = 0; k < 3; k++)// 3 channels/subpixel.
    {
        // C++ method
        D = 3 * i + k + 3 * j * tg_lens + bias + centerShit;
        A = fmod(D, lineNum);

        // handles negative pixels to the left of the first lens.
        while (A < 0)
        {
            A = A + lineNum;
        }
        
        viewp = floor(A / (lineNum / img_num));
        if (viewp == img_num)
        {
            viewp = img_num - 1;
        }
        
        // The Camera order face to Objectis is from Left to Right, So Need Reverse The Order
        viewp = img_num - viewp - 1;
        float movepixel = wid * (viewp - mid) / (float) mid;

        //translate grid numbering to global pixel coordinates, getting the pixel value
        if (isRGB)
        {
            pixelValue[k] = inputTexture.Load(int3(inputWidth / col * (viewp % col) + x + movepixel, inputHeight / row * (viewp / col) + y, 0))[k];
        }
        else
        {
            pixelValue[2 - k] = inputTexture.Load(int3(inputWidth / col * (viewp % col) + x + movepixel, inputHeight / row * (viewp / col) + y, 0))[2 - k];
        }
    }
    // write the pixel value to the output texture
    //outputTexture[int2(i, j)] = pixelValue;
    return pixelValue;
}