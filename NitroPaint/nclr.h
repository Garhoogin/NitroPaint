#pragma once
#include <Windows.h>
#include "color.h"
#include "filecommon.h"

#define NCLR_TYPE_INVALID    0
#define NCLR_TYPE_NCLR       1
#define NCLR_TYPE_NC         2
#define NCLR_TYPE_ISTUDIO    3
#define NCLR_TYPE_ISTUDIOC   4
#define NCLR_TYPE_HUDSON     5
#define NCLR_TYPE_BIN        6
#define NCLR_TYPE_NTFP       7
#define NCLR_TYPE_COMBO      8

extern LPCWSTR paletteFormatNames[];

typedef struct NCLR_ {
	OBJECT_HEADER header;
	int nBits;             // bit depth of graphics this palette is intended for
	int nColors;           // number of colors in the unpacked palette
	int extPalette;        // whether this palette is an extended palette or not
	int compressedPalette; // omit unused palettes from the file
	int g2dBug;            // replicate converter bug for compressed palettes
	COLOR *colors;         // raw color data
} NCLR;

#include "combo2d.h"

//
// Initialize an palette structure with sensible defaults given a format.
//
void PalInit(NCLR *nclr, int format);

//
// Determines if an byte array represents a valid Hudson palette file.
//
int PalIsValidHudson(const unsigned char *lpFile, unsigned int size);

//
// Determines if a byte array represents a valid raw palette file.
//
int PalIsValidBin(const unsigned char *lpFile, unsigned int size);

//
// Determines if a byte array represents a valid NTFP file.
//
int PalIsValidNtfp(const unsigned char *lpFile, unsigned int size);

//
// Reads an palette file from a byte array.
//
int PalRead(NCLR *nclr, const unsigned char *buffer, unsigned int size);

//
// Writes a palette to a stream.
//
int PalWrite(NCLR *nclr, BSTREAM *stream);

//
// Writes a palette to a file.
//
int PalWriteFile(NCLR *nclr, LPCWSTR name);

//
// Reads a palette from a file.
//
int PalReadFile(NCLR *nclr, LPCWSTR path);
