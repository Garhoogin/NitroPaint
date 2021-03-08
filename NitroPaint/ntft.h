#pragma once
#include "filecommon.h"
#include "color.h"

#define BMAP_TYPE_NTFT    1

typedef struct NTFT_ {
	OBJECT_HEADER header;
	int nPx;
	COLOR *px;
} NTFT;

int ntftIsValid(char *file, int size);

int ntftRead(NTFT *ntft, char *buffer, int size);

int ntftReadFile(NTFT *ntft, LPWSTR path);

int ntftCreate(NTFT *ntft, DWORD *px, int nPx);

int ntftWrite(NTFT *ntft, LPWSTR path);