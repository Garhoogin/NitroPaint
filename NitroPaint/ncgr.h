#pragma once

#include "nclr.h"

#define NCGR_TYPE_INVALID	0
#define NCGR_TYPE_NCGR		1
#define NCGR_TYPE_HUDSON	2
#define NCGR_TYPE_HUDSON2	3
#define NCGR_TYPE_NCBR      4
#define NCGR_TYPE_BIN       5
#define NCGR_TYPE_COMBO     6

#define GX_OBJVRAMMODE_CHAR_2D        0x000000
#define GX_OBJVRAMMODE_CHAR_1D_32K    0x000010
#define GX_OBJVRAMMODE_CHAR_1D_64K    0x100010
#define GX_OBJVRAMMODE_CHAR_1D_128K   0x200010
#define GX_OBJVRAMMODE_CHAR_1D_256K   0x300010

#define NCGR_2D(m)              ((m)==GX_OBJVRAMMODE_CHAR_2D)
#define NCGR_1D(m)              (!NCGR_2D(m))
#define NCGR_BYTE_BOUNDARY(m)   (1<<((((m)>>20)&0x7)+5))
#define NCGR_BOUNDARY(n)        (NCGR_BYTE_BOUNDARY((n)->mappingMode)/((n)->nBits<<3))

extern LPCWSTR characterFormatNames[];

typedef struct NCGR_{
	OBJECT_HEADER header;
	int nTiles;
	int tilesX;
	int tilesY;
	int mappingMode;
	int nBits;
	int tileWidth;
	BYTE **tiles;
	struct COMBO2D_ *combo2d; //for combination files
} NCGR;

#include "combo2d.h"

int calculateWidth(int nTiles);

int ncgrIsValidHudson(LPBYTE buffer, int size);

int ncgrIsValidBin(LPBYTE buffer, int size);

int ncgrGetTile(NCGR * ncgr, NCLR * nclr, int x, int y, DWORD * out, int previewPalette, BOOL drawChecker, BOOL transparent);

int ncgrReadFile(NCGR *ncgr, LPCWSTR path);

int ncgrWrite(NCGR *ncgr, BSTREAM *stream);

int ncgrWriteFile(NCGR * ncgr, LPWSTR name);

void ncgrCreate(DWORD * blocks, int nBlocks, int nBits, LPWSTR name, int fmt);