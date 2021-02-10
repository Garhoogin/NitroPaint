#pragma once
#include "ncgr.h"
#include "nclr.h"

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
#define NSCR_TYPE_HUDSON	2
#define NSCR_TYPE_HUDSON2	3

typedef struct NSCR_ {
	OBJECT_HEADER header;
	DWORD nWidth;
	DWORD nHeight;
	DWORD dataSize;
	WORD *data;
	int fmt;
	int nHighestIndex;//weird hack
} NSCR;

int nscrIsValidHudson(LPBYTE buffer, int size);

int nscrRead(NSCR * nscr, char * file, DWORD dwFileSize);

DWORD * toBitmap(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int * width, int * height);

void nscrWrite(NSCR *nscr, LPWSTR name);

int nscrGetTile(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int x, int y, BOOL chceker, DWORD * out);

int nscrGetTileEx(NSCR * nscr, NCGR * ncgr, NCLR * nclr, int x, int y, BOOL checker, DWORD * out, int *tileNo);

void nscrCreate(DWORD * imgBits, int width, int height, int nBits, int dither, LPWSTR lpszNclrLocation, LPWSTR lpszNcgrLocation, LPWSTR lpszNscrLocation, int palette, int nPalettes, int bin);