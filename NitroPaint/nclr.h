#pragma once
#include <Windows.h>
#include "color.h"

typedef struct NCLR_ {
	int nBits;
	int nColors;
	COLOR* colors;
	int isHudson;
} NCLR;

int nclrIsValidHudson(LPBYTE lpFile, int size);

int nclrRead(NCLR * nclr, char * buffer, int size);

void nclrWrite(NCLR * nclr, LPWSTR name);

void nclrCreate(DWORD * palette, int nColors, int nBits, int extended, LPWSTR name, int bin);

int nclrReadFile(NCLR *nclr, LPWSTR path);