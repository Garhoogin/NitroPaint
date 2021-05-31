#pragma once
#include <Windows.h>
#include "texture.h"

typedef struct {
	DWORD *px;
	int width;
	int height;
	int fmt;
	int dither;
	int ditherAlpha;
	int colorEntries;
	int threshold;
	TEXTURE *dest;
	void (*callback) (void *);
	void *callbackParam;
	char pnam[17];
} CREATEPARAMS;

int countColors(DWORD *px, int nPx);

int convertDirect(CREATEPARAMS *params);

int convertPalette(CREATEPARAMS *params);

int convertTranslucent(CREATEPARAMS *params);

//progress markers for convert4x4.
extern volatile _globColors;
extern volatile _globFinal;
extern volatile _globFinished;

int convert4x4(CREATEPARAMS *params);

//to convert a texture directly. lpParam is a CREATEPARAMS struct pointer.
DWORD CALLBACK startConvert(LPVOID lpParam);

void threadedConvert(DWORD *px, int width, int height, int fmt, BOOL dither, BOOL ditherAlpha, int colorEntries, int threshold, char *pnam, TEXTURE *dest, void (*callback) (void *), void *callbackParam);