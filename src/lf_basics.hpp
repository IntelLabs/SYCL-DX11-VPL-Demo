//==============================================================================
// Copyright Intel Corporation 2024
//
// SPDX-License-Identifier: MIT
//==============================================================================
#pragma once

template <typename T> struct vec2 {
    vec2(): x(0.0f), y(0.0f) {}
    vec2(T x, T y) : x(x), y(y) {}
    T   x;
    T   y;
};

enum lf_shader_type {
    mono = 0,
    shader_types_total = 1
};

// pixel shader parameters

struct lf_parameters_render {
    unsigned int render_width = 7680;
    unsigned int render_height = 4320;

    vec2<float> centerXY = {0.5f, 0.5f};
};
