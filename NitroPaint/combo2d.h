#pragma once
#include "filecommon.h"

#define COMBO2D_TYPE_INVALID     0
#define COMBO2D_TYPE_TIMEACE     1
#define COMBO2D_TYPE_POWERBIKE   2
#define COMBO2D_TYPE_SHTXDS      3

//structure that manages a linkage of palette, graphics, and screen.
typedef struct COMBO2D_ {
	OBJECT_HEADER header;
	struct NCLR_ *nclr;
	struct NCGR_ *ncgr;
	struct NSCR_ *nscr;
} COMBO2D;

//these lines cause chaos :(
//#include "nclr.h"
//#include "ncgr.h"
//#include "nscr.h"

int combo2dIsValid(BYTE *buffer, int size);

void combo2dWrite(COMBO2D *combo, LPWSTR path);
