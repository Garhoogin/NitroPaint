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

#define NSCR_TYPE_INVALID	0
#define NSCR_TYPE_NSCR		1
#define NSCR_TYPE_NC        2
#define NSCR_TYPE_IC        3
#define NSCR_TYPE_AC        4
#define NSCR_TYPE_HUDSON	5
#define NSCR_TYPE_HUDSON2	6
#define NSCR_TYPE_BIN       7
#define NSCR_TYPE_COMBO     8

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
	int colorMode;
	int fmt;
	int nHighestIndex;   // weird hack
	uint16_t clearValue; // default tile value
	int showGrid;        // for NC
	short gridWidth;     // for NC
	short gridHeight;    // for NC
} NSCR;

#include "combo2d.h"

//
// Initialize an NSCR structure with sensible values.
//
void ScrInit(NSCR *nscr, int format);

//
// Determines if a byte array represents a valid Hudson screen file.
//
int ScrIsValidHudson(const unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid raw screen file.
//
int ScrIsValidBin(const unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid IS-ACG-CHARACTER screen file.
//
int ScrIsValidAsc(const unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid IS-IRIS-CHARACTER screen file.
//
int ScrIsValidIsc(const unsigned char *buffer, unsigned int size);

//
// Idenfities the format of this screen data.
//
int ScrIdentify(const unsigned char *file, unsigned int size);

//
// Reads a screen file from an array.
//
int ScrRead(NSCR *nscr, const unsigned char *file, unsigned int dwFileSize);

//
// Reads a screen from a file.
//
int ScrReadFile(NSCR *nscr, LPCWSTR path);

//
// Write a screen to a stream.
//
int ScrWrite(NSCR *nscr, BSTREAM *stream);

//
// Write a screen to a file.
//
int ScrWriteFile(NSCR *nscr, LPCWSTR name);

//
// Render a single tile of a screen to 32-bit output, with respect to character
// base for quirks in some game setups.
//
int nscrGetTileEx(NSCR *nscr, NCGR *ncgr, NCLR *nclr, int tileBase, int x, int y, COLOR32 *out, int *tileNo, int transparent);

//
// Computes and stores the highest character index in the screen file and
// returns it.
//
int ScrComputeHighestCharacter(NSCR *nscr);
