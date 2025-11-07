//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
#pragma once
#include <d3d11.h>
#include <iostream>
#include <Windows.h>

#include "lf_nv12_pixel.hpp"

class render_window {
public:
	bool init(HINSTANCE hInstance, int x = 0, int y = 0, int w = 7680, int h = 4320);
	virtual int run();
protected: 
	HWND	hwnd=NULL; // window handle
	int		x0=0;
	int		y0=0;
	int		width=0;
	int		height=0;
};