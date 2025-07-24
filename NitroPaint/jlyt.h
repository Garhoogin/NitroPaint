#pragma once
#include <Windows.h>
#include "filecommon.h"

extern LPCWSTR bnllFormatNames[];
extern LPCWSTR bnclFormatNames[];
extern LPCWSTR bnblFormatNames[];


#define BNLL_TYPE_INVALID    0
#define BNLL_TYPE_BNLL       1

#define BNCL_TYPE_INVALID    0
#define BNCL_TYPE_BNCL       1

#define BNBL_TYPE_INVALID    0
#define BNBL_TYPE_BNBL       1


typedef enum JLytOrigin_ {
	JLYT_ORIG_X_LEFT     = 0,
	JLYT_ORIG_X_CENTER   = 1,
	JLYT_ORIG_X_RIGHT    = 2,

	JLYT_ORIG_Y_TOP      = 0,
	JLYT_ORIG_Y_CENTER   = 1,
	JLYT_ORIG_Y_BOTTOM   = 2
} JLytOrigin;

typedef struct JLytCoordinate_ {
	JLytOrigin origin;
	int pos;
} JLytCoordinate;

typedef struct JLytPosition_ {
	JLytCoordinate x;
	JLytCoordinate y;
} JLytPosition;

typedef struct JLytAlignment_ {
	JLytOrigin x;
	JLytOrigin y;
} JLytAlignment;


// ----- BNLL

typedef struct BnllMessage_ {
	JLytPosition pos;        // message position
	JLytAlignment alignment; // text align X
	int spaceX;              // font spacing X
	int spaceY;              // font spacing Y
	int color;               // message color
	int palette;             // message color palette
	int font;                // message font
	wchar_t *msg;            // message text
} BnllMessage;

typedef struct BNLL_ {
	OBJECT_HEADER header;

	int nMsg;
	BnllMessage *messages;
} BNLL;

int BnllIdentify(const unsigned char *buffer, unsigned int size);
int BnllRead(BNLL *bnll, const unsigned char *buffer, unsigned int size);
int BnllReadFile(BNLL *bnll, LPCWSTR path);
int BnllWrite(BNLL *bnll, BSTREAM *stream);



// ----- BNCL

typedef struct BnclCell_ {
	JLytPosition pos;    // position of cell
	int cell;            // index of cell
} BnclCell;

typedef struct BNCL_ {
	OBJECT_HEADER header;

	int nCell;
	BnclCell *cells;
} BNCL;

int BnclIdentify(const unsigned char *buffer, unsigned int size);
int BnclRead(BNCL *bncl, const unsigned char *buffer, unsigned int size);
int BnclReadFile(BNCL *bncl, LPCWSTR path);
int BnclWrite(BNCL *bncl, BSTREAM *stream);



// ----- BNBL

typedef struct BnblRegion_ {
	JLytPosition pos;
	int width;
	int height;
} BnblRegion;

typedef struct BNBL_ {
	OBJECT_HEADER header;

	int nRegion;
	BnblRegion *regions;
} BNBL;

int BnblIdentify(const unsigned char *buffer, unsigned int size);
int BnblRead(BNBL *bnbl, const unsigned char *buffer, unsigned int size);
int BnblReadFile(BNBL *bnbl, LPCWSTR path);
int BnblWrite(BNBL *bnbl, BSTREAM *stream);
