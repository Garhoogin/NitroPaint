#pragma once
#include <stdint.h>

#include "bstream.h"

// ----- NNS generic functions

int NnsHeaderIsValid(const unsigned char *buffer, unsigned int size);

int NnsIsValid(const unsigned char *buffer, unsigned int size);

// ----- NNS G2D functions

int NnsG2dIsValid(const unsigned char *buffer, unsigned int size);

int NnsG2dIsOld(const unsigned char *buffer, unsigned int size);

int NnsG2dGetNumberOfSections(const unsigned char *buffer, unsigned int size);

unsigned char *NnsG2dGetSectionByIndex(const unsigned char *buffer, unsigned int size, int index);

unsigned char *NnsG2dGetSectionByMagic(const unsigned char *buffer, unsigned int size, unsigned int sectionMagic);

// ----- NNS G3D functions

typedef char *(*NnsGetResourceNameCallback) (void *resource);

int NnsG3dIsValid(const unsigned char *buffer, unsigned int size);

unsigned char *NnsG3dGetSectionByMagic(const unsigned char *buffer, unsigned int size, const char *magic);

unsigned char *NnsG3dGetSectionByIndex(const unsigned char *buffer, unsigned int size, int index);

int NnsG3dWriteDictionary(BSTREAM *stream, void *resources, int itemSize, int nItems, NnsGetResourceNameCallback getName, int dictEntrySize);


// ----- NNS Stream functions

#define NNS_SIG_LE 0
#define NNS_SIG_BE 1

#define NNS_TYPE_G2D 0
#define NNS_TYPE_G3D 1

typedef struct NnsStream_ {
	unsigned char header[16];
	int g3d;
	int old;
	int sigByteorder;
	uint16_t nBlocks;
	BSTREAM headerStream;
	BSTREAM blockStream;
	BSTREAM currentStream;
} NnsStream;

void NnsStreamCreate(NnsStream *stream, const char *identifier, int versionHigh, int versionLow, int type, int sigByteOrder);

void NnsStreamStartBlock(NnsStream *stream, const char *identifier);

void NnsStreamEndBlock(NnsStream *stream);

void NnsStreamWrite(NnsStream *stream, const void *bytes, unsigned int size);

BSTREAM *NnsStreamGetBlockStream(NnsStream *stream);

void NnsStreamFinalize(NnsStream *stream);

void NnsStreamFlushOut(NnsStream *stream, BSTREAM *out);

void NnsStreamFree(NnsStream *stream);
