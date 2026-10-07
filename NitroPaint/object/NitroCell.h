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

#include "NitroCharacter.h"
#include "NitroPalette.h"


// ----- cell bank format types

#define NCER_TYPE_INVALID    0
#define NCER_TYPE_NCER       1
#define NCER_TYPE_SETOSA     2
#define NCER_TYPE_HUDSON     3
#define NCER_TYPE_BOMBERMAN  4
#define NCER_TYPE_GHOSTTRICK 5


// -----------------------------------------------------------------------------------------------
// Name: enum CellCompressionMode
//
// This indicates the kind of OBJ compression used when assembling a 1D mapped cell data bank.
// This controls how graphics are optimized in that process.
//
// Boolean values:
//   CELL_COMPRESS_NONE          OBJ graphics are not combined.
//   CELL_COMPRESS_CELL          OBJ graphics are overlapped with matching character data within
//                               the cell. This keeps graphics for different cells separate.
//   CELL_COMPRESS_FILE          OBJ graphics are able to be merged across cells. This allows
//                               different cells to referene the same characters. This option
//                               should not be used with VRAM transfer characters.
// -----------------------------------------------------------------------------------------------
typedef enum CellCompressionMode_ {
	CELL_COMPRESS_NONE,  // No compression
	CELL_COMPRESS_CELL,  // Cell compression
	CELL_COMPRESS_FILE   // File compression
} CellCompressionMode;

typedef struct NCER_CELL_ {
	int nAttribs;
	int cellAttr;
	uint32_t attrEx; // UCAT extended attribute data

	int maxX;
	int maxY;
	int minX;
	int minY;
	
	uint16_t *attr;         // raw OAM attribute info array
	uint32_t *exCharNames;  // extended character name

	int forbidCompression; // forbids compression of graphics
} NCER_CELL;

typedef struct GxOamAttrInfo_ {
	//Attribute 0
	int y;				//
	int rotateScale;	//
	int doubleSize;		//
	int disable;		//
	int mode;			//
	int mosaic;			//
	int characterBits;	//
	int shape;			//

	//Attribute 1
	int x;				//
	int matrix;
	int flipX;			//
	int flipY;			//
	int size;			//

	//Attribute 2
	int characterName;	//
	int priority;		//
	int palette;		//

	//Convenience
	int width;
	int height;
} GxOamAttrInfo;

typedef struct NCER_ {
	ObjHeader header;                  // object header
	int nCells;                        // number of cells in cell bank
	int bankAttribs;                   // cell bank attribute
	int mappingMode;                   // cell mapping mode
	NCER_CELL *cells;                  // list of cells

	int useVramTransferCharacters;     // indicates the use of VRAM transfer characters
	int useExtAttr;                    // use NCER extended attributes
	int isEx2d;                        // use of pseudo extended 2D mapping
	int ex2dBaseMappingMode;           // base mapping mode when extended 2D is used

	int uextSize;                      // size of UEXT
	char *uext;                        // UEXT
	int lablSize;                      // size of LABL
	char *labl;                        // LABL
} NCER;

void CellRegisterFormats(void);


void CellInitBankCell(NCER *ncer, NCER_CELL *cell, int nObj);

void CellInsertOBJ(NCER *ncer, NCER_CELL *cell, int index, int nObj);

void CellDeleteOBJ(NCER *ncer, NCER_CELL *cell, int index, int nObj);

void CellGetObjDimensions(int shape, int size, int *width, int *height);

int CellDecodeOamAttributes(GxOamAttrInfo *info, NCER_CELL *cell, int oam);

int CellFree(ObjHeader *header);

void CellDeleteCell(NCER *ncer, int idx);

void CellMoveCellIndex(NCER *ncer, int iSrc, int iDst);

unsigned int CellGetCharacterName(NCER_CELL *cell, int i);


// ----- render cell

void CellRender(
	COLOR32   *px,             // output 512x256 pixel buffer
	int       *covbuf,         // output coverage buffer (optional)
	NCER      *ncer,           // cell data bank
	NCGR      *ncgr,           // character graphics
	NCLR      *nclr,           // color palette
	NCER_CELL *cell,           // cell to render
	int        xOffs,          // horizontal displacement of render
	int        yOffs,          // vertical displacement of render
	double     a,              // affine parameter A
	double     b,              // affine parameter B
	double     c,              // affine parameter C
	double     d,              // affine parameter D
	int        forceAffine,    // forces all OBJ to be in affine mode
	int        forceDoubleSize // forces all affine OBJ to be in double size mode
);
