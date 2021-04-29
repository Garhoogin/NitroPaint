#pragma once
#include <Windows.h>
#include "ncgr.h"
#include "nclr.h"

#define NCER_TYPE_NCER 0
#define NCER_TYPE_HUDSON 1

typedef struct NCER_CELL_ {
	int nAttribs;
	int cellAttr;

	int maxX;
	int maxY;
	int minX;
	int minY;
	
	int nAttr;
	WORD *attr;
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
	NCER_CELL *cells;

	int uextSize;
	char *uext;
	int lablSize;
	char *labl;
} NCER;

int ncerIsValid(char *buffer, int size);

int ncerIsValidHudson(char *buffer, int size);

int ncerRead(NCER *ncer, char *buffer, int size);

int ncerReadFile(NCER *ncer, LPWSTR path);

int decodeAttributes(NCER_CELL_INFO *info, NCER_CELL *cell);

int decodeAttributesEx(NCER_CELL_INFO *info, NCER_CELL *cell, int oam);

int ncerFree(NCER *ncer);

DWORD *ncerCellToBitmap(NCER_CELL_INFO *info, NCGR * ncgr, NCLR * nclr, int * width, int * height, int checker);

DWORD *ncerRenderWholeCell(NCER_CELL *cell, NCGR *ncgr, NCLR *nclr, int xOffs, int yOffs, int *width, int *height, int checker, int outline);

void ncerWrite(NCER *ncer, LPWSTR name);