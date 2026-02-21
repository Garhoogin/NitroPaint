#pragma once
#include "color.h"
#include "filecommon.h"

#define NCLR_TYPE_INVALID     0
#define NCLR_TYPE_NCLR        1
#define NCLR_TYPE_NC          2
#define NCLR_TYPE_ISTUDIO     3
#define NCLR_TYPE_ISTUDIOC    4
#define NCLR_TYPE_TOSE        5
#define NCLR_TYPE_HUDSON      6
#define NCLR_TYPE_SETOSA      7
#define NCLR_TYPE_BIN         8
#define NCLR_TYPE_NTFP        9
#define NCLR_TYPE_COMBO      10

typedef struct NCLR_ {
	ObjHeader header;
	int nBits;             // bit depth of graphics this palette is intended for
	int nColors;           // number of colors in the unpacked palette
	int extPalette;        // whether this palette is an extended palette or not
	int compressedPalette; // omit unused palettes from the file
	int g2dBug;            // replicate converter bug for compressed palettes
	COLOR *colors;         // raw color data
} NCLR;


void PalRegisterFormats(void);

//
// Determine the file format of a palette file.
//
int PalIdentify(const unsigned char *buffer, unsigned int size);

//
// Determines if a byte array represents a valid raw palette file.
//
int PalIsValidBin(const unsigned char *lpFile, unsigned int size);
int PalIsValidNtfp(const unsigned char *lpFile, unsigned int size);

//
// Reads an palette file from a byte array.
//
int PalRead(NCLR *nclr, const unsigned char *buffer, unsigned int size);

//
// Writes a palette to a stream.
//
int PalWrite(NCLR *nclr, BSTREAM *stream);

