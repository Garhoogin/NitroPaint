#include <stdio.h>
#include <stdint.h>

#include "jlyt.h"

static int BnllIsValidBnll(const unsigned char *buffer, unsigned int size);
static int BnclIsValidBncl(const unsigned char *buffer, unsigned int size);
static int BnblIsValidBnbl(const unsigned char *buffer, unsigned int size);

static void BnllFree(ObjHeader *hdr);
static void BnclFree(ObjHeader *hdr);
static void BnblFree(ObjHeader *hdr);

void JLytRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_BNLL, sizeof(BNLL), L"Letter Layout", (ObjReader) BnllRead, (ObjWriter) BnllWrite, NULL, BnllFree);
	ObjRegisterType(FILE_TYPE_BNCL, sizeof(BNCL), L"Cell Layout", (ObjReader) BnclRead, (ObjWriter) BnclWrite, NULL, BnclFree);
	ObjRegisterType(FILE_TYPE_BNBL, sizeof(BNBL), L"Button Layout", (ObjReader) BnblRead, (ObjWriter) BnblWrite, NULL, BnblFree);

	ObjRegisterFormat(FILE_TYPE_BNLL, BNLL_TYPE_BNLL, L"BNLL", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED, BnllIsValidBnll);
	ObjRegisterFormat(FILE_TYPE_BNCL, BNCL_TYPE_BNCL, L"BNCL", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED, BnclIsValidBncl);
	ObjRegisterFormat(FILE_TYPE_BNBL, BNBL_TYPE_BNBL, L"BNBL", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_VALIDATED, BnblIsValidBnbl);
}


// ----- common

//
// Check a jnlib fnt library file for header validity.
//
static int JLytIsValid(const unsigned char *buffer, unsigned int size, unsigned int tblElemSize) {
	//must have a file header.
	if (size < 8) return 0;

	//must have enough space for the table
	unsigned int nElem = *(const uint16_t *) (buffer + 0x6);
	if ((size - 8) / tblElemSize < nElem) return 0;

	return 1;
}

static int JLytIsValidRawCoordinate(uint16_t raw) {
	int orig  = (raw >> 12) & 3;
	int align = (raw >> 14) & 3;

	//alignment and origin must not be 3
	if (orig == 3) return 0;
	if (align == 3) return 0;
	return 1;
}

static void JLytDecodePosition(uint16_t rawX, uint16_t rawY, JLytPosition *pOutPosition, JLytAlignment *pOutAlignment) {
	JLytOrigin origX = (JLytOrigin) ((rawX >> 12) & 3);
	JLytOrigin origY = (JLytOrigin) ((rawY >> 12) & 3);

	int realX = rawX & 0x0FFF;
	int realY = rawY & 0x0FFF;
	if (realX & 0x0800) realX -= 0x1000; // to signed 12bit
	if (realY & 0x8000) realY -= 0x1000; // to signed 12bit
	
	if (pOutPosition != NULL) {
		pOutPosition->x.origin = origX;
		pOutPosition->x.pos = realX;
		pOutPosition->y.origin = origY;
		pOutPosition->y.pos = realY;
	}

	if (pOutAlignment != NULL) {
		pOutAlignment->x = (JLytOrigin) ((rawX >> 14) & 3);
		pOutAlignment->y = (JLytOrigin) ((rawY >> 14) & 3);
	}
}

static void JLytEncodePosition(int x, int y, JLytOrigin origX, JLytOrigin origY, JLytOrigin alignX, JLytOrigin alignY, uint16_t *pEnc) {
	pEnc[0] = (x & 0x0FFF) | ((((uint16_t) origX) & 3) << 12) | ((((uint16_t) alignX) & 3) << 14);
	pEnc[1] = (y & 0x0FFF) | ((((uint16_t) origY) & 3) << 12) | ((((uint16_t) alignY) & 3) << 14);
}

