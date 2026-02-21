#pragma once

#include "nclr.h"

#define NCGR_TYPE_INVALID	  0
#define NCGR_TYPE_NCGR		  1
#define NCGR_TYPE_NC          2
#define NCGR_TYPE_IC          3
#define NCGR_TYPE_AC          4
#define NCGR_TYPE_TOSE        5
#define NCGR_TYPE_HUDSON	  6
#define NCGR_TYPE_HUDSON2	  7
#define NCGR_TYPE_GHOSTTRICK  8
#define NCGR_TYPE_SETOSA      9
#define NCGR_TYPE_BIN        10
#define NCGR_TYPE_COMBO      11

#define GX_OBJVRAMMODE_CHAR_2D        0x000000
#define GX_OBJVRAMMODE_CHAR_1D_32K    0x000010
#define GX_OBJVRAMMODE_CHAR_1D_64K    0x100010
#define GX_OBJVRAMMODE_CHAR_1D_128K   0x200010
#define GX_OBJVRAMMODE_CHAR_1D_256K   0x300010

#define NCGR_2D(m)              ((m)==GX_OBJVRAMMODE_CHAR_2D)
#define NCGR_1D(m)              (!NCGR_2D(m))
#define NCGR_BYTE_BOUNDARY(m)   (1<<((((m)>>20)&0x7)+5))
#define NCGR_BOUNDARY(n,x)      (NCGR_BYTE_BOUNDARY((n)->mappingMode)*(x)/(((n)->nBits)<<3))
#define NCGR_CHNAME(x,m,b)      (NCGR_BYTE_BOUNDARY(m)*(x)/((b)<<3))

typedef struct CHAR_SLICE_ {
	unsigned int offset;
	unsigned int size;
} CHAR_SLICE;

typedef struct NCGR_{
	ObjHeader header;
	int nTiles;
	int tilesX;
	int tilesY;
	int mappingMode;
	int bitmap;
	int nBits;
	int extPalette;           // whether character is using an extended palette
	unsigned char *attr;      // per-character palette attribute data
	int isExChar;             // is extended character data
	int isIntermediate;       // is intermediate file?
	unsigned char **tiles;
	CHAR_SLICE *slices;       // for Ghost Trick files
	int nSlices;              // for Ghost Trick files
} NCGR;

typedef struct CHAR_VRAM_TRANSFER_ {
	unsigned int srcAddr; //source address in bytes
	unsigned int dstAddr; //destination address in bytes
	unsigned int size;    //size in bytes
} CHAR_VRAM_TRANSFER;


void ChrRegisterFormats(void);


//
// Calculates a sensible width given a character count.
//
int ChrGuessWidth(int nTiles);

//
// Determines the validity of this file being a raw character graphics file.
//
int ChrIsValidBin(const unsigned char *buffer, unsigned int size);

//
// Get a 32-bit color render of graphics data
//
int ChrRenderCharacter(NCGR *ncgr, NCLR *nclr, int chNo, COLOR32 *out, int previewPalette, int transparent);

//
// Get single character, respecting VRAM transfer operations
//
void ChrGetChar(NCGR *ncgr, int chno, CHAR_VRAM_TRANSFER *transfer, unsigned char *out);

//
// Render character respecting a VRAM transfer operation.
//
int ChrRenderCharacterTransfer(NCGR *ncgr, NCLR *nclr, int chNo, CHAR_VRAM_TRANSFER *transfer, COLOR32 *out, int palette, int transparent);

//
// Update the width of graphics data. Useful for bitmapped graphics.
//
void ChrSetWidth(NCGR *ncgr, int width);

//
// Read character data from binary data and store in an NCGR.
//
void ChrReadChars(NCGR *ncgr, const unsigned char *buffer);

//
// Rad bitmap data from binary data and store in an NCGR.
//
void ChrReadBitmap(NCGR *ncgr, const unsigned char *buffer);

//
// Read either bitmap or chactacter graphics.
//
void ChrReadGraphics(NCGR *ncgr, const unsigned char *buffer);

//
// Read character graphics from a byte array.
//
int ChrRead(NCGR *ncgr, const unsigned char *buffer, unsigned int size);

//
// Write character data to stream.
//
void ChrWriteChars(NCGR *ncgr, BSTREAM *stream);

//
// Write graphics data to stream.
//
void ChrWriteGraphics(NCGR *ncgr, BSTREAM *stream);

//
// Write character graphics to a stream.
//
int ChrWrite(NCGR *ncgr, BSTREAM *stream);

//
// Change the bit depth of character graphics.
//
void ChrSetDepth(NCGR *ncgr, int depth);

//
// Resize the grphics area of character graphics.
//
void ChrResize(NCGR *ncgr, int width, int height);
