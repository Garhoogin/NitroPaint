#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bstream.h"
#include "filecommon.h"

void bstreamCreate(BSTREAM *stream, void *init, int initSize) {
	stream->buffer = NULL;
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

void bstreamWrite(BSTREAM *stream, void *data, int dataSize) {
	if (data == NULL || dataSize == 0) return;

	//determine required size for buffer. Can write in the middle of the stream, beware!
	int requiredSize = max(stream->pos + dataSize, stream->size);
	if (stream->bufferSize < requiredSize) {

		//keep expanding by 1.5x until it's big enough
		int newSize = stream->bufferSize;
		while (newSize < requiredSize) {
			newSize = (newSize + 2) * 3 / 2;
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

int bstreamCompress(BSTREAM *stream, int algorithm, int start, int size) {
	//checks
	if (size == 0) {
		size = stream->size - start;
	}
	if (start + size > stream->size) {
		size = stream->size - start;
	}
	char *src = stream->buffer + start;

	//compress section
	char *compressed = NULL;
	int compressedSize = size;
	compressed = compress(src, size, algorithm, &compressedSize);

	//insert section
	int beforeCompressed = start;
	int afterCompressed = stream->size - start - size;
	int bufferSize = beforeCompressed + compressedSize + afterCompressed;
	char *newBuffer = malloc(bufferSize);
	memcpy(newBuffer, stream->buffer, beforeCompressed);
	memcpy(newBuffer + beforeCompressed, compressed, compressedSize);
	memcpy(newBuffer + beforeCompressed + compressedSize, stream->buffer + start + size, afterCompressed);
	free(stream->buffer);
	stream->buffer = newBuffer;
	stream->bufferSize = bufferSize;
	stream->size = bufferSize;

	if (compressed != NULL) free(compressed);
	return compressedSize;
}
