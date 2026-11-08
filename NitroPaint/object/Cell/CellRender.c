#include "object/NitroCell.h"
#include "CellRender.h"

#define SEXT8(n)   (((n)<0x080)?(n):((n)-0x100))
#define SEXT9(n)   (((n)<0x100)?(n):((n)-0x200))


static int FloatToInt(double x) {
	return (int) (x + (x < 0.0f ? -0.5f : 0.5f));
}

static void CellRenderOBJ_Character(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	int tilesX = info->width / 8;
	int tilesY = info->height / 8;

	if (ncgr == NULL) {
		//null NCGR, render opaque coverage by OBJ
		COLOR32 fill = 0xFF000000;
		if (nclr != NULL && nclr->nColors >= 1) {
			fill = 0xFF000000 | ColorConvertFromDS(nclr->colors[0]);
			fill = REVERSE(fill);
		}
		for (int i = 0; i < (tilesX * tilesY * 8 * 8); i++) out[i] = fill;
		return;
	}

	int ncgrStart = NCGR_CHNAME(info->characterName, mapping, ncgr->nBits);
	for (int y = 0; y < tilesY; y++) {
		for (int x = 0; x < tilesX; x++) {
			COLOR32 block[64];

			int bitsOffset = x * 8 + (y * 8 * tilesX * 8);
			int index;
			if (NCGR_2D(mapping)) {
				int ncx = x + ncgrStart % ncgr->tilesX;
				int ncy = y + ncgrStart / ncgr->tilesX;
				index = ncx + ncgr->tilesX * ncy;
			} else {
				index = ncgrStart + x + y * tilesX;
			}

			ChrRenderCharacter(ncgr, nclr, index, block, info->palette);
			for (int i = 0; i < 8; i++) {
				memcpy(out + bitsOffset + tilesX * 8 * i, block + i * 8, 32);
			}
		}
	}
}

static void CellRenderOBJ_Bitmap(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	//if the mapping mode is 2D mapping, then we can use the same logic as for the character type rendering
	//since we emulate the graphics layout in character order.
	if (mapping == GX_OBJVRAMMODE_CHAR_2D) {
		CellRenderOBJ_Character(out, info, ncgr, nclr, mapping);
		return;
	}

	//the handling for 1D bitmap graphics: bitmaps are placed sequentially, rather than
	//assuming a 2D sheet layout.
	unsigned int mapShift = (mapping >> 20) & 0x3;
	unsigned int iPx = ((info->characterName * 64) << mapShift) >> (ncgr->nBits == 8);  // initial starting index of bitmap data
	unsigned int pxW = 8 * ncgr->tilesX;                                                // width of the simulated sheet in dots

	for (int y = 0; y < info->height; y++) {
		for (int x = 0; x < info->width; x++, iPx++) {
			
			//convert into the internal character order
			unsigned int pxX     = iPx % pxW, pxY     = iPx / pxW;
			unsigned int charX   = pxX / 8,   charY   = pxY / 8;
			unsigned int inCharX = pxX % 8,   inCharY = pxY % 8;
			unsigned int iChar   = charX + charY * ncgr->tilesX;

			//color index lookup
			unsigned int pval = 0;
			if (ncgr != NULL && iChar < (unsigned int) ncgr->nTiles) {
				pval = ncgr->tiles[iChar][inCharX + 8 * inCharY];
			}

			unsigned int cidx = pval + (info->palette << ncgr->nBits);

			//color palette lookup
			COLOR c = 0;
			if (nclr != NULL && cidx < (unsigned int) nclr->nColors) {
				c = nclr->colors[cidx];
			}

			COLOR32 c32 = ColorConvertFromDS(c);
			if (pval > 0) c32 |= 0xFF000000;
			out[x + y * info->width] = REVERSE(c32);
		}
	}
}

static void CellRenderOBJ(COLOR32 *out, GxOamAttrInfo *info, NCGR *ncgr, NCLR *nclr, int mapping) {
	//use the rendering procedure for the type of graphics
	if (ncgr == NULL || !ncgr->bitmap) {
		//character graphics (use on the 2D graphics engine)
		CellRenderOBJ_Character(out, info, ncgr, nclr, mapping);
	} else {
		//bitmap graphics (use on the 3D graphics engine)
		CellRenderOBJ_Bitmap(out, info, ncgr, nclr, mapping);
	}
}

