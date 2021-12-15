#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bstream.h"

void bstreamCreate(BSTREAM *stream, unsigned char *init, int initSize) {
	if (stream->buffer != NULL) {
		free(stream->buffer);
		stream->buffer = NULL;
	}
	stream->bufferSize = 0;
	stream->size = 0;
	stream->pos = 0;

	if (initSize && init != NULL) {
		stream->buffer = malloc(initSize);
		memcpy(stream->buffer, init, initSize);
		stream->bufferSize = initSize;
		stream->size = initSize;
		stream->pos = 0;
	}
}

void bstreamFree(BSTREAM *stream) {
	if (stream->buffer != NULL) {
		free(stream->buffer);
		stream->buffer = NULL;
	}

	stream->pos = 0;
	stream->size = 0;
	stream->bufferSize = 0;
}

void bstreamWrite(BSTREAM *stream, unsigned char *data, int dataSize) {
	if (data == NULL || dataSize == 0) return;

	//determine required size for buffer. Can write in the middle of the stream, beware!
	int requiredSize = max(stream->pos + dataSize, stream->size);
	if (stream->bufferSize < requiredSize) {

		//keep expanding by 1.5x until it's big enough
		int newSize = stream->bufferSize;
		while (newSize < requiredSize) {
			newSize = (stream->bufferSize + 2) * 3 / 2;
		}
		stream->bufferSize = newSize;
		stream->buffer = realloc(stream->buffer, newSize);
	}

	//write data to stream.
	memcpy(stream->buffer + stream->pos, data, dataSize);
	stream->pos += dataSize;
	stream->size = requiredSize;
}

int bstreamSeek(BSTREAM *stream, int pos, int relative) {
	int newPos = stream->pos;
	if (relative) newPos += pos;
	else newPos = pos;

	//bounds check!
	if (newPos < 0) newPos = 0;
	if (newPos > stream->size) newPos = stream->size;

	stream->pos = newPos;
	return newPos;
}
