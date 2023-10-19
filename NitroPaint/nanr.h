#pragma once
#include <Windows.h>
#include "filecommon.h"

#define NANR_TYPE_INVALID    0
#define NANR_TYPE_NANR       1
#define NANR_TYPE_GHOSTTRICK 2

extern LPCWSTR cellAnimationFormatNames[];

typedef struct ANIM_DATA_ {
	unsigned short index;
} ANIM_DATA;

typedef struct ANIM_DATA_SRT_ {
	unsigned short index;
	unsigned short rotZ;
	int sx;
	int sy;
	short px;
	short py;
} ANIM_DATA_SRT;

typedef struct ANIM_DATA_T_ {
	unsigned short index;
	unsigned short pad_;
	short px;
	short py;
} ANIM_DATA_T;

typedef struct FRAME_DATA_ {
	void *animationData;
	unsigned short nFrames;
	unsigned short pad_;
} FRAME_DATA;

typedef struct NANR_SQUENCE_ {
	unsigned short nFrames;
	unsigned short startFrameIndex;
	int type;
	int mode;
	FRAME_DATA *frames;
} NANR_SEQUENCE;


typedef struct NANR_ {
	OBJECT_HEADER header;

	int nSequences;
	NANR_SEQUENCE *sequences;  //animation sequences
	int **seqVramTransfers;    //VRAM transfer arrays (one per frame per sequence)

	void *labl;
	int lablSize;
	void *uext;
	int uextSize;
} NANR;

int AnmIsValidGhostTrick(const unsigned char *buffer, unsigned int size);

int AnmIsValidNanr(const unsigned char *lpFile, unsigned int size);

int AnmRead(NANR *nanr, const unsigned char *lpFile, unsigned int size);

int AnmReadFile(NANR *nanr, LPCWSTR path);

int AnmWrite(NANR *nanr, BSTREAM *stream);

int AnmWriteFile(NANR *nanr, LPWSTR name);

void AnmFree(NANR *nanr);
