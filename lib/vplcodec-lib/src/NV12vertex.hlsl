//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
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
