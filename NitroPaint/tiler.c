#include "tiler.h"

HBITMAP CreateTileBitmap(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders) {
	return CreateTileBitmap2(lpBits, nWidth, nHeight, hiliteX, hiliteY, pOutWidth, pOutHeight, scale, bBorders, 8, FALSE, FALSE);
}

HBITMAP CreateTileBitmap2(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders, UINT tileWidth, BOOL bReverseColors, BOOL bAlphaBlend) {
	if (tileWidth > 8) return NULL;
	//first off, find the number of tiles.
	unsigned tilesX = nWidth / tileWidth;
	unsigned tilesY = nHeight / tileWidth;

	//next, how many borders do we need?
	int bordersX = tilesX + 1;
	int bordersY = tilesY + 1;
	if (!bBorders) {
		bordersX = 0;
		bordersY = 0;
	}

	//next, find the size of a tile.
	unsigned tileSize = scale * tileWidth;

	//now we can find the size of the output.
	unsigned outWidth = tilesX * tileSize + bordersX;
	unsigned outHeight = tilesY * tileSize + bordersY;

	//allocate a buffer for this.
	LPDWORD lpOutBits = (LPDWORD) calloc(outWidth * outHeight, 4);

	//next, iterate over each tile.
	for (unsigned int tileY = 0; tileY < tilesY; tileY++) {
		for (unsigned int tileX = 0; tileX < tilesX; tileX++) {
			//for this tile, create a 64-dword array to write the tile into.
			DWORD block[64];
			unsigned imgX = tileX * tileWidth;
			unsigned imgY = tileY * tileWidth;
			unsigned offs = imgX + imgY * nWidth;

			//copy to block. This could be achieved by some cool rep movs-type instructions.
			
			unsigned stride = 4 * tileWidth;
			for (unsigned int i = 0; i < tileWidth; i++) {
				memcpy(block + tileWidth * i, lpBits + offs + nWidth * i, stride);
			}

			//if this tile is highlighted, then highlight it.
			if (tileX == hiliteX && tileY == hiliteY) {
				//average the tile with white.
				for (int i = 0; i < 64; i++) {
					DWORD d = block[i];
					int r = d & 0xFF;
					r = (r + 255) >> 1;
					int g = (d >> 8) & 0xFF;
					g = (g + 255) >> 1;
					int b = (d >> 16) & 0xFF;
					b = (b + 255) >> 1;
					block[i] = r | (g << 8) | (b << 16) | (d & 0xFF000000);
				}
			}

			//next, fill out each pixel in the output image.
			for (unsigned int destY = 0; destY < tileSize; destY++) {
				for (unsigned int destX = 0; destX < tileSize; destX++) {
					//first, find destination point.
					int x = destX + (tileX * scale * tileWidth) + tileX + 1;
					int y = destY + (tileY * scale * tileWidth) + tileY + 1;
					if (!bBorders) {
						x -= tileX + 1;
						y -= tileY + 1;
					}

					//next, find the source point in block.
					int sampleX = destX / scale;
					int sampleY = destY / scale;
					DWORD sample = block[sampleX + sampleY * tileWidth];
					if ((sample & 0xFF000000) == 0) {
						int cx = destX / (tileWidth / 2);
						int cy = destY / (tileWidth / 2);
						int cb = (cx ^ cy) & 1;
						if (cb) {
							sample = 0xFFC0C0C0;
						} else {
							sample = 0xFFFFFFFF;
						}
					} else if (bAlphaBlend && (((sample >> 24) & 0xFF) < 255)) {
						int alpha = sample >> 24;
						int cx = destX / (tileWidth / 2);
						int cy = destY / (tileWidth / 2);
						int cb = (cx ^ cy) & 1;
						int bg;
						if (cb) {
							bg = 0xFFC0C0C0;
						} else {
							bg = 0xFFFFFFFF;
						}

						int r = ((bg & 0xFF) * (255 - alpha) + (sample & 0xFF) * alpha) >> 8;
						int g = (((bg >> 8) & 0xFF) * (255 - alpha) + ((sample >> 8) & 0xFF) * alpha) >> 8;
						int b = (((bg >> 16) & 0xFF) * (255 - alpha) + ((sample >> 16) & 0xFF) * alpha) >> 8;
						sample = r | (g << 8) | (b << 16) | 0xFF000000;
					}

					//write it
					lpOutBits[x + y * outWidth] = sample;
				}
			}
		}
	}

	*pOutWidth = outWidth;
	*pOutHeight = outHeight;

	if (bReverseColors) {
		for (unsigned int i = 0; i < outWidth * outHeight; i++) {
			DWORD d = lpOutBits[i];
			d = (d & 0xFF00FF00) | ((d & 0xFF0000) >> 16) | ((d & 0xFF) << 16);
			lpOutBits[i] = d;
		}
	}

	//create bitmap
	HBITMAP hBitmap = CreateBitmap(outWidth, outHeight, 1, 32, lpOutBits);

	//free out bits
	free(lpOutBits);

	return hBitmap;
	
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