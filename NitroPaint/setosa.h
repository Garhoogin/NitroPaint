#pragma once

#include <stdlib.h>
#include <stdint.h>

#include "bstream.h"

typedef struct SetStream_ {
	uint32_t nBlocks;
	BSTREAM headerStream;
	BSTREAM blockStream;
	BSTREAM currentStream;
} SetStream;

void SetStreamCreate(SetStream *stream);
void SetStreamStartBlock(SetStream *stream, const char *sig);
void SetStreamWrite(SetStream *stream, const void *data, unsigned int size);
void SetStreamEndBlock(SetStream *stream);
void SetStreamFinalize(SetStream *stream);
void SetStreamFlushOut(SetStream *stream, BSTREAM *out);
void SetStreamFree(SetStream *stream);



typedef struct SetResDirectory_ {
	int hasNames;
	int nObjects;
	BSTREAM dirStream;
	BSTREAM objStream;
	BSTREAM nameStream;
} SetResDirectory;

void SetResDirCreate(SetResDirectory *dir, int hasNames);
void SetResDirAdd(SetResDirectory *dir, const char *name, const void *data, unsigned int dataSize);
void SetResDirFinalize(SetResDirectory *dir);
void SetResDirFlushOut(SetResDirectory *dir, SetStream *out);
void SetResDirFree(SetResDirectory *dir);
