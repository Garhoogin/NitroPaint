#pragma once
#include <Windows.h>
#include "filecommon.h"

#define NANR_TYPE_INVALID 0
#define NANR_TYPE_NANR 1

extern LPCWSTR cellAnimationFormatNames[];

typedef struct ANIM_DATA_ {
	unsigned short index;
	unsigned short pad_;
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
	NANR_SEQUENCE *sequences;

	void *labl;
	int lablSize;
	void *uext;
	int uextSize;
} NANR;

int nanrIsValid(LPBYTE lpFile, int size);

int nanrRead(NANR *nanr, LPBYTE lpFile, int size);

int nanrReadFile(NANR *nanr, LPWSTR path);

int nanrWrite(NANR *nanr, BSTREAM *stream);

int nanrWriteFile(NANR *nanr, LPWSTR name);

void nanrFree(NANR *nanr);