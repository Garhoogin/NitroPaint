#pragma once

#include "filecommon.h"
#include <stdint.h>


// ----- Font file format code

#define NFTR_TYPE_INVALID       0
#define NFTR_TYPE_NFTR_01       1  // NFTR 0.1
#define NFTR_TYPE_NFTR_10       2  // NFTR 1.0
#define NFTR_TYPE_NFTR_11       3  // NFTR 1.1
#define NFTR_TYPE_NFTR_12       4  // NFTR 1.2
#define NFTR_TYPE_BNFR_11       5  // BNFR 1.1
#define NFTR_TYPE_BNFR_12       6  // BNFR 1.2
#define NFTR_TYPE_BNFR_20       7  // BNFR 2.0
#define NFTR_TYPE_GF_NFTR_11    8  // GameFreak NFTR 1.1

extern LPCWSTR fontFormatNames[];


typedef enum FontCharacterSet_ {
	FONT_CHARSET_UTF8,         // UTF-8 character set (store 16-bit Unicode code points)
	FONT_CHARSET_UTF16,        // UTF-16 character set (store 16-bit Unicode code points)
	FONT_CHARSET_SJIS,         // Shift-JIS character set (store 16-bit SJIS code points)
	FONT_CHARSET_ASCII         // ASCII extended character set (store 8-bit code points)
} FontCharacterSet;

typedef enum FontRotation_ {
	FONT_ROTATION_0,           // font runs 0 degrees clockwise
	FONT_ROTATION_90,          // font runs 90 degrees clockwise
	FONT_ROTATION_180,         // font runs 180 degrees clockwise
	FONT_ROTATION_270          // font runs 270 degrees clockwise
} FontRotation;

typedef struct NFTR_GLYPH_ {
	unsigned char *px;
	int width;                 // width of used glyph space
	int spaceLeft;             // space subtracted from the left of the glyph
	int spaceRight;
	uint16_t cp;               // code point associated with the glyph, if code map is present
	uint16_t isInvalid;        // is this the invalid glyph
} NFTR_GLYPH;

typedef struct NFTR_ {
	OBJECT_HEADER header;
	int cellWidth;             // width of each glyph cell
	int cellHeight;            // height of each glyph cell
	int lineHeight;            // height of a line
	int bpp;
	uint16_t invalid;          // index of invalid glyph
	FontCharacterSet charset;  // character set associated with the font
	FontRotation rotation;     // text rendering rotation
	int vertical;              // text rendering in vertical orientation

	int pxLeading;             // pixels of space added to the left of each glyph
	int pxAscent;              // how many pixels up from the baseline in glyph height
	int pxDescent;             // how many pixels down from the baseline in glyph height

	int nGlyph;
	int hasCodeMap;
	NFTR_GLYPH *glyphs;
} NFTR;

int NftrIsValidBnfr11(const unsigned char *buffer, unsigned int size);
int NftrIsValidBnfr12(const unsigned char *buffer, unsigned int size);
int NftrIsValidBnfr20(const unsigned char *buffer, unsigned int size);
int NftrIdentify(const unsigned char *buffer, unsigned int size);

void NftrInit(NFTR *nftr, int format);
int NftrRead(NFTR *nftr, const unsigned char *buffer, unsigned int size);
int NftrReadFile(NFTR *nftr, LPCWSTR path);

int NftrGetGlyphIndexByCP(NFTR *nftr, uint16_t cp);
int NftrGetInvalidGlyphIndex(NFTR *nftr);
NFTR_GLYPH *NftrGetGlyphByCP(NFTR *nftr, uint16_t cp);
NFTR_GLYPH *NftrGetInvalidGlyph(NFTR *nftr);
void NftrEnsureSorted(NFTR *nftr);
void NftrSetBitDepth(NFTR *nftr, int depth);
void NftrSetCellSize(NFTR *nftr, int width, int height);

int NftrWrite(NFTR *nftr, BSTREAM *stream);
int NftrWriteFile(NFTR *nftr, LPWSTR name);



// ----- Code map file 

extern LPCWSTR codeMapFormatNames[];

#define BNCMP_TYPE_INVALID   0
#define BNCMP_TYPE_BNCMP_11  1 // BNCMP 1.1
#define BNCMP_TYPE_BNCMP_12  2 // BNCMP 1.2


int BncmpIdentify(const unsigned char *buffer, unsigned int size);
int BncmpRead(NFTR *nftr, const unsigned char *buffer, unsigned int size);

int BncmpWrite(NFTR *nftr, BSTREAM *stream);

