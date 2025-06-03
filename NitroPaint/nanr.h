#pragma once
#include <Windows.h>

#include "filecommon.h"
#include "ncer.h"
#include "ncgr.h"
#include "nclr.h"
#include "color.h"

#define NANR_TYPE_INVALID    0
#define NANR_TYPE_NANR       1
#define NANR_TYPE_GHOSTTRICK 2
#define NANR_TYPE_COMBO      3


#define NANR_SEQ_TYPE_INDEX         0
#define NANR_SEQ_TYPE_INDEX_SRT     1
#define NANR_SEQ_TYPE_INDEX_T       2

#define NANR_SEQ_TYPE_CELL          1
#define NANR_SEQ_TYPE_MULTICELL     2

#define NANR_SEQ_MODE_FORWARD       1
#define NANR_SEQ_MODE_FORWARD_LOOP  2
#define NANR_SEQ_MODE_BACKWARD      3
#define NANR_SEQ_MODE_BACKWARD_LOOP 4

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

void AnmInit(NANR *nanr, int format);

int AnmIsValidGhostTrick(const unsigned char *buffer, unsigned int size);

int AnmIsValidNanr(const unsigned char *lpFile, unsigned int size);

int AnmRead(NANR *nanr, const unsigned char *lpFile, unsigned int size);

int AnmReadFile(NANR *nanr, LPCWSTR path);

int AnmWrite(NANR *nanr, BSTREAM *stream);

int AnmWriteFile(NANR *nanr, LPWSTR name);

void AnmFree(OBJECT_HEADER *obj);


// ----- animation operations

int AnmGetAnimFrame(
	NANR          *nanr,     // animation bank
	int            iSeq,     // index of sequence
	int            iFrm,     // index of frame
	ANIM_DATA_SRT *pFrame,   // output frame data (optional)
	int           *pDuration // output frame duration (optional)
);

void AnmCalcTransformMatrix(
	double  centerX,         // center point X of transformation
	double  centerY,         // center point Y of transformation
	double  scaleX,          // 1. scale X
	double  scaleY,          // 1. scale Y
	double  rotZ,            // 2. rotation (radians)
	double  transX,          // 3. translation X
	double  transY,          // 3. translation Y
	double *pMtx,            // -> output transformation matrix
	double *pTrans           // -> output translation vector
);

void AnmRenderSequenceFrame(
	COLOR32 *dest,           // output buffer 512x256
	NANR    *nanr,           // animation bank
	NCER    *ncer,           // cell data bank
	NCGR    *ncgr,           // character graphics
	NCLR    *nclr,           // color palette
	int      iSeq,           // index of animation sequence
	int      iFrm,           // index of frame
	int      x,              // X displacement
	int      y,              // Y displacement
	int      forceAffine,    // force affine mode for all OBJ
	int      forceDoubleSize // force double size for all affine OBJ
);
