#include "gdip.h"
#include "texture.h"
#include <Windows.h>

static int startup = 0;

int isTGA(BYTE *buffer, DWORD dwSize) {
	if (dwSize < 0x12) return 0;
	BYTE commentLength = *buffer;
	if (dwSize < commentLength + 0x12u) return 0;
	if (buffer[1] != 0 && buffer[1] != 1) return 0;
	if (buffer[2] > 11) return 0;
	if (*(WORD *) (buffer + 5) != 0) return 0; //we don't support color table TGAs
	if (buffer[2] != 2 && buffer[2] != 10) return 0; //only RGB and RLE are supported
	return 1;
}

DWORD *readTga(BYTE *buffer, DWORD dwSize, int *pWidth, int *pHeight) {
	int type = (int) *(BYTE *) (buffer + 2);
	int width = (int) *(short *) (buffer + 0x0C);
	int height = (int) *(short *) (buffer + 0x0E);
	*pWidth = width;
	*pHeight = height;
	int dataOffset = ((int) *buffer) + 0x12;
	int depth = ((int) *(buffer + 0x10)) >> 3;
	buffer += dataOffset;
	DWORD * pixels = (DWORD *) calloc(width * height, 4);
	if (type == 2) {
		for (int i = 0; i < width * height; i++) {
			int x, y, ay, destIndex, offs;
			UCHAR b, g, r, a;
			x = i % width;
			y = i / width;
			ay = height - 1 - y;
			destIndex = ay * width + x;

			offs = i * depth;
			b = buffer[offs];
			g = buffer[offs + 1];
			r = buffer[offs + 2];
			a = (depth == 4) ? buffer[offs + 3] : 0xFF;
			pixels[destIndex] = r | (g << 8) | (b << 16) | (a << 24);
		}
	} else if (type == 10) {
		int nPixelsRead = 0, offset = 0, i = 0;
		DWORD *line = (DWORD *) calloc(width, 4);
		while (nPixelsRead < width * height) {
			BYTE b = buffer[offset];
			int num = (b & 0x7F) + 1;
			offset++;
			if (b & 0x80) {	//run-length encoded
				DWORD col = 0;
				if (depth == 4) col = *(DWORD *) (buffer + offset);
				if (depth == 3) col = (*(BYTE *) (buffer + offset)) | ((*(BYTE *) (buffer + offset + 1)) << 8) | ((*(BYTE *) (buffer + offset + 2)) << 16) | 0xFF000000;
				col = (col & 0xFF00FF00) | ((col & 0xFF) << 16) | ((col & 0xFF0000) >> 16);
				for (i = 0; i < num; i++) pixels[nPixelsRead + i] = col;
				offset += depth;
			} else { //raw data
				for (i = 0; i < num; i++) {
					DWORD col = 0;
					if (depth == 4) col = *(DWORD *) (buffer + offset + 4 * i);
					if (depth == 3) col = (*(BYTE *) (buffer + offset + 3 * i)) | ((*(BYTE *) (buffer + offset + 1 + 3 * i)) << 8) | ((*(BYTE *) (buffer + offset + 2 + 3 * i)) << 16) | 0xFF000000;
					col = (col & 0xFF00FF00) | ((col & 0xFF) << 16) | ((col & 0xFF0000) >> 16);
					pixels[nPixelsRead + i] = col;
				}
				offset += depth * num;
			}
			nPixelsRead += num;
		}
		//flip vertically
		for (i = 0; i < (height >> 1); i++) {
			memcpy(line, pixels + (width * i), width << 2);
			memcpy(pixels + (width * i), pixels + (width * (height - 1 - i)), width << 2);
			memcpy(pixels + (width * (height - 1 - i)), line, width << 2);
		}

		free(line);
	}

	return pixels;
}

COLOR32 *gdipReadImage(LPCWSTR lpszFileName, int *pWidth, int *pHeight) {
	return gdipReadImageEx(lpszFileName, pWidth, pHeight, NULL, NULL);
}

COLOR32 *gdipReadImageEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, COLOR32 **pImagePalette, int *pPaletteSize) {
	if (!startup) {
		STARTUPINPUT si = { 0 };
		si.GdiplusVersion = 1;
		ULONG_PTR token;
		GdiplusStartup(&token, &si, 0);
		startup = 1;
	}

	//test for valid file, or TGA file, which GDI+ does not support.
	HANDLE hFile = CreateFile(lpszFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}
	DWORD dwSizeHigh, dwSizeLow, dwRead;
	dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
	BYTE *buffer = (BYTE *) calloc(dwSizeLow, 1);
	ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	CloseHandle(hFile);
	if (isTGA(buffer, dwSizeLow)) {
		COLOR32 *pixels = NULL;
		pixels = readTga(buffer, dwSizeLow, pWidth, pHeight);
		free(buffer);
		return pixels;
	}
	free(buffer);

	RECT r = { 0 };
	BYTE *gp = NULL;
	BITMAPDATA bm;
	INT paletteSize = 0;
	ColorPalette *palette = NULL;
	GdipCreateBitmapFromFile(lpszFileName, (void *) &gp);
	GdipGetImageWidth(gp, (INT *) &(r.right));
	GdipGetImageHeight(gp, (INT *) &(r.bottom));
	GdipBitmapLockBits(gp, (void *) &r, 3, 0x0026200A, (void *) &bm);

	GdipGetImagePaletteSize(gp, &paletteSize);
	palette = (ColorPalette *) calloc(1, paletteSize);
	if (palette != NULL && (GdipGetImagePalette(gp, palette, paletteSize), palette->Count > 0)) {
		COLOR32 *colors = (COLOR32 *) palette->Entries;
		if (pImagePalette != NULL) {
			*pImagePalette = (COLOR32 *) calloc(palette->Count, sizeof(COLOR32));
			memcpy(*pImagePalette, colors, palette->Count * sizeof(COLOR32));
		}
		if (pPaletteSize != NULL) {
			*pPaletteSize = palette->Count;
		}
	} else {
		if (pImagePalette != NULL) *pImagePalette = NULL;
		if (pPaletteSize != NULL) *pPaletteSize = 0;
	}
	if (palette != NULL) free(palette);

	COLOR32 *px = (COLOR32 *) calloc(r.right * r.bottom, 4);
	*pWidth = r.right;
	*pHeight = r.bottom;
	COLOR32 *scan0 = (COLOR32 *) bm.d1[4];
	for (int i = 0; i < r.right * r.bottom; i++) {
		COLOR32 d = scan0[i];
		d = ((d & 0xFF) << 16) | (d & 0xFF00FF00) | ((d >> 16) & 0xFF);
		px[i] = d;
	}
	GdipDisposeImage(gp);
	return px;
}

void writeImage(DWORD *px, int width, int height, LPCWSTR name) {
	if (!startup) {
		STARTUPINPUT si = { 0 };
		si.GdiplusVersion = 1;
		ULONG_PTR token;
		GdiplusStartup(&token, &si, 0);
		startup = 1;
	}
	BYTE clsid[] = { 0x06, 0xf4, 0x7c, 0x55, 0x04, 0x1a, 0xd3, 0x11, 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e };
	void *bitmap = NULL;
	GdipCreateBitmapFromScan0(width, height, width * 4, 0x0026200A, px, &bitmap);
	GdipSaveImageToFile(bitmap, name, clsid, NULL);
	GdipDisposeImage(bitmap);
}