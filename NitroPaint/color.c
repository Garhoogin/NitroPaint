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
#include "color.h"

static const uint8_t sLookup_8_to_5[] = {
	 0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
	 2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,
	 4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,
	 6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,
	 8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9, 10,
	10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12,
	12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15,
	16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
	19, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
	21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23,
	23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25,
	25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27,
	27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29,
	29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31
};

static const uint8_t sLookup_5_to_8[] = {
	  0,   8,  16,  25,  33,  41,  49,  58,  66,  74,  82,  90,  99, 107, 115, 123,
	132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255
};

static const uint8_t sLookup_8_to_6_to_8[] = {
	  0,   0,   0,   4,   4,   4,   4,   8,   8,   8,   8,  12,  12,  12,  12,  16,
	 16,  16,  16,  20,  20,  20,  20,  24,  24,  24,  24,  28,  28,  28,  28,  32,
	 32,  32,  32,  36,  36,  36,  36,  40,  40,  40,  40,  45,  45,  45,  45,  49,
	 49,  49,  49,  53,  53,  53,  53,  57,  57,  57,  57,  61,  61,  61,  61,  65,
	 65,  65,  65,  69,  69,  69,  69,  73,  73,  73,  73,  77,  77,  77,  77,  81,
	 81,  81,  81,  85,  85,  85,  85,  85,  89,  89,  89,  89,  93,  93,  93,  93,
	 97,  97,  97,  97, 101, 101, 101, 101, 105, 105, 105, 105, 109, 109, 109, 109,
	113, 113, 113, 113, 117, 117, 117, 117, 121, 121, 121, 121, 125, 125, 125, 125,
	130, 130, 130, 130, 134, 134, 134, 134, 138, 138, 138, 138, 142, 142, 142, 142,
	146, 146, 146, 146, 150, 150, 150, 150, 154, 154, 154, 154, 158, 158, 158, 158,
	162, 162, 162, 162, 166, 166, 166, 166, 170, 170, 170, 170, 170, 174, 174, 174,
	174, 178, 178, 178, 178, 182, 182, 182, 182, 186, 186, 186, 186, 190, 190, 190,
	190, 194, 194, 194, 194, 198, 198, 198, 198, 202, 202, 202, 202, 206, 206, 206,
	206, 210, 210, 210, 210, 215, 215, 215, 215, 219, 219, 219, 219, 223, 223, 223,
	223, 227, 227, 227, 227, 231, 231, 231, 231, 235, 235, 235, 235, 239, 239, 239,
	239, 243, 243, 243, 243, 247, 247, 247, 247, 251, 251, 251, 251, 255, 255, 255
};

COLOR ColorConvertToDS(COLOR32 c) {
	unsigned int r = sLookup_8_to_5[(c >>  0) & 0xFF];
	unsigned int g = sLookup_8_to_5[(c >>  8) & 0xFF];
	unsigned int b = sLookup_8_to_5[(c >> 16) & 0xFF];
	return r | (g << 5) | (b << 10);
}

COLOR32 ColorConvertFromDS(COLOR c) {
	unsigned int r = sLookup_5_to_8[GetR(c)];
	unsigned int g = sLookup_5_to_8[GetG(c)];
	unsigned int b = sLookup_5_to_8[GetB(c)];
	return r | (g << 8) | (b << 16);
}

COLOR32 ColorRoundToDS15(COLOR32 c) {
	unsigned int r = sLookup_5_to_8[sLookup_8_to_5[(c >>  0) & 0xFF]];
	unsigned int g = sLookup_5_to_8[sLookup_8_to_5[(c >>  8) & 0xFF]];
	unsigned int b = sLookup_5_to_8[sLookup_8_to_5[(c >> 16) & 0xFF]];
	return r | (g << 8) | (b << 16);
}

COLOR32 ColorRoundToDS18(COLOR32 c) {
	unsigned r = sLookup_8_to_6_to_8[(c >>  0) & 0xFF];
	unsigned g = sLookup_8_to_6_to_8[(c >>  8) & 0xFF];
	unsigned b = sLookup_8_to_6_to_8[(c >> 16) & 0xFF];
	return r | (g << 8) | (b << 16);
}

COLOR ColorInterpolate(COLOR c1, COLOR c2, float amt) {
	int r = (int) (GetR(c1) * (1.0f - amt) + GetR(c2) * amt);
	int g = (int) (GetG(c1) * (1.0f - amt) + GetG(c2) * amt);
	int b = (int) (GetB(c1) * (1.0f - amt) + GetB(c2) * amt);
	return r | (g << 5) | (b << 10);
}
