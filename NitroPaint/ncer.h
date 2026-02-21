#pragma once
#include "ncgr.h"
#include "nclr.h"

#define NCER_TYPE_INVALID    0
#define NCER_TYPE_NCER       1
#define NCER_TYPE_SETOSA     2
#define NCER_TYPE_HUDSON     3
#define NCER_TYPE_GHOSTTRICK 4
#define NCER_TYPE_COMBO      5

typedef struct NCER_CELL_ {
	int nAttribs;
	int cellAttr;
	uint32_t attrEx; // UCAT extended attribute data

	int maxX;
	int maxY;
	int minX;
	int minY;
	
	uint16_t *attr;
	uint32_t *ex2dCharNames;
	int useEx2d;

	int forbidCompression; // forbids compression of graphics
} NCER_CELL;

typedef struct NCER_CELL_INFO_ {
	//Attribute 0
	int y;				//
	int rotateScale;	//
	int doubleSize;		//
	int disable;		//
	int mode;			//
	int mosaic;			//
	int characterBits;	//
	int shape;			//

	//Attribute 1
	int x;				//
	int matrix;
	int flipX;			//
	int flipY;			//
	int size;			//

	//Attribute 2
	int characterName;	//
	int priority;		//
	int palette;		//

	//Convenience
	int width;
	int height;
} NCER_CELL_INFO;

typedef struct NCER_ {
	ObjHeader header;              // object header
	int nCells;                        // number of cells in cell bank
	int bankAttribs;                   // cell bank attribute
	int mappingMode;                   // cell mapping mode
	NCER_CELL *cells;                  // list of cells

	CHAR_VRAM_TRANSFER *vramTransfer;  // list of VRAM transfer entries
	int nVramTransferEntries;          // number of VRAM transfer animation entries
	int useExtAttr;                    // use NCER extended attributes
	int isEx2d;                        // use of pseudo extended 2D mapping
	int ex2dBaseMappingMode;           // base mapping mode when extended 2D is used

	int uextSize;                      // size of UEXT
	char *uext;                        // UEXT
	int lablSize;                      // size of LABL
	char *labl;                        // LABL
} NCER;

void CellRegisterFormats(void);


int CellIdentify(const unsigned char *buffer, unsigned int size);

int CellRead(NCER *ncer, const unsigned char *buffer, unsigned int size);

void CellInitBankCell(NCER *ncer, NCER_CELL *cell, int nObj);

void CellInsertOBJ(NCER_CELL *cell, int index, int nObj);

void CellDeleteOBJ(NCER_CELL *cell, int index, int nObj);

int CellSetBankExt2D(NCER *ncer, NCGR *ncgr, int enable);

void CellGetObjDimensions(int shape, int size, int *width, int *height);

int CellDecodeOamAttributes(NCER_CELL_INFO *info, NCER_CELL *cell, int oam);

int CellFree(ObjHeader *header);

void CellRenderObj(NCER_CELL_INFO *info, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, COLOR32 *out);

COLOR32 *CellRenderCell(COLOR32 *px, NCER_CELL *cell, int mapping, NCGR *ncgr, NCLR *nclr, CHAR_VRAM_TRANSFER *vramTransfer, int xOffs, int yOffs, float a, float b, float c, float d);

void CellDeleteCell(NCER *ncer, int idx);

void CellMoveCellIndex(NCER *ncer, int iSrc, int iDst);

int CellWrite(NCER *ncer, BSTREAM *stream);



// ----- render cell

void CellRender(
	COLOR32   *px,             // output 512x256 pixel buffer
	int       *covbuf,         // output coverage buffer (optional)
	NCER      *ncer,           // cell data bank
	NCGR      *ncgr,           // character graphics
	NCLR      *nclr,           // color palette
	int        cellIndex,      // cell index (required if cell is in the cell data bank)
	NCER_CELL *cell,           // cell data (required if not in the cell data bank)
	int        xOffs,          // horizontal displacement of render
	int        yOffs,          // vertical displacement of render
	double     a,              // affine parameter A
	double     b,              // affine parameter B
	double     c,              // affine parameter C
	double     d,              // affine parameter D
	int        forceAffine,    // forces all OBJ to be in affine mode
	int        forceDoubleSize // forces all affine OBJ to be in double size mode
);
