//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
#ifndef NV12LFSHADERS_HPP
#define NV12LFSHADERS_HPP

#include <D3D11.h>
#include <DirectXMath.h>

/// From NV12shaders.cpp
extern const char* FS_QUAD_VS_SRC;	// fullscreen quad vs
extern const char* MONO_PS_SRC;		// non-planar (e.g. rgba) texture ps
extern const char* MONO_NV12_PS_SRC;	// planar (nv12) texture ps

#endif 
