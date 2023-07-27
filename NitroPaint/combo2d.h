#pragma once
#include "filecommon.h"

#define COMBO2D_TYPE_INVALID     0
#define COMBO2D_TYPE_TIMEACE     1
#define COMBO2D_TYPE_BANNER      2
#define COMBO2D_TYPE_DATAFILE    3
#define COMBO2D_TYPE_5BG         4

//structure that manages a linkage of palette, graphics, and screen.
typedef struct COMBO2D_ {
	OBJECT_HEADER header;
	struct NCLR_ *nclr;
	struct NCGR_ *ncgr;
	struct NSCR_ *nscr;
	void *extraData; //depends on the type, store data we're not interested in particularly
} COMBO2D;

typedef struct DATAFILECOMBO_ {
	char *data;
	int size;
	int pltOffset;
	int pltSize;
	int chrOffset;
	int chrSize;
	int scrOffset;
	int scrSize;
} DATAFILECOMBO; //structure to maintain a file with embedded graphics data (sizes and offsets)

//these lines cause chaos :(
//#include "nclr.h"
//#include "ncgr.h"
//#include "nscr.h"

//
// Returns 1 if the specified format contains palette data.
//
int combo2dFormatHasPalette(int format);

//
// Returns 1 if the specified format contains character graphics data.
//
int combo2dFormatHasCharacter(int format);

//
// Returns 1 if the specified format contains screen data.
//
int combo2dFormatHasScreen(int format);

//
// Prepares a COMBO2D structure for deletion, can safely call free() after this.
//
void combo2dFree(COMBO2D *combo);

//
// Returns the type of COMBO2D the buffer contains, or COMBO2D_TYPE_INVALID if the data is invalid.
//
int combo2dIsValid(BYTE *buffer, int size);

//
// Read a COMBO2D extra data from a buffer, if there is any.
//
int combo2dRead(COMBO2D *combo, char *buffer, int size);

//
// Writes a COMBO2D to a BSTREAM.
//
int combo2dWrite(COMBO2D *combo, BSTREAM *stream);

//
// Write a COMBO2D to a file.
//
int combo2dWriteFile(COMBO2D *combo, LPWSTR path);