void CellRender(
	COLOR32   *px,
	int       *covbuf,
	NCER      *ncer,
	NCGR      *ncgr,
	NCLR      *nclr,
	NCER_CELL *cell,
	int        xOffs,
	int        yOffs,
	double     a,
	double     b,
	double     c,
	double     d,
	int        forceAffine,
	int        forceDoubleSize
) {
	//adjust (X,Y) offset to center of preview
	xOffs += 256;
	yOffs += 128;

	//compute inverse matrix parameters.
	double invA = 1.0, invB = 0.0, invC = 0.0, invD = 1.0;
	int isMtxIdentity = 1;
	if (a != 1.0 || b != 0.0 || c != 0.0 || d != 1.0) {
		//not identity matrix
		double det = a * d - b * c; // DBCA
		if (det != 0.0) {
			invA =  d / det;
			invB = -b / det;
			invC = -c / det;
			invD =  a / det;
		} else {
			//max scale identity
			invA = 127.99609375;
			invB =   0.0;
			invC =   0.0;
			invD = 127.99609375;
		}
		isMtxIdentity = 0; // not identity
	}

	COLOR32 *block = (COLOR32 *) calloc(64 * 64, sizeof(COLOR32));
	for (int i = cell->nAttribs - 1; i >= 0; i--) {
		GxOamAttrInfo info;
		CellDecodeOamAttributes(&info, cell, i);

		//if OBJ is marked disabled, skip rendering
		if (info.disable) continue;

		CellRenderOBJ(block, &info, ncgr, nclr, ncer->mappingMode);

		//HV flip? Only if not affine!
		if (!(info.rotateScale || forceAffine)) {
			COLOR32 temp[64];
			if (info.flipY) {
				for (int i = 0; i < info.height / 2; i++) {
					memcpy(temp, block + i * info.width, info.width * 4);
					memcpy(block + i * info.width, block + (info.height - 1 - i) * info.width, info.width * 4);
					memcpy(block + (info.height - 1 - i) * info.width, temp, info.width * 4);

				}
			}
			if (info.flipX) {
				for (int i = 0; i < info.width / 2; i++) {
					for (int j = 0; j < info.height; j++) {
						COLOR32 left = block[i + j * info.width];
						block[i + j * info.width] = block[info.width - 1 - i + j * info.width];
						block[info.width - 1 - i + j * info.width] = left;
					}
				}
			}
		}

		int doubleSize = info.doubleSize;
		if ((info.rotateScale || forceAffine) && forceDoubleSize) doubleSize = 1;

		//apply transformation matrix to OBJ position
		int x = SEXT9(info.x);
		int y = SEXT8(info.y);

		//when forcing double size on an OBJ that isn't naturally double size, we'll correct its position.
		if ((forceDoubleSize && (info.rotateScale || forceAffine)) && !info.doubleSize) {
			x -= info.width / 2;
			y -= info.height / 2;
		}

		if (!isMtxIdentity) {
			//adjust coordinates by correction for double-size
			int realWidth = info.width << doubleSize;
			int realHeight = info.height << doubleSize;
			int movedX = x + realWidth / 2;
			int movedY = y + realHeight / 2;

			//un-correct moved position from center to top-left, un-correct for double-size
			x = FloatToInt(movedX * a + movedY * b) - realWidth / 2;
			y = FloatToInt(movedX * c + movedY * d) - realHeight / 2;
		}

		//copy data
		if (!(info.rotateScale || forceAffine)) {
			//adjust for double size
			if (doubleSize) {
				x += info.width / 2;
				y += info.height / 2;
			}

			//no rotate/scale enabled, copy output directly.
			for (int j = 0; j < info.height; j++) {
				int _y = (y + j + yOffs) & 0xFF;
				for (int k = 0; k < info.width; k++) {
					int _x = (x + k + xOffs) & 0x1FF;
					COLOR32 col = block[j * info.width + k];
					if (col >> 24) {
						px[_x + _y * 512] = col;
						if (covbuf != NULL) covbuf[_x + _y * 512] = i + 1; // 0=no OBJ
					}
				}
			}
		} else {
			//transform about center
			int realWidth = info.width << doubleSize;
			int realHeight = info.height << doubleSize;
			double cx = (realWidth - 1) * 0.5; // rotation center X in OBJ
			double cy = (realHeight - 1) * 0.5; // rotation center Y in OBJ

			for (int j = 0; j < realHeight; j++) {
				int destY = (y + j + yOffs) & 0xFF;
				for (int k = 0; k < realWidth; k++) {
					int destX = (x + k + xOffs) & 0x1FF;

					int srcX = FloatToInt(((((double) k) - cx) * invA + (((double) j) - cy) * invB) + cx);
					int srcY = FloatToInt(((((double) k) - cx) * invC + (((double) j) - cy) * invD) + cy);

					//if double size, adjust source coordinate by the excess size
					if (doubleSize) {
						srcX -= realWidth / 4;
						srcY -= realHeight / 4;
					}

					if (srcX >= 0 && srcY >= 0 && srcX < info.width && srcY < info.height) {
						COLOR32 src = block[srcY * info.width + srcX];
						if (src >> 24) {
							px[destX + destY * 512] = src;
							if (covbuf != NULL) covbuf[destX + destY * 512] = i + 1; // 0=no OBJ
						}
					}

				}
			}
		}
	}
	free(block);
}
