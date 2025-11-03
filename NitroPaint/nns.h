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

unsigned char *NnsG2dFindBlockBySignature(const unsigned char *buffer, unsigned int size, const char *sig, int sigType, unsigned int *blockSize);

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

void NnsStreamAlign(NnsStream *stream, int to);

BSTREAM *NnsStreamGetBlockStream(NnsStream *stream);

void NnsStreamFinalize(NnsStream *stream);

void NnsStreamFlushOut(NnsStream *stream, BSTREAM *out);

void NnsStreamFree(NnsStream *stream);


// ----- ISCAD stream functions

int IscadScanFooter(const unsigned char *buffer, unsigned int size, const char *const *requiredBlocks, unsigned int nRequiredBlocks);
int IscadIsValidFooter(const unsigned char *footer, unsigned int size);
unsigned char *IscadFindBlockBySignature(const unsigned char *buffer, unsigned int size, const char *signature, unsigned int *pSize);


typedef struct IscadStream_ {
	BSTREAM stream;          // file stream
	int inFooter;            // in footer
	unsigned int blockStart; // current block starting offset
} IscadStream;

void IscadStreamCreate(IscadStream *stream);
void IscadStreamFree(IscadStream *stream);
void IscadStreamStartBlock(IscadStream *stream, const char *signature);
void IscadStreamEndBlock(IscadStream *stream);
void IscadStreamFinalize(IscadStream *stream);
void IscadStreamWrite(IscadStream *stream, const void *data, unsigned int len);
void IscadStreamWriteCountedString(IscadStream *stream, const char *str);
void IscadWriteBlock(IscadStream *stream, const char *signature, const void *data, unsigned int len);
void IscadStreamFlushOut(IscadStream *stream, BSTREAM *out);


// ----- Homebrew file format

#define GRF_TYPE_INVALID 0   // invalid
#define GRF_TYPE_GRF_10  1   // GRF 1.0 (devkiPro variant)
#define GRF_TYPE_GRF_20  2   // GRF 2.0 (BlocksDS variant)

int GrfIsValid(const unsigned char *buffer, unsigned int size);
unsigned char *GrfFindBlockBySignature(const unsigned char *buffer, unsigned int size, const char *signature, unsigned int *pSize);
unsigned char *GrfReadBlockUncompressed(const unsigned char *buffer, unsigned int size, const char *signature, unsigned int *pSize);
unsigned char *GrfGetHeader(const unsigned char *buffer, unsigned int size, unsigned int *pSize);

