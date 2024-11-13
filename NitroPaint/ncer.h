#pragma once
#include <Windows.h>
#include "ncgr.h"
#include "nclr.h"

#define NCER_TYPE_INVALID    0
#define NCER_TYPE_NCER       1
#define NCER_TYPE_HUDSON     2
#define NCER_TYPE_GHOSTTRICK 3

extern LPCWSTR cellFormatNames[];

typedef struct NCER_CELL_ {
	int nAttribs;
	int cellAttr;

	int maxX;
	int maxY;
	int minX;
	int minY;
	
	uint16_t *attr;
} NCER_CELL;

typedef struct NCER_CELL_INFO_ {
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
} NCER_CELL_INFO;

typedef struct NCER_ {
	OBJECT_HEADER header;
	int nCells;
	int bankAttribs;
	int mappingMode;
	NCER_CELL *cells;

	CHAR_VRAM_TRANSFER *vramTransfer;
	int nVramTransferEntries;

	int uextSize;
	char *uext;
	int lablSize;
	char *labl;
} NCER;

void CellInit(NCER *ncer, int format);

int CellIdentify(const unsigned char *buffer, unsigned int size);

int CellIsValidHudson(const unsigned char *buffer, unsigned int size);

int CellIsValidGhostTrick(const unsigned char *buffer, unsigned int size);

int CellIsValidNcer(const unsigned char *buffer, unsigned int size);

int CellRead(NCER *ncer, const unsigned char *buffer, unsigned int size);

int CellReadFile(NCER *ncer, LPCWSTR path);

void CellGetObjDimensions(int shape, int size, int *width, int *height);

int CellDecodeOamAttributes(NCER_CELL_INFO *info, NCER_CELL *cell, int oam);

int CellFree(OBJECT_HEADER *header);

void CellRenderObj(NCER_CELL_INFO *info, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, COLOR32 *out);

COLOR32 *CellRenderCell(COLOR32 *px, NCER_CELL *cell, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, int xOffs, int yOffs, float a, float b, float c, float d);

void CellDeleteCell(NCER *ncer, int idx);

void CellMoveCellIndex(NCER *ncer, int iSrc, int iDst);

int CellWrite(NCER *ncer, BSTREAM *stream);

int CellWriteFile(NCER *ncer, LPWSTR name);
