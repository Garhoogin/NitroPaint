#include <Windows.h>
#include "color.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "combo2d.h"

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

int combo2dIsValid(BYTE *file, int size) {
	if (combo2dIsValidTimeAce(file, size)) return COMBO2D_TYPE_TIMEACE;
	return 0;
}

int combo2dWrite(COMBO2D *combo, BSTREAM *stream) {
	if (combo->nclr == NULL || combo->ncgr == NULL || combo->nscr == NULL) return 1;

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
	}

	return 0;
}

int combo2dWriteFile(COMBO2D *combo, LPWSTR path) {
	return fileWrite(path, (OBJECT_HEADER *) combo, (OBJECT_WRITER) combo2dWrite);
}