static void JLytWriteFntHeader(BSTREAM *stream, const char *signature, int nEntry) {
	unsigned char hdr[8];
	memcpy(hdr, signature, 4);

	*(uint16_t *) (hdr + 4) = 0;
	*(uint16_t *) (hdr + 6) = nEntry;
	bstreamWrite(stream, hdr, sizeof(hdr));
}



// ----- BNLL

static int BnllIsValidBnll(const unsigned char *buffer, unsigned int size) {
	if (!JLytIsValid(buffer, size, 0xC)) return 0;

	//file signature
	if (memcmp(buffer, "JNLL", 4) != 0) return 0;

	//check offsets
	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0xC;
		uint32_t ofsText = *(const uint32_t *) (ent + 0x8);

		//check coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);
		if (!JLytIsValidRawCoordinate(rawX)) return 0;
		if (!JLytIsValidRawCoordinate(rawY)) return 0;

		if (ofsText == 0) continue;
		if (ofsText >= size) return 0;
	}

	return 1;
}

int BnllIdentify(const unsigned char *buffer, unsigned int size) {
	if (BnllIsValidBnll(buffer, size)) return BNLL_TYPE_BNLL;
	return BNLL_TYPE_INVALID;
}

static void BnllFree(ObjHeader *hdr) {
	BNLL *bnll = (BNLL *) hdr;
	free(bnll->messages);
	bnll->messages = NULL;
	bnll->nMsg = 0;
}

static int BnllReadBnll(BNLL *bnll, const unsigned char *buffer, unsigned int size) {
	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	bnll->nMsg = nEntry;
	bnll->messages = (BnllMessage *) calloc(nEntry, sizeof(BnllMessage));
	
	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0xC;

		//decode coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);

		//decode basic attributes
		JLytDecodePosition(rawX, rawY, &bnll->messages[i].pos, &bnll->messages[i].alignment);
		bnll->messages[i].spaceX = *(const int8_t *) (ent + 0x4);
		bnll->messages[i].spaceY = *(const int8_t *) (ent + 0x5);
		bnll->messages[i].color = *(const uint8_t *) (ent + 0x6);
		bnll->messages[i].palette = (*(const uint8_t *) (ent + 0x7) >> 0) & 0xF;
		bnll->messages[i].font = (*(const uint8_t *) (ent + 0x7) >> 4) & 0xF;

		//message offset may be 0. This indicates a null pointer, otherwise the
		//offset is relative to the start of the file. Strings are null-terminated in
		//UCS-2.
		uint32_t ofsText = *(const uint32_t *) (ent + 0x8);
		if (ofsText == 0) {
			//no string
			bnll->messages[i].msg = NULL; // TODO
		} else {
			//yes string
			const uint16_t *pStr = (const uint16_t *) (buffer + ofsText);
			unsigned int len = 0;
			while (pStr[len]) len++;

			//copy null terminated
			bnll->messages[i].msg = (wchar_t *) calloc(len + 1, sizeof(wchar_t));
			memcpy(bnll->messages[i].msg, pStr, (len + 1) * sizeof(wchar_t));
		}
	}

	return OBJ_STATUS_SUCCESS;
}

