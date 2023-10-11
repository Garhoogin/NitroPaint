#include "filecommon.h"
#include "nns.h"
#include "nclr.h"
#include "ncgr.h"
#include "ncer.h"
#include "nanr.h"
#include "nmcr.h"

int nmcrRead(NMCR *nmcr, char *buffer, unsigned int size) {
	int type = nmcrIsValid(buffer, size);
	if (type == NMCR_TYPE_INVALID) return 1;

	nmcr->header.size = sizeof(NMCR);
	fileInitCommon((OBJECT_HEADER *) nmcr, FILE_TYPE_NMCR, type);
	if (type == NMCR_TYPE_NMCR) {
		char *pMcbk = g2dGetSectionByMagic(buffer, size, 'MCBK');
		if (pMcbk == NULL) {
			pMcbk = g2dGetSectionByMagic(buffer, size, 'KBCM');
		}
		pMcbk += 8;

		char *pMultiCells = pMcbk + *(uint32_t *) (pMcbk + 4);
		char *pHierarchy = pMcbk + *(uint32_t *) (pMcbk + 8);
		nmcr->nMultiCell = *(uint16_t *) pMcbk;
		nmcr->multiCells = (MULTI_CELL *) calloc(nmcr->nMultiCell, sizeof(MULTI_CELL));
		memcpy(nmcr->multiCells, pMultiCells, nmcr->nMultiCell * sizeof(MULTI_CELL));

		//adjust pointers to heap allocated arrays
		for (int i = 0; i < nmcr->nMultiCell; i++) {
			MULTI_CELL *cell = nmcr->multiCells + i;
			CELL_HIERARCHY *hierarchy = (CELL_HIERARCHY *) calloc(cell->nNodes, sizeof(CELL_HIERARCHY));
			memcpy(hierarchy, pHierarchy + (uint32_t) (cell->hierarchy), cell->nNodes * sizeof(CELL_HIERARCHY));
			cell->hierarchy = hierarchy;
		}
	}

	return 0;
}

int nmcrIsValid(char *buffer, unsigned int size) {
	if (!g2dIsValid(buffer, size)) return NMCR_TYPE_INVALID;

	char *pMcbk = g2dGetSectionByMagic(buffer, size, 'MCBK');
	if (pMcbk == NULL) {
		pMcbk = g2dGetSectionByMagic(buffer, size, 'KBCM');
	}
	if (pMcbk == NULL) return NMCR_TYPE_INVALID;

	return NMCR_TYPE_NMCR;
}

int nmcrReadFile(NMCR *nmcr, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nmcr, (OBJECT_READER) nmcrRead);
}
