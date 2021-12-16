#pragma once
#include "compression.h"

typedef struct BTREAM_ {
	unsigned char *buffer;
	int bufferSize;
	int size;
	int pos;
} BSTREAM;

void bstreamCreate(BSTREAM *stream, void *init, int initSize);

void bstreamFree(BSTREAM *stream);

void bstreamWrite(BSTREAM *stream, void *data, int size);

int bstreamSeek(BSTREAM *stream, int pos, int relative);

int bstreamCompress(BSTREAM *stream, int algorithm, int start, int size);
