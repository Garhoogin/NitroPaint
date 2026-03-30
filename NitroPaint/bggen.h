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

// -----------------------------------------------------------------------------------------------
// BG Generator Core Routines
//
// This header exposes a simple library for generating BG data. It provides an interface to
// generate backgrounds in all of the DS's background formats (text, affine, affine extended,
// and bitmap). It supports character compression (the mergence of similar character units) and
// the character map color reduction (the use of more than one color palette across the whole
// background).
// -----------------------------------------------------------------------------------------------
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "palette.h"

//#define BGGEN_USE_DCT

// -----------------------------------------------------------------------------------------------
// enum BggenColor0Mode
//
// This enum defines the valid modes for how the library will choose color 0 in a generated
// color palette, if applicable. The values are the folowing:
//   BGGEN_COLOR0_FIXED          Color 0 is fixed as a default color (magenta).
//   BGGEN_COLOR0_AVERAGE        Color 0 is taken to be the average color of the input bitmap.
//   BGGEN_COLOR0_EDGE           Color 0 is taken to be the average color of edge pixels of the
//                               input bitmap. Pixels are considered belonging to an edge if they
//                               are adjacent a transparent pixel or the edge of the bitmap.
//   BGGEN_COLOR0_CONTRAST       Color 0 is taken to be one far away from the input bitmap's
//                               colors.
//   BGGEN_COLOR0_USE            Color 0 is used as an opaque color. When one palette is used,
//                               the slot is used as an opaque color slot. When more than one
//                               palette is used, the color in position 0 is taken to be the same
//                               across all palettes, and transparent pixels are produced to let
//                               the backdrop color show.
// -----------------------------------------------------------------------------------------------
typedef enum BggenColor0Mode_ {
	BGGEN_COLOR0_FIXED,
	BGGEN_COLOR0_AVERAGE,
	BGGEN_COLOR0_EDGE,
	BGGEN_COLOR0_CONTRAST,
	BGGEN_COLOR0_USE
} BggenColor0Mode;

#define BGGEN_BGTYPE_TEXT_16x16       0
#define BGGEN_BGTYPE_TEXT_256x1       1
#define BGGEN_BGTYPE_AFFINE_256x1     2
#define BGGEN_BGTYPE_AFFINEEXT_256x16 3
#define BGGEN_BGTYPE_BITMAP           4


#define BGGEN_FORMAT_NITROSYSTEM     0          // NCLR, NCGR, NSCR
#define BGGEN_FORMAT_NITROCHARACTER  1          // NCL,  NCG,  NSC
#define BGGEN_FORMAT_IRISCHARACTER   2          // ICL,  ICG,  ISC
#define BGGEN_FORMAT_AGBCHARACTER    3          // ACL,  ACG,  ASC
#define BGGEN_FORMAT_IMAGESTUDIO     4          // 5BG
#define BGGEN_FORMAT_HUDSON          5          // Hudson bin
#define BGGEN_FORMAT_HUDSON2         6          // Hudson bin
#define BGGEN_FORMAT_GRF             7          // GRF
#define BGGEN_FORMAT_BIN             8          // raw
#define BGGEN_FORMAT_BIN_COMPRESSED  9          // raw compresed

typedef struct BgDctBlock_ {
	float blockY[64];
	float blockI[64];
	float blockQ[64];
	float blockA[64];
} BgDctBlock;

//
// Structure used for character compression. Fill them out and pass them to
// BgPerformCharacterCompression.
//
typedef struct BgTile_ {
	COLOR32 px[64];               // RGBA colors: redundant, speed
	RxYiqColor pxYiq[64];         // YIQA colors
#ifdef BGGEN_USE_DCT
	BgDctBlock dct;               // DCT coefficients
#endif
	unsigned char indices[64];    // color indices per pixel
	int masterTile;               // index of master tile for this tile 
	int nRepresents;              // number of tiles this tile represents
	int flipMode;                 // flip orientation of this tile
	int palette;                  // palette index of this tile
	int charNo;                   // this character's output index, if this is a master tile.
} BgTile;

/****************************************************************************\
*
*         /--offset---\  /-------length-------\
*        [ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]  }  base
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  \
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  |  count
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  /
*        [ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]
*
\****************************************************************************/
typedef struct BgPaletteRegion_ {
	int base;                          // Index of first palette to use
	int count;                         // Number of palettes to use
	int length;                        // Number of colors per palette
	int offset;                        // Index of first color to use in each palette
} BgPaletteRegion;

typedef struct BgCharacterSetting_ {
	int base;                         // character VRAM base offset
	int compress;                     // enables character compression
	int nMax;                         // max characters if compression enabled
	int alignment;                    // rounds up character count to a multiple of this
} BgCharacterSetting;

typedef struct BgGenerateParameters_ {
	//global
	int bgType;                       // Type of BG (e.g. text, affine...)
	int fmt;                          // Format of output data
	RxBalanceSetting balance;         // Balance settings to use during conversion

	//palette
	int compressPalette;              // Use palette compression
	BggenColor0Mode color0Mode;       // Specifies how color 0 is chosen
	BgPaletteRegion paletteRegion;    // Palette region to use for conversion

	//character
	RxDitherSetting dither;          // Dither configuration
	BgCharacterSetting characterSetting;
} BgGenerateParameters;



// -----------------------------------------------------------------------------------------------
// Name: BgSetupTiles
// 
// Call this function after filling out the RGB color info in the tile array. The function will
// associate each tile with its best fitting palette, index the tile with that palette, and
// perform optional dithering.
// -----------------------------------------------------------------------------------------------
void BgSetupTiles(
	BgTile                 *tiles,
	unsigned int            nTiles,
	int                     nBits,
	const COLOR32          *palette,
	unsigned int            paletteSize,
	unsigned int            nPalettes,
	unsigned int            paletteBase,
	unsigned int            paletteOffset,
	int                     dither,
	float                   diffuse,
	const RxBalanceSetting *balance
);

// -----------------------------------------------------------------------------------------------
// Name: BgPerformCharacterCompression
//
// Perform character compresion on the input array of tiles. After tiles are combined, the bit
// depth and palette settings are used to finalize the result in the tile array. progress must
// not be NULL, and ranges from 0-1000.
//
// Returns:
//   The number of unique characters after compression
// -----------------------------------------------------------------------------------------------
int BgPerformCharacterCompression(
	BgTile                 *tiles,
	unsigned int            nTiles,
	unsigned int            nBits,
	unsigned int            nMaxChars,
	int                     allowFlip,
	const COLOR32          *palette,
	unsigned int            paletteSize,
	unsigned int            nPalettes,
	unsigned int            paletteBase,
	unsigned int            paletteOffset,
	const RxBalanceSetting *balance,
	volatile int           *progress
);

// -----------------------------------------------------------------------------------------------
// Name: BgGenerate
//
// Generates a BG with specified parameters.
//
// Parameters:
//   nclr                        Pointer to output palette data
//   ncgr                        Pointer to output character data
//   nscr                        Pointer to output screen data
//   px                          Image pixel data
//   width                       Image width
//   height                      Image height
//   params                      Conversion parameters
//   progress1                   Progress 1
//   progress1Max                Progress 1 max
//   progress2                   Progress 2
//   progress2Max                Progress 2 max
// -----------------------------------------------------------------------------------------------
void BgGenerate(
	NCLR                      **pNclr,
	NCGR                      **pNcgr,
	NSCR                      **pNscr,
	COLOR32                    *px,
	unsigned int                width,
	unsigned int                height,
	const BgGenerateParameters *params,
	volatile int               *progress1,
	volatile int               *progress1Max,
	volatile int               *progress2,
	volatile int               *progress2Max
);

void BgReplaceSection(
	NCLR *nclr, 
	NCGR *ncgr,
	NSCR *nscr,
	COLOR32 *px,
	int width,
	int height,
	int writeScreen,
	int writeCharacterIndices,
	int tileBase,
	int nPalettes,
	int paletteNumber,
	int paletteOffset,
	int paletteSize,
	int newPalettes,
	int writeCharBase,
	int nMaxChars,
	int newCharacters,
	int dither,
	float diffuse,
	int maxTilesX,
	int maxTilesY,
	int nscrTileX,
	int nscrTileY,
	int balance,
	int colorBalance,
	int enhanceColors,
	int *progress,
	int *progressMax,
	int *progress2,
	int *progress2Max
);

