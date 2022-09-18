#pragma once

#include <stdint.h>

typedef uint16_t COLOR;
typedef uint32_t COLOR32;

#define COLOR_CHANNEL_R 0
#define COLOR_CHANNEL_G 1
#define COLOR_CHANNEL_B 2

#define GetR(c) ((c)&0x1F)
#define GetG(c) (((c)>>5)&0x1F)
#define GetB(c) (((c)>>10)&0x1F)
#define GetA(c) (((c)>>15)&1)

#define REVERSE(x) ((x)&0xFF00FF00)|(((x)&0xFF)<<16)|(((x)>>16)&0xFF)

#define CREVERSE(x) (((x)&0x83E0)|(((x)&0x1F)<<10)|(((x)>>10)&0x1F))

#define ColorSetChannel(c,ch,val) ((c)=(COLOR)(((c)&((0x1F<<((ch)*5))^0x7FFF))|(val)<<((ch)*5)))

#define ColorCreate(r,g,b) ((COLOR)((r)|((g)<<5)|((b)<<10)))

//
// Convert 24-bit RGB color to 15-bit color
//
COLOR ColorConvertToDS(COLOR32 c);

//
// Convert 15-bit RGB color to 24-bit color
//
COLOR32 ColorConvertFromDS(COLOR c);

//
// Round a 24-bit RGB color to the nearest 15-bit representable 24-bit color
//
COLOR32 ColorRoundToDS15(COLOR32 c);

//
// Round a 24-bit RGB color to the nearest 18-bit representable 24-bit color
//
COLOR32 ColorRoundToDS18(COLOR32 c);

//
// Interpolate between two 15-bit colors
//
COLOR ColorInterpolate(COLOR c1, COLOR c2, float amt);
