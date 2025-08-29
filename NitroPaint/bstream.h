#pragma once

typedef enum BstreamStatus_ {
	BSTREAM_STATUS_OK      = 0,
	BSTREAM_STATUS_NOMEM   = 1
} BstreamStatus;

typedef struct BTREAM_ {
	unsigned char *buffer;
	unsigned int bufferSize;
	unsigned int size;
	unsigned int pos;
} BSTREAM;

BstreamStatus bstreamCreate(BSTREAM *stream, const void *init, unsigned int initSize);

BstreamStatus bstreamFree(BSTREAM *stream);

BstreamStatus bstreamWrite(BSTREAM *stream, const void *data, unsigned int size);

BstreamStatus bstreamAlign(BSTREAM *stream, unsigned int by);

int bstreamSeek(BSTREAM *stream, int pos, int relative);

BstreamStatus bstreamTruncate(BSTREAM *stream, unsigned int to);

unsigned char *bstreamToByteArray(BSTREAM *stream, unsigned int *pSize);
