#include "tiler.h"

HBITMAP CreateTileBitmap(LPDWORD lpBits, UINT nWidth, UINT nHeight, int hiliteX, int hiliteY, PUINT pOutWidth, PUINT pOutHeight, UINT scale, BOOL bBorders) {
	//first off, find the number of tiles.
	unsigned tilesX = nWidth >> 3;
	unsigned tilesY = nHeight >> 3;

	//next, how many borders do we need?
	int bordersX = tilesX + 1;
	int bordersY = tilesY + 1;
	if (!bBorders) {
		bordersX = 0;
		bordersY = 0;
	}

	//next, find the size of a tile.
	unsigned tileSize = scale << 3;

	//now we can find the size of the output.
	unsigned outWidth = tilesX * tileSize + bordersX;
	unsigned outHeight = tilesY * tileSize + bordersY;

	//allocate a buffer for this.
	LPDWORD lpOutBits = (LPDWORD) calloc(outWidth * outHeight, 4);

	//next, iterate over each tile.
	for (int tileY = 0; tileY < tilesY; tileY++) {
		for (int tileX = 0; tileX < tilesX; tileX++) {
			//for this tile, create a 64-dword array to write the tile into.
			volatile DWORD block[64];
			unsigned imgX = tileX << 3;
			unsigned imgY = tileY << 3;
			unsigned offs = imgX + imgY * nWidth;

			//copy to block. This could be achieved by some cool rep movs-type instructions.
			
			memcpy(block, lpBits + offs, 32);
			memcpy(block + 8, lpBits + offs + nWidth, 32);
			memcpy(block + 16, lpBits + offs + nWidth * 2, 32);
			memcpy(block + 24, lpBits + offs + nWidth * 3, 32);
			memcpy(block + 32, lpBits + offs + nWidth * 4, 32);
			memcpy(block + 40, lpBits + offs + nWidth * 5, 32);
			memcpy(block + 48, lpBits + offs + nWidth * 6, 32);
			memcpy(block + 56, lpBits + offs + nWidth * 7, 32);

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
			for (int destY = 0; destY < tileSize; destY++) {
				for (int destX = 0; destX < tileSize; destX++) {
					//first, find destination point.
					int x = destX + (tileX * scale << 3) + tileX + 1;
					int y = destY + (tileY * scale << 3) + tileY + 1;
					if (!bBorders) {
						x -= tileX + 1;
						y -= tileY + 1;
					}

					//next, find the source point in block.
					int sampleX = destX / scale;
					int sampleY = destY / scale;
					DWORD sample = block[sampleX + sampleY * 8];
					if ((sample & 0xFF000000) == 0) {
						int cx = destX / 4;
						int cy = destY / 4;
						int cb = (cx ^ cy) & 1;
						if (cb) {
							sample = 0xFFC0C0C0;
						} else {
							sample = 0xFFFFFFFF;
						}
					}

					//write it
					lpOutBits[x + y * outWidth] = sample;
				}
			}
		}
	}

	*pOutWidth = outWidth;
	*pOutHeight = outHeight;

	//create bitmap
	HBITMAP hBitmap = CreateBitmap(outWidth, outHeight, 1, 32, lpOutBits);

	//free out bits
	free(lpOutBits);

	return hBitmap;
	
}

int getDimension(int tiles, int border, int scale) {
	int width = tiles * 8 * scale;
	if (border) width += 1 + tiles;
	return width;
}