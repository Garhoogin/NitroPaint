#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bstream.h"

BstreamStatus bstreamCreate(BSTREAM *stream, const void *init, unsigned int initSize) {
	stream->buffer = NULL;
	stream->bufferSize = 0;
	stream->size = 0;
	stream->pos = 0;

	if (initSize && init != NULL) {
		stream->buffer = malloc(initSize);
		if (stream->buffer == NULL) return BSTREAM_STATUS_NOMEM;

		memcpy(stream->buffer, init, initSize);
		stream->bufferSize = initSize;
		stream->size = initSize;
		stream->pos = 0;
	}
	return BSTREAM_STATUS_OK;
}

BstreamStatus bstreamFree(BSTREAM *stream) {
	if (stream->buffer != NULL) {
		free(stream->buffer);
		stream->buffer = NULL;
	}

	stream->pos = 0;
	stream->size = 0;
	stream->bufferSize = 0;
	return BSTREAM_STATUS_OK;
}

BstreamStatus bstreamWrite(BSTREAM *stream, const void *data, unsigned int dataSize) {
	if (data == NULL || dataSize == 0) return BSTREAM_STATUS_OK;

	//determine required size for buffer. Can write in the middle of the stream, beware!
	unsigned int requiredSize = max(stream->pos + dataSize, stream->size);
	if (stream->bufferSize < requiredSize) {

		//keep expanding by 1.5x until it's big enough
		unsigned int newSize = stream->bufferSize;
		while (newSize < requiredSize) {
			newSize = (newSize + 2) * 3 / 2;
		}

		void *newbuf = realloc(stream->buffer, newSize);
		if (newbuf == NULL) {
			//no memory error, try reducing allocation size
			newSize = requiredSize;
			newbuf = realloc(stream->buffer, newSize);
			if (newbuf == NULL) {
				return BSTREAM_STATUS_NOMEM;
			}
		}

		stream->bufferSize = newSize;
		stream->buffer = newbuf;
	}

	//write data to stream.
	memcpy(stream->buffer + stream->pos, data, dataSize);
	stream->pos += dataSize;
	stream->size = requiredSize;
	return BSTREAM_STATUS_OK;
}

BstreamStatus bstreamAlign(BSTREAM *stream, unsigned int by) {
	//temp buffer for fill
	unsigned char buf[32] = { 0 };
	unsigned int required = by - (stream->pos % by);

	//if already aligned, required==by.
	if (required == by) return BSTREAM_STATUS_OK;

	while (required > 0) {
		int nFill = required;
		if (nFill > sizeof(buf)) nFill = sizeof(buf);

		BstreamStatus status = bstreamWrite(stream, buf, nFill);
		required -= nFill;
		if (status != BSTREAM_STATUS_OK) return status;
	}
	return BSTREAM_STATUS_OK;
}

int bstreamSeek(BSTREAM *stream, int pos, int relative) {
	unsigned int newPos = stream->pos;
	if (relative) newPos += pos;
	else newPos = pos;

	//bounds check!
	if (newPos < 0) newPos = 0;
	if (newPos > stream->size) newPos = stream->size;

	stream->pos = newPos;
	return newPos;
}

BstreamStatus bstreamTruncate(BSTREAM *stream, unsigned int to) {
	if (to >= stream->size) return BSTREAM_STATUS_OK;

	stream->size = to;
	return BSTREAM_STATUS_OK;
}

unsigned char *bstreamToByteArray(BSTREAM *stream, unsigned int *pSize) {
	//shrink buffer size
	unsigned char *outbuf = realloc(stream->buffer, stream->size);
	if (outbuf == NULL) outbuf = stream->buffer;

	*pSize = stream->size;
	stream->buffer = NULL;
	stream->bufferSize = 0;
	stream->pos = 0;
	stream->size = 0;

	return outbuf;
}
