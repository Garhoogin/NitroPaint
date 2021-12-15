#pragma once

typedef struct BTREAM_ {
	unsigned char *buffer;
	int bufferSize;
	int size;
	int pos;
} BSTREAM;

void bstreamCreate(BSTREAM *stream, unsigned char *init, int initSize);

void bstreamFree(BSTREAM *stream);

void bstreamWrite(BSTREAM *stream, unsigned char *data, int size);

int bstreamSeek(BSTREAM *stream, int pos, int relative);