int BnllRead(BNLL *bnll, const unsigned char *buffer, unsigned int size) {
	switch (BnllIdentify(buffer, size)) {
		case BNLL_TYPE_BNLL:
			return BnllReadBnll(bnll, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

static int BnllWriteBnll(BNLL *bnll, BSTREAM *stream) {
	//header
	JLytWriteFntHeader(stream, "JNLL", bnll->nMsg);

	//construct string pool. Sort strings by their lengths, and write them in order of longest
	//to shortest. When inserting a string, scan for it first.
	BSTREAM stmStrings;
	bstreamCreate(&stmStrings, NULL, 0);

	unsigned char entbuf[0xC];
	for (int i = 0; i < bnll->nMsg; i++) {

		//scan for string
		uint32_t offs = 0;
		if (bnll->messages[i].msg != NULL) {
			unsigned int msglen = wcslen(bnll->messages[i].msg) + 1;

			int found = 0;
			for (unsigned int j = 0; (j + 2 * msglen) <= stmStrings.size; j += 2) {
				if (memcmp(bnll->messages[i].msg, stmStrings.buffer + j, msglen * 2) == 0) {
					//mark found
					found = 1;
					offs = j;
					break;
				}
			}

			if (!found) {
				//append
				offs = stmStrings.size;
				bstreamWrite(&stmStrings, bnll->messages[i].msg, msglen * 2);
			}

			//add array size and header size
			offs += 8 + 0xC * bnll->nMsg;
		}

		//encode
		BnllMessage *msg = &bnll->messages[i];
		JLytEncodePosition(msg->pos.x.pos, msg->pos.y.pos, msg->pos.x.origin, msg->pos.y.origin, msg->alignment.x, msg->alignment.y, (uint16_t *) entbuf);
		*(int8_t *) (entbuf + 0x4) = msg->spaceX;
		*(int8_t *) (entbuf + 0x5) = msg->spaceY;
		*(uint8_t *) (entbuf + 0x6) = msg->color;
		*(uint8_t *) (entbuf + 0x7) = (msg->palette & 0xF) | ((msg->font & 0xF) << 4);
		*(uint32_t *) (entbuf + 0x8) = offs;
		bstreamWrite(stream, entbuf, sizeof(entbuf));
	}

	//write string pool
	bstreamWrite(stream, stmStrings.buffer, stmStrings.size);
	bstreamFree(&stmStrings);

	return OBJ_STATUS_SUCCESS;
}

int BnllWrite(BNLL *bnll, BSTREAM *stream) {
	switch (bnll->header.format) {
		case BNLL_TYPE_BNLL:
			return BnllWriteBnll(bnll, stream);
	}
	return OBJ_STATUS_INVALID;
}



// ----- BNCL

static int BnclIsValidBncl(const unsigned char *buffer, unsigned int size) {
	if (!JLytIsValid(buffer, size, 0x8)) return 0;

	//file signature
	if (memcmp(buffer, "JNCL", 4) != 0) return 0;

	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0x8;

		//check coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);
		if (!JLytIsValidRawCoordinate(rawX)) return 0;
		if (!JLytIsValidRawCoordinate(rawY)) return 0;
	}

	return 1;
}

int BnclIdentify(const unsigned char *buffer, unsigned int size) {
	if (BnclIsValidBncl(buffer, size)) return BNCL_TYPE_BNCL;
	return BNCL_TYPE_INVALID;
}

static void BnclFree(ObjHeader *hdr) {
	BNCL *bncl = (BNCL *) hdr;
	free(bncl->cells);
	bncl->cells = NULL;
	bncl->nCell = 0;
}

static int BnclReadBncl(BNCL *bncl, const unsigned char *buffer, unsigned int size) {
	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	bncl->nCell = nEntry;
	bncl->cells = (BnclCell *) calloc(nEntry, sizeof(BnclCell));

	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0x8;

		//decode coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);

		JLytDecodePosition(rawX, rawY, &bncl->cells[i].pos, NULL);
		bncl->cells[i].cell = *(const uint32_t *) (ent + 0x4);
	}

	return OBJ_STATUS_SUCCESS;
}

int BnclRead(BNCL *bncl, const unsigned char *buffer, unsigned int size) {
	switch (BnclIdentify(buffer, size)) {
		case BNCL_TYPE_BNCL:
			return BnclReadBncl(bncl, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

static int BnclWriteBncl(BNCL *bncl, BSTREAM *stream) {
	JLytWriteFntHeader(stream, "JNCL", bncl->nCell);

	unsigned char entbuf[8];
	for (int i = 0; i < bncl->nCell; i++) {
		BnclCell *cell = &bncl->cells[i];

		JLytEncodePosition(cell->pos.x.pos, cell->pos.y.pos, cell->pos.x.origin, cell->pos.y.origin, 0, 0, (uint16_t *) entbuf);
		*(uint32_t *) (entbuf + 4) = cell->cell;
		bstreamWrite(stream, entbuf, sizeof(entbuf));
	}

	return OBJ_STATUS_SUCCESS;
}

int BnclWrite(BNCL *bncl, BSTREAM *stream) {
	switch (bncl->header.format) {
		case BNCL_TYPE_BNCL:
			return BnclWriteBncl(bncl, stream);
	}
	return OBJ_STATUS_INVALID;
}



// ----- BNBL

static int BnblIsValidBnbl(const unsigned char *buffer, unsigned int size) {
	if (!JLytIsValid(buffer, size, 0x6)) return 0;

	//file signature
	if (memcmp(buffer, "JNBL", 4) != 0) return 0;

	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0x6;

		//check coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);
		if (!JLytIsValidRawCoordinate(rawX)) return 0;
		if (!JLytIsValidRawCoordinate(rawY)) return 0;
	}

	return 1;
}

int BnblIdentify(const unsigned char *buffer, unsigned int size) {
	if (BnblIsValidBnbl(buffer, size)) return BNBL_TYPE_BNBL;
	return BNBL_TYPE_INVALID;
}

static void BnblFree(ObjHeader *hdr) {
	BNBL *bnbl = (BNBL *) hdr;
	free(bnbl->regions);
	bnbl->regions = NULL;
	bnbl->nRegion = 0;
}

static int BnblReadBnbl(BNBL *bnbl, const unsigned char *buffer, unsigned int size) {
	uint16_t nEntry = *(const uint16_t *) (buffer + 0x6);
	bnbl->nRegion = nEntry;
	bnbl->regions = (BnblRegion *) calloc(nEntry, sizeof(BnblRegion));

	for (unsigned int i = 0; i < nEntry; i++) {
		const unsigned char *ent = buffer + 0x8 + i * 0x6;

		//decode coordinates
		uint16_t rawX = *(const uint16_t *) (ent + 0x0);
		uint16_t rawY = *(const uint16_t *) (ent + 0x2);
		
		JLytDecodePosition(rawX, rawY, &bnbl->regions[i].pos, NULL);
		bnbl->regions[i].width = *(const uint8_t *) (ent + 0x4);
		bnbl->regions[i].height = *(const uint8_t *) (ent + 0x5);
	}
	return OBJ_STATUS_SUCCESS;
}

int BnblRead(BNBL *bnbl, const unsigned char *buffer, unsigned int size) {
	switch (BnblIdentify(buffer, size)) {
		case BNBL_TYPE_BNBL:
			return BnblReadBnbl(bnbl, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

static int BnblWriteBnbl(BNBL *bnbl, BSTREAM *stream) {
	//file header
	JLytWriteFntHeader(stream, "JNBL", bnbl->nRegion);

	unsigned char entbuf[6];
	for (int i = 0; i < bnbl->nRegion; i++) {
		BnblRegion *rgn = &bnbl->regions[i];
		JLytEncodePosition(rgn->pos.x.pos, rgn->pos.y.pos, rgn->pos.x.origin, rgn->pos.y.origin, 0, 0, (uint16_t *) entbuf);
		entbuf[4] = rgn->width;
		entbuf[5] = rgn->height;
		bstreamWrite(stream, entbuf, sizeof(entbuf));
	}

	return OBJ_STATUS_SUCCESS;
}

int BnblWrite(BNBL *bnbl, BSTREAM *stream) {
	switch (bnbl->header.format) {
		case BNBL_TYPE_BNBL:
			return BnblWriteBnbl(bnbl, stream);
	}
	return OBJ_STATUS_INVALID;
}
