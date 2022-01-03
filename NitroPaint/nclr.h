#pragma once
#include <Windows.h>
#include "color.h"
#include "filecommon.h"

#define NCLR_TYPE_INVALID 0
#define NCLR_TYPE_NCLR 1
#define NCLR_TYPE_HUDSON 2
#define NCLR_TYPE_BIN 3
#define NCLR_TYPE_NTFP 4
#define NCLR_TYPE_COMBO 5

extern LPCWSTR paletteFormatNames[];

typedef struct NCLR_ {
	OBJECT_HEADER header;
	int nBits;
	int nColors;
	int nPalettes; //for compressed palettes
	int totalSize; //for extended palettes
	int extPalette;
	short *idxTable;
	COLOR *colors;
	struct COMBO2D_ *combo2d; //for part of a combination file
} NCLR;

#include "combo2d.h"

//
// Initialize an NCLR structure with sensible defaults given a format.
//
void nclrInit(NCLR *nclr, int format);

//
// Determines if an byte array represents a valid Hudson palette file.
//
int nclrIsValidHudson(LPBYTE lpFile, int size);

//
// Determines if a byte array represents a valid raw palette file.
//
int nclrIsValidBin(LPBYTE lpFile, int size);

//
// Determines if a byte array represents a valid NTFP file.
//
int nclrIsValidNtfp(LPBYTE lpFile, int size);

//
// Reads an palette file from a byte array.
//
int nclrRead(NCLR * nclr, char * buffer, int size);

//
// Writes a palette to a file.
//
int nclrWriteFile(NCLR *nclr, LPWSTR name);

//
// Reads a palette from a file.
//
int nclrReadFile(NCLR *nclr, LPWSTR path);
