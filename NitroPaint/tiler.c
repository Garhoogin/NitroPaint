#include "tiler.h"

HBITMAP CreateTileBitmap(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders) {
	//first off, find the number of tiles.
	unsigned tilesX = nWidth / 8;
	unsigned tilesY = nHeight / 8;

	//next, how many borders do we need?
	int bordersX = tilesX + 1;
	int bordersY = tilesY + 1;
	if (!bBorders) {
		bordersX = 0;
		bordersY = 0;
	}

	//next, find the size of a tile.
	unsigned tileSize = scale * 8;

	//now we can find the size of the output.
	unsigned outWidth = tilesX * tileSize + bordersX;
	unsigned outHeight = tilesY * tileSize + bordersY;

	//allocate a buffer for this.
	LPDWORD lpOutBits = (LPDWORD) calloc(outWidth * outHeight, 4);
	RenderTileBitmap(lpOutBits, outWidth, outHeight, 0, 0, outWidth, outHeight,
		lpBits, nWidth, nHeight, hiliteX, hiliteY, scale, bBorders, 8, FALSE, FALSE, FALSE);

	//create bitmap
	HBITMAP hBitmap = CreateBitmap(outWidth, outHeight, 1, 32, lpOutBits);

	//free out bits
	free(lpOutBits);

	*pOutWidth = outWidth;
	*pOutHeight = outHeight;

	return hBitmap;
}

void RenderTileBitmap(DWORD *out, UINT outWidth, UINT outHeight, UINT startX, UINT startY, UINT viewWidth, UINT viewHeight, DWORD *lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, UINT scale, BOOL bBorders, UINT tileWidth, BOOL bReverseColors, BOOL blend, BOOL partialImage) {
	unsigned tilesX = nWidth / tileWidth;
	unsigned tilesY = nHeight / tileWidth;
	unsigned tileSize = scale * tileWidth;
	unsigned tileSpacing = (bBorders) ? (tileSize + 1) : tileSize;

	unsigned int fullWidth = tilesX * tileSpacing + !!bBorders;
	unsigned int fullHeight = tilesY * tileSpacing + !!bBorders;

	//coordinate to subract for output 
	unsigned int xBase = partialImage ? startX : 0;
	unsigned int yBase = partialImage ? startY : 0;
	for (unsigned int y = startY; (y - yBase) < outHeight && y < (startY + viewHeight); y++) {
		//is this a border row?
		if (bBorders) {
			if ((y % (tileSize + 1)) == 0) {
				unsigned int rowWidth = min(fullWidth - startX, min(viewWidth, outWidth));
				if (!partialImage) memset(out + (y - yBase) * outWidth + startX, 0, rowWidth * sizeof(DWORD));
				else               memset(out + (y - yBase) * outWidth, 0, rowWidth * sizeof(DWORD));
				continue;
			}
		}

		//tile Y coordinate
		unsigned int srcY;
		if (bBorders) {
			//adjust for gridlines at tileSize intervals
			srcY = ((y - 1) - (y - 1) / (tileSize + 1)) / scale;
		} else {
			srcY = y / scale;
		}
		unsigned int tileY = srcY / tileWidth;

		//left-most border column
		if (bBorders && startX == 0) {
			out[(y - yBase) * outWidth] = 0;
		}

		//scan tiles horizontally
		unsigned int baseDestX = !!bBorders;
		for (unsigned int tileX = 0; tileX < tilesX; tileX++) {
			
			//scan horizontally within tile
			for (unsigned int x = 0; x < tileWidth; x++) {
				unsigned int tileDestX = baseDestX + x * scale;
				if ((tileDestX + scale) <= startX) continue; //before line
				if (tileDestX >= (startX + viewWidth)) break; //end of line

				unsigned int srcX = tileX * tileWidth + x;

				//get color to render
				DWORD sample = lpBits[srcY * nWidth + srcX];

				//render horizontal strip
				for (unsigned int h = 0; h < scale; h++) {
					unsigned int destX = tileDestX + h;
					unsigned int destY = y;
					if (destX < startX || destX >= (startX + viewWidth)) continue;

					//partial image render: subtract relative view coordinates
					destX -= xBase;
					destY -= yBase;
					if (destX >= outWidth || destY >= outHeight) continue;

					DWORD blended = sample;

					//blend with background logic
					unsigned int cx = (x * scale + h) / (tileWidth / 2);
					unsigned int cy = (bBorders ? ((y - 1) % tileSpacing) : (y)) / (tileWidth / 2);
					int cb = (cx ^ cy) & 1;
					if ((sample & 0xFF000000) == 0) {
						if (cb) {
							blended = 0xFFC0C0C0;
						} else {
							blended = 0xFFFFFFFF;
						}
					} else if (blend && (((sample >> 24) & 0xFF) < 255)) {
						unsigned int alpha = sample >> 24;
						DWORD bg;
						if (cb) {
							bg = 0xFFC0C0C0;
						} else {
							bg = 0xFFFFFFFF;
						}

						unsigned int r = (((bg >>  0) & 0xFF) * (255 - alpha) + ((sample >>  0) & 0xFF) * alpha) >> 8;
						unsigned int g = (((bg >>  8) & 0xFF) * (255 - alpha) + ((sample >>  8) & 0xFF) * alpha) >> 8;
						unsigned int b = (((bg >> 16) & 0xFF) * (255 - alpha) + ((sample >> 16) & 0xFF) * alpha) >> 8;
						blended = r | (g << 8) | (b << 16) | 0xFF000000;
					}

					//highlighted tile?
					if (tileX == hiliteX && tileY == hiliteY) {
						//bit magic to blend with white
						blended = (blended & 0xFF000000) | (blended >> 1) | 0x808080;
					}

					out[destX + destY * outWidth] = blended;
				}
			}

			//draw vertical borders
			if (bBorders) {
				unsigned int destX = baseDestX + tileWidth * scale;
				unsigned int destY = y;
				if (destX >= xBase && destY >= yBase && (destX - xBase) < outWidth && (destY - yBase) < outHeight) {
					out[(destX - xBase) + (destY - yBase) * outWidth] = 0;
				}
			}

			baseDestX += tileSpacing;
		}
	}

	if (bReverseColors) {
		for (unsigned int i = 0; i < outWidth * outHeight; i++) {
			DWORD d = out[i];
			d = (d & 0xFF00FF00) | ((d & 0xFF0000) >> 16) | ((d & 0xFF) << 16);
			out[i] = d;
		}
	}
}

int getDimension2(int tiles, int border, int scale, int tileSize) {
	int width = tiles * tileSize * scale;
	if (border) width += 1 + tiles;
	return width;
}

int getDimension(int tiles, int border, int scale) {
	int width = tiles * 8 * scale;
	if (border) width += 1 + tiles;
	return width;
}