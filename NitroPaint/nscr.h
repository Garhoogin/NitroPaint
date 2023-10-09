#pragma once
#include <Windows.h>
#include <stdint.h>

#include "ncgr.h"
#include "nclr.h"
#include "palette.h"

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

#define BGGEN_FORMAT_NITROSYSTEM     0
#define BGGEN_FORMAT_HUDSON          1
#define BGGEN_FORMAT_HUDSON2         2
#define BGGEN_FORMAT_NITROCHARACTER  3
#define BGGEN_FORMAT_AGBCHARACTER    4
#define BGGEN_FORMAT_BIN             5
#define BGGEN_FORMAT_BIN_COMPRESSED  6

#define NSCR_TYPE_INVALID	0
#define NSCR_TYPE_NSCR		1
#define NSCR_TYPE_HUDSON	2
#define NSCR_TYPE_HUDSON2	3
#define NSCR_TYPE_BIN       4
#define NSCR_TYPE_NC        5
#define NSCR_TYPE_AC        6
#define NSCR_TYPE_COMBO     7

#define BG_COLOR0_FIXED     0
#define BG_COLOR0_AVERAGE   1
#define BG_COLOR0_EDGE      2
#define BG_COLOR0_CONTRAST  3

extern LPCWSTR screenFormatNames[];

typedef struct NSCR_ {
	OBJECT_HEADER header;
	unsigned int nWidth;
	unsigned int nHeight;
	unsigned int dataSize;
	uint16_t *data;
	int fmt;
	int nHighestIndex;//weird hack
	uint16_t clearValue; //default tile value
	char *comment; //null terminated
	char *link; //null terminated, linked NCG
	int showGrid;     //for NC
	short gridWidth;  //for NC
	short gridHeight; //for NC
	struct COMBO2D_ *combo2d; //for combination files
} NSCR;

#include "combo2d.h"

//
// Structure used for character compression. Fill them out and pass them to
// performCharacterCompression.
//
typedef struct BGTILE_ {
	BYTE indices[64];
	COLOR32 px[64]; //redundant, speed
	YIQ_COLOR pxYiq[64];
	int masterTile;
	int nRepresents;
	int flipMode;
	int palette;
} BGTILE;

//
// Initialize an NSCR structure with sensible values.
//
void nscrInit(NSCR *nscr, int format);

//
// Determines if a byte array represents a valid Hudson screen file.
//
int nscrIsValidHudson(unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid raw screen file.
//
int nscrIsValidBin(unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid IS-ACG-CHARACTER screen file.
//
int nscrIsValidAsc(unsigned char *buffer, unsigned int size);

//
// Reads a screen file from an array.
//
int nscrRead(NSCR *nscr, unsigned char *file, unsigned int dwFileSize);

//
// Reads a screen from a file.
//
int nscrReadFile(NSCR *nscr, LPCWSTR path);

//
// Renders a screenwith character and palette data to 32-bit output.
//
COLOR32 *toBitmap(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int *width, int *height, int transparent);

//
// Write a screen to a stream.
//
int nscrWrite(NSCR *nscr, BSTREAM *stream);

//
// Write a screen to a file.
//
int nscrWriteFile(NSCR *nscr, LPCWSTR name);

//
// Render a single tile of a screen to 32-bit output.
//
int nscrGetTile(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int x, int y, int chceker, COLOR32 *out, int transparent);

//
// Render a single tile of a screen to 32-bit output, with respect to character
// base for quirks in some game setups.
//
int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, int checker, COLOR32 *out, int *tileNo, int transparent);

//
// Computes and stores the highest character index in the screen file and
// returns it.
//
int nscrGetHighestCharacter(NSCR *nscr);

//
// Call this function after filling out the RGB color info in the tile array.
// The function will associate each tile with its best fitting palette, index
// the tile with that palette, and perform optional dithering.
//
void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse);

//
// Same functionality as setupBgTiles, with the added ability to specify
// specific color balance settings.
//
void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Perform character compresion on the input array of tiles. After tiles are
// combined, the bit depth and palette settings are used to finalize the
// result in the tile array. progress must not be NULL, and ranges from 0-1000.
//
int performCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
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
void nscrCreate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse, 
				int palette, int nPalettes, int bin, int tileBase, int mergeTiles, int alignment,
				int paletteSize, int paletteOffsetm, int rowLimit, int nMaxChars,
				int color0Mode, int balance, int colorBalance, int enhanceColors,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max,
				NCLR *nclr, NCGR *ncgr, NSCR *nscr);
