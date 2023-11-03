#pragma once
#include <Windows.h>
#include "texture.h"
#include "filecommon.h"


typedef struct TdsFile_ {

	OBJECT_HEADER header;
	TEXTURE texture;

} TdsFile;

HWND CreateTdsViewer(HWND hWndParent, LPCWSTR path);

void TdsInit(TdsFile* tds);

int TdsRead(TdsFile* tds, char* buffer, int size);

int TdsIsValid(char* buffer, unsigned int size);

int TdsReadFile(TdsFile* tds, LPCWSTR path);

int TdsWriteFile(TdsFile* tds, LPWSTR filename);

int TdsWrite(TdsFile* tds, BSTREAM* stream);
