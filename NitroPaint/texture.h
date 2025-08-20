#pragma once
#include "color.h"
#include "filecommon.h"

//texture file types
#define TEXTURE_TYPE_INVALID     0
#define TEXTURE_TYPE_NNSTGA      1 // NNS TGA format
#define TEXTURE_TYPE_ISTUDIO     2 // iMageStudio 5TX format
#define TEXTURE_TYPE_SPT         3 // SPL texture format
#define TEXTURE_TYPE_TDS         4 // Ghost Trick TDS format
#define TEXTURE_TYPE_NTGA        5 // Not to be confused with NNS TGA!
#define TEXTURE_TYPE_TOLOVERU    6 // To Love Ru format

#define CT_A3I5 1			/*can read and write*/
#define CT_4COLOR 2			/*can read and write*/
#define CT_16COLOR 3		/*can read and write*/
#define CT_256COLOR 4		/*can read and write*/
#define CT_4x4 5			/*can read and write*/
#define CT_A5I3 6			/*can read and write*/
#define CT_DIRECT 7			/*can read and write*/


#define FORMAT(p)		(((p)>>26)&7)
#define COL0TRANS(p)	(((p)>>29)&1)
#define OFFSET(p)		(((p)&0xFFFF)<<3)
#define TEXW(p)			(8<<(((p)>>20)&7))
#define TEXH(p)			(8<<(((p)>>23)&7))

//4x4 compression macros
#define COMP_INTERPOLATE   0x4000
#define COMP_FULL          0x0000
#define COMP_OPAQUE        0x8000
#define COMP_TRANSPARENT   0x0000
#define COMP_MODE_MASK     0xC000
#define COMP_INDEX_MASK    0x3FFF
#define COMP_INDEX(c)      (((c)&COMP_INDEX_MASK)<<1)

typedef struct {
	int texImageParam;
	int height;
	unsigned char *texel;
	uint16_t *cmp;
	char *name;
} TEXELS;

typedef struct {
	int nColors;
	COLOR *pal;
	char *name;
} PALETTE;

typedef struct {
	TEXELS texels;
	PALETTE palette;
} TEXTURE;

typedef struct TextureObject_ {
	OBJECT_HEADER header;
	TEXTURE texture;
} TextureObject;

const char *TxNameFromTexFormat(int fmt);

void TxRender(COLOR32 *px, int dstWidth, int dstHeight, TEXELS *texels, PALETTE *palette, int flip);

int TxGetTexelSize(int width, int height, int texImageParam);

int TxGetTextureVramSize(TEXELS *texels);

int TxGetIndexVramSize(TEXELS *texels);

int TxGetTexPlttVramSize(PALETTE *palette);

int TxDimensionIsValid(int x);


// ----- Functions operating on the texture as an object

extern LPCWSTR textureFormatNames[];

void TxInit(TextureObject *texture, int format);

void TxFree(OBJECT_HEADER *texture);

void TxContain(TextureObject *object, int format, TEXTURE *texture);

TEXTURE *TxUncontain(TextureObject *texture);

int TxIsValidNnsTga(const unsigned char *buffer, unsigned int size);

int TxIsValidIStudio(const unsigned char *buffer, unsigned int size);

int TxIsValidTds(const unsigned char *buffer, unsigned int size);

int TxIdentify(const unsigned char *buffer, unsigned int size);

int TxIdentifyFile(LPCWSTR path);

int TxReadNnsTga(TextureObject *texture, const unsigned char *buffer, unsigned int size);

int TxReadIStudio(TextureObject *texture, const unsigned char *buffer, unsigned int size);

int TxReadTds(TextureObject *texture, const unsigned char *buffer, unsigned int size);

int TxRead(TextureObject *texture, const unsigned char *buffer, unsigned int size);

int TxReadFile(TextureObject *texture, LPCWSTR path);

int TxReadFileDirect(TEXELS *texels, PALETTE *palette, LPCWSTR path);

int TxWriteNnsTga(TextureObject *texture, BSTREAM *stream);

int TxWriteTds(TextureObject *texture, BSTREAM *stream);

int TxWriteIStudio(TextureObject *texture, BSTREAM *stream);

int TxWrite(TextureObject *texture, BSTREAM *stream);

int TxWriteFile(TextureObject *texture, LPCWSTR path);

int TxWriteFileDirect(TEXELS *texels, PALETTE *palette, int format, LPCWSTR path);
