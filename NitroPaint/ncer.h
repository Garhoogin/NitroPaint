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
	int x;
	int y;
	int characterName;
	int palette;
	int width;
	int height;
	int priority;
	int shape;
	int characterBits;
	int mosaic;
	int flipX;
	int flipY;
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

void ncerWrite(NCER *ncer, LPWSTR name);