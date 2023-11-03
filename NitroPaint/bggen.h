#pragma once
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"


#define BGGEN_FORMAT_NITROSYSTEM     0
#define BGGEN_FORMAT_NITROCHARACTER  1
#define BGGEN_FORMAT_AGBCHARACTER    2
#define BGGEN_FORMAT_HUDSON          3
#define BGGEN_FORMAT_HUDSON2         4
#define BGGEN_FORMAT_BIN             5
#define BGGEN_FORMAT_BIN_COMPRESSED  6

//
// Structure used for character compression. Fill them out and pass them to
// BgPerformCharacterCompression.
//
typedef struct BGTILE_ {
	BYTE indices[64];
	COLOR32 px[64]; //redundant, speed
	RxYiqColor pxYiq[64];
	int masterTile;
	int nRepresents;
	int flipMode;
	int palette;
} BGTILE;


//
// Call this function after filling out the RGB color info in the tile array.
// The function will associate each tile with its best fitting palette, index
// the tile with that palette, and perform optional dithering.
//
void BgSetupTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Perform character compresion on the input array of tiles. After tiles are
// combined, the bit depth and palette settings are used to finalize the
// result in the tile array. progress must not be NULL, and ranges from 0-1000.
//
int BgPerformCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress);

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
//  - color0Mode: change how color 0 is determined
//
void BgGenerate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse,
	int palette, int nPalettes, int bin, int tileBase, int mergeTiles, int alignment,
	int paletteSize, int paletteOffsetm, int rowLimit, int nMaxChars,
	int color0Mode, int balance, int colorBalance, int enhanceColors,
	int *progress1, int *progress1Max, int *progress2, int *progress2Max,
	NCLR *nclr, NCGR *ncgr, NSCR *nscr);

void BgReplaceSection(NCLR *nclr, NCGR *ncgr, NSCR *nscr, COLOR32 *px, int width, int height,
	int writeScreen, int writeCharacterIndices,
	int tileBase, int nPalettes, int paletteNumber, int paletteOffset,
	int paletteSize, BOOL newPalettes, int writeCharBase, int nMaxChars,
	BOOL newCharacters, BOOL dither, float diffuse, int maxTilesX, int maxTilesY,
	int nscrTileX, int nscrTileY, int balance, int colorBalance, int enhanceColors,
	int *progress, int *progressMax);



