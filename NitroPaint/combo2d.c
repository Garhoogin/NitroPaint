#include <Windows.h>
#include "color.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "combo2d.h"

typedef struct BANNER_INFO_ {
	int version;
	WCHAR titleJp[128];
	WCHAR titleEn[128];
	WCHAR titleFr[128];
	WCHAR titleGe[128];
	WCHAR titleIt[128];
	WCHAR titleSp[128];
	WCHAR titleCn[128];
	WCHAR titleHn[128];
} BANNER_INFO;

int combo2dFormatHasPalette(int format) {
	return 1;
}

int combo2dFormatHasCharacter(int format) {
	return 1;
}

int combo2dFormatHasScreen(int format) {
	return format == COMBO2D_TYPE_TIMEACE;
}

int combo2dCanSave(COMBO2D *combo) {
	if (combo2dFormatHasPalette(combo->header.format) && combo->nclr == NULL) return 0;
	if (combo2dFormatHasCharacter(combo->header.format) && combo->ncgr == NULL) return 0;
	if (combo2dFormatHasScreen(combo->header.format) && combo->nscr == NULL) return 0;
	return 1;
}

void combo2dFree(COMBO2D *combo) {
	if (combo->nclr != NULL) {
		free(combo->nclr);
		combo->nclr = NULL;
	}
	if (combo->ncgr != NULL) {
		free(combo->ncgr);
		combo->ncgr = NULL;
	}
	if (combo->nscr != NULL) {
		free(combo->nscr);
		combo->nscr = NULL;
	}
	if (combo->extraData != NULL) {
		free(combo->extraData);
		combo->extraData = NULL;
	}
}

int combo2dIsValidTimeAce(BYTE *file, int size) {
	//file must be big enough for 12 bytes plus palette (512 bytes) and screen (2048 bytes).
	if (size < 0xA0C) return 0;

	//validate fields
	int bitness = *(int *) file;
	if (bitness != 0 && bitness != 1) return 0;
	int nChars = *(int *) (file + 0xA08);
	int charSize = bitness ? 0x40 : 0x20;
	if (0xA0C + nChars * charSize != size) return 0;

	for (int i = 0; i < 256; i++) {
		COLOR c = ((COLOR *) (file + 4))[i];
		if (c & 0x8000) return 0;
	}
	return 1;
}

int combo2dIsValidBanner(BYTE *file, int size) {
	if (size < 0x840) return 0;

	int version = *(unsigned short *) file;
	int crcA = *(unsigned short *) (file + 2);
	int crcB = *(unsigned short *) (file + 4);
	int crcC = *(unsigned short *) (file + 6);
	int crcD = *(unsigned short *) (file + 8);
	if (version != 1 && version != 2 && version != 3 && version != 0x0103) return 0;
	if (crcA != computeCrc16(file + 0x20, 0x820, 0xFFFF)) return 0;

	//at 0xA, 0x16 bytes should be 0.
	for (int i = 0; i < 0x16; i++) if (file[i + 0xA] != 0) return 0;

	COLOR *palette = (COLOR *) (file + 0x220);
	for (int i = 0; i < 16; i++) if (palette[i] & 0x8000) return 0;
	
	if (version == 1 && (size != 0x840 && size != 0xA00)) return 0;
	if (version == 2 && (size != 0x940 && size != 0xA00)) return 0;
	if (version == 3 && (size != 0xA40 && size != 0xC00)) return 0;

	return 1;
}

int combo2dIsValid(BYTE *file, int size) {
	if (combo2dIsValidTimeAce(file, size)) return COMBO2D_TYPE_TIMEACE;
	if (combo2dIsValidBanner(file, size)) return COMBO2D_TYPE_BANNER;
	return 0;
}

int combo2dRead(COMBO2D *combo, char *buffer, int size) {
	int format = combo2dIsValid(buffer, size);
	if (format == COMBO2D_TYPE_INVALID) return 1;

	switch (format) {
		case COMBO2D_TYPE_TIMEACE:
			return 0;
		case COMBO2D_TYPE_BANNER:
		{
			BANNER_INFO *info = (BANNER_INFO *) calloc(1, sizeof(BANNER_INFO));
			combo->extraData = (void *) info;
			info->version = *(unsigned short *) buffer;
			memcpy(info->titleJp, buffer + 0x240, 0x600);
			if (info->version >= 2) memcpy(info->titleCn, buffer + 0x840, 0x100);
			if (info->version >= 3) memcpy(info->titleHn, buffer + 0x940, 0x100);
			return 0;
		}
	}
	return 1;
}

int combo2dWrite(COMBO2D *combo, BSTREAM *stream) {
	if(!combo2dCanSave(combo)) return 1;

	//write out 
	if (combo->header.format == COMBO2D_TYPE_TIMEACE) {
		BOOL is8bpp = combo->ncgr->nBits == 8;
		int dummy = 0;

		bstreamWrite(stream, &is8bpp, sizeof(is8bpp));
		bstreamWrite(stream, combo->nclr->colors, 2 * combo->nclr->nColors);
		bstreamWrite(stream, &dummy, sizeof(dummy));
		bstreamWrite(stream, combo->nscr->data, combo->nscr->dataSize);
		bstreamWrite(stream, &combo->ncgr->nTiles, 4);

		NCGR *ncgr = combo->ncgr;
		if (ncgr->nBits == 8) {
			for (int i = 0; i < ncgr->nTiles; i++) {
				bstreamWrite(stream, ncgr->tiles[i], 64);
			}
		} else {
			BYTE buffer[32];
			for (int i = 0; i < ncgr->nTiles; i++) {
				for (int j = 0; j < 32; j++) {
					buffer[j] = ncgr->tiles[i][j * 2] | (ncgr->tiles[i][j * 2 + 1] << 4);
				}
				bstreamWrite(stream, buffer, sizeof(buffer));
			}
		}
	} else if (combo->header.format == COMBO2D_TYPE_BANNER) {
		BANNER_INFO *info = (BANNER_INFO *) combo->extraData;
		unsigned short header[16] = { 0 };
		bstreamWrite(stream, header, sizeof(header));

		NCGR *ncgr = combo->ncgr;
		BYTE charBuffer[32];
		for (int i = 0; i < ncgr->nTiles; i++) {
			for (int j = 0; j < 32; j++) {
				charBuffer[j] = ncgr->tiles[i][j * 2] | (ncgr->tiles[i][j * 2 + 1] << 4);
			}
			bstreamWrite(stream, charBuffer, sizeof(charBuffer));
		}

		//write palette
		bstreamWrite(stream, combo->nclr->colors, 32);

		//write titles
		bstreamWrite(stream, info->titleJp, 0x600);
		if (info->version >= 2) bstreamWrite(stream, info->titleCn, 0x100);
		if (info->version >= 3) bstreamWrite(stream, info->titleHn, 0x100);

		//go back and write the CRCs
		bstreamSeek(stream, 0, 0);
		header[0] = info->version;
		header[1] = computeCrc16(stream->buffer + 0x20, 0x820, 0xFFFF);
		if (info->version >= 2) header[2] = computeCrc16(stream->buffer + 0x20, 0x920, 0xFFFF);
		if (info->version >= 3) header[3] = computeCrc16(stream->buffer + 0x20, 0xA20, 0xFFFF);
		bstreamWrite(stream, header, sizeof(header));
	}

	return 0;
}

int combo2dWriteFile(COMBO2D *combo, LPWSTR path) {
	return fileWrite(path, (OBJECT_HEADER *) combo, (OBJECT_WRITER) combo2dWrite);
}
