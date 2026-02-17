#pragma once

#include <stdint.h>

#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "ncer.h"
#include "nanr.h"

#define NMCR_TYPE_INVALID  0
#define NMCR_TYPE_NMCR     1

typedef struct CELL_HIERARCHY_ {
	uint16_t sequenceNumber;
	int16_t x;
	int16_t y;
	uint16_t nodeAttr;
} CELL_HIERARCHY;

typedef struct MULTI_CELL_ {
	uint16_t nNodes;
	uint16_t nCellAnim;
	CELL_HIERARCHY *hierarchy;
} MULTI_CELL;

typedef struct NMCR_ {
	ObjHeader header;
	int nMultiCell;
	MULTI_CELL *multiCells;
} NMCR;

void nmcrRegisterFormats(void);

//
// Determine the type of NMCR pointed to. Can be NMCR_TYPE_NMCR or
// NMCR_TYPE_INVALID.
//
int nmcrIsValid(const unsigned char *buffer, unsigned int size);

//
// Read the NMCR file pointed to into the destination structure.
//
int nmcrRead(NMCR *nmcr, char *buffer, unsigned int size);
