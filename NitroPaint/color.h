// -----------------------------------------------------------------------------------------------
// Copyright (c) 2020, Garhoogin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials provided
//    with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// -----------------------------------------------------------------------------------------------
#pragma once

#include <stdint.h>

typedef uint16_t COLOR;    // 15-bit color. BGR555.
typedef uint32_t COLOR32;  // 32-bit color. ABGR8888

#define COLOR_CHANNEL_R 0
#define COLOR_CHANNEL_G 1
#define COLOR_CHANNEL_B 2

#define GetR(c) (((c)>> 0)&0x1F)
#define GetG(c) (((c)>> 5)&0x1F)
#define GetB(c) (((c)>>10)&0x1F)
#define GetA(c) (((c)>>15)&0x01)

#define REVERSE(x) ((x)&0xFF00FF00)|(((x)&0xFF)<<16)|(((x)>>16)&0xFF)

#define CREVERSE(x) (((x)&0x83E0)|(((x)&0x1F)<<10)|(((x)>>10)&0x1F))

// -----------------------------------------------------------------------------------------------
// Name: ColorConvertToDS
//
// Converts a 24-bit RGB color to a 15-bit RGB color by scaling each color channel independently
// from a scale of 255 to a scale of 31, rounding to the nearest whole number. The returned color
// has the most significant bit clear.
//
// Parameters:
//   c             The 24-bit color to convert.
//
// Returns:
//   The converted color.
// -----------------------------------------------------------------------------------------------
COLOR ColorConvertToDS(COLOR32 c);

// -----------------------------------------------------------------------------------------------
// Name: ColorConvertFromDS
//
// Converts a 15-bit RGB color to a 24-bit RGB color by scaling each color channel independently
// from a scale of 31 to a scale of 255, rounding to the nearest whole number. The returned color
// has the most significant 8 bits clear.
//
// Parameters:
//   c             The 24-bit color to convert.
//
// Returns:
//   The converted color.
// -----------------------------------------------------------------------------------------------
COLOR32 ColorConvertFromDS(COLOR c);

// -----------------------------------------------------------------------------------------------
// Name: ColorRoundToDS15
//
// Converts a 24-bit color to its nearest 15-bit color and then back, as though by calling the
// ColorConvertToDS and ColorConvertFromDS routines. The returned color has its most significant
// bit clear.
//
// Parameters:
//   c             The 24-bit color to convert.
//
// Returns:
//   The converted color.
// -----------------------------------------------------------------------------------------------
COLOR32 ColorRoundToDS15(COLOR32 c);

// -----------------------------------------------------------------------------------------------
// Name: ColorRoundToDS18
//
// Converts a 24-bit color to its nearest 18-bit color and then back.
//
// Parameters:
//   c             The 24-bit color to convert.
//
// Returns:
//   The converted color.
// -----------------------------------------------------------------------------------------------
COLOR32 ColorRoundToDS18(COLOR32 c);

//
// Interpolate between two 15-bit colors
// Deprecated do not use
//
COLOR ColorInterpolate(COLOR c1, COLOR c2, float amt);
