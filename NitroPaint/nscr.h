#pragma once
#include <Windows.h>
#include "ncgr.h"
#include "nclr.h"

#define SCREENFORMAT_TEXT 0
#define SCREENFORMAT_AFFINE 1
#define SCREENFORMAT_AFFINEEXT 2

#define SCREENCOLORMODE_16x16 0
#define SCREENCOLORMODE_256x1 1
#define SCREENCOLORMODE_256x16 2

#define TILE_FLIPX 1
#define TILE_FLIPY 2
#define TILE_FLIPXY (TILE_FLIPX|TILE_FLIPY)
#define TILE_FLIPNONE 0

#define NSCR_TYPE_INVALID	0
#define NSCR_TYPE_NSCR		1
#define NSCR_TYPE_HUDSON	2
#define NSCR_TYPE_HUDSON2	3
#define NSCR_TYPE_BIN       4
#define NSCR_TYPE_COMBO     5

extern LPCWSTR screenFormatNames[];

typedef struct NSCR_ {
	OBJECT_HEADER header;
	DWORD nWidth;
	DWORD nHeight;
	DWORD dataSize;
	WORD *data;
	int fmt;
	int nHighestIndex;//weird hack
	struct COMBO2D_ *combo2d; //for combination files
} NSCR;

#include "combo2d.h"

//
// Initialize an NSCR structure with sensible values.
//
void nscrInit(NSCR *nscr, int format);

//
// Determines if a byte array represents a valid Hudson screen file.
//
int nscrIsValidHudson(LPBYTE buffer, int size);

//
// Determines if a byte array represents a valid raw screen file.
//
int nscrIsValidBin(LPBYTE buffer, int size);

//
// Reads a screen file from an array.
//
int nscrRead(NSCR * nscr, char * file, DWORD dwFileSize);

//
// Reads a screen from a file.
//
int nscrReadFile(NSCR *nscr, LPCWSTR path);

//
// Renders a screenwith character and palette data to 32-bit output.
//
DWORD *toBitmap(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int *width, int *height, BOOL transparent);

//
// Write a screen to a stream.
//
int nscrWrite(NSCR *nscr, BSTREAM *stream);

//
// Write a screen to a file.
//
int nscrWriteFile(NSCR *nscr, LPWSTR name);

//
// Render a single tile of a screen to 32-bit output.
//
int nscrGetTile(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int x, int y, BOOL chceker, DWORD *out, BOOL transparent);

//
// Render a single tile of a screen to 32-bit output, with respect to character
// base for quirks in some game setups.
//
int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, BOOL checker, DWORD *out, int *tileNo, BOOL transparent);

//
// Generates a BG with the parameters:
//  - imgBits: source image
//  - width: width of image
//  - height: height of image
//  - nBits: bit depth of output
//  - dither: 1/0 to dither/not dither
//  - diffuse: between 0.0f and 1.0f, controls diffuse amount
//  - palette: first palette index to use
//  - nPalettes: number of palettes to use
//  - bin: generate raw data
//  - tileBase: index to be added to all tiles' character index
//  - mergeTiles: combine tiles in output
//  - paletteSize: maximum number of colors to output per palette
//  - paletteOffset: First color slot to output in each palette
//  - rowLimit: 1/0 to cut off/not cut off unused end colors
//  - nMaxChars: Maximum character count of resulting graphics
//
void nscrCreate(DWORD *imgBits, int width, int height, int nBits, int dither, float diffuse, 
				int palette, int nPalettes, int bin, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffsetm, int rowLimit, int nMaxChars,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max,
				NCLR *nclr, NCGR *ncgr, NSCR *nscr);
