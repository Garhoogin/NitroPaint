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
	ObjInit((OBJECT_HEADER *) nmcr, FILE_TYPE_NMCR, type);
	if (type == NMCR_TYPE_NMCR) {
		char *pMcbk = NnsG2dFindBlockBySignature(buffer, size, "MCBK", NNS_SIG_LE, NULL);

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
	if (!NnsG2dIsValid(buffer, size)) return NMCR_TYPE_INVALID;
	if (memcmp(buffer, "RCMN", 4) != 0) return 0;

	const unsigned char *pMcbk = NnsG2dFindBlockBySignature(buffer, size, "MCBK", NNS_SIG_LE, NULL);
	if (pMcbk == NULL) return NMCR_TYPE_INVALID;

	return NMCR_TYPE_NMCR;
}

int nmcrReadFile(NMCR *nmcr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nmcr, (OBJECT_READER) nmcrRead);
}
