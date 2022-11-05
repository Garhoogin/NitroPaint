#include <Windows.h>
#include <wincodec.h>

#include "gdip.h"

#pragma comment(lib, "windowscodecs.lib")

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

#define CHECK_RESULT(x) if(!SUCCEEDED(x)) goto cleanup

HRESULT imageWriteInternal(LPCWSTR path, void *scan0, WICPixelFormatGUID *format, int width, int height, int stride, int scan0Size, COLOR32 *palette, int paletteSize) {
	//WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
	IWICImagingFactory *factory = NULL;
	IWICStream *stream = NULL;
	IWICPalette *wicPalette = NULL;
	IWICBitmapEncoder *encoder = NULL;
	IWICBitmapFrameEncode *frameEncode = NULL;
	WICColor *pWicColors = NULL;

	//get factory instance
	HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &factory);
	CHECK_RESULT(result);

	//create stream
	result = factory->lpVtbl->CreateStream(factory, &stream);
	CHECK_RESULT(result);

	//point stream to file
	result = stream->lpVtbl->InitializeFromFilename(stream, path, GENERIC_WRITE);
	CHECK_RESULT(result);

	//create palette
	if (palette != NULL) {
		result = factory->lpVtbl->CreatePalette(factory, &wicPalette);
		CHECK_RESULT(result);

		pWicColors = (WICColor *) calloc(paletteSize, sizeof(WICColor));
		for (int i = 0; i < paletteSize; i++) {
			pWicColors[i] = palette[i];
		}
		result = wicPalette->lpVtbl->InitializeCustom(wicPalette, pWicColors, paletteSize);
		CHECK_RESULT(result);
	}

	//create bitmap encoder
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder));
	SUCCEEDED(result) && (result = encoder->lpVtbl->Initialize(encoder, (IStream *) stream, WICBitmapEncoderNoCache));

	//create frame encoder
	SUCCEEDED(result) && (result = encoder->lpVtbl->CreateNewFrame(encoder, &frameEncode, NULL));
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->Initialize(frameEncode, NULL));

	//provide image data
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->SetSize(frameEncode, width, height));
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->SetPixelFormat(frameEncode, format));
	SUCCEEDED(result) && (wicPalette != NULL) && (result = frameEncode->lpVtbl->SetPalette(frameEncode, wicPalette));
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->WritePixels(frameEncode, height, stride, scan0Size, scan0));

	//flush data
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->Commit(frameEncode));
	SUCCEEDED(result) && (result = encoder->lpVtbl->Commit(encoder));
	SUCCEEDED(result) && (result = stream->lpVtbl->Commit(stream, STGC_DEFAULT));

cleanup:
	if (stream != NULL)
		stream->lpVtbl->Release(stream);
	if (frameEncode != NULL)
		frameEncode->lpVtbl->Release(frameEncode);
	if (encoder != NULL)
		encoder->lpVtbl->Release(encoder);
	if (wicPalette != NULL)
		wicPalette->lpVtbl->Release(wicPalette);
	if (factory != NULL)
		factory->lpVtbl->Release(factory);
	if (pWicColors != NULL)
		free(pWicColors);
	return result;
}

HRESULT imageReadInternal(LPCWSTR path, COLOR32 **ppPixels, unsigned char **ppIndices, int *pWidth, int *pHeight, COLOR32 **ppPalette, int *pPaletteSize) {
	IWICImagingFactory *factory = NULL;
	IWICStream *stream = NULL;
	IWICBitmapDecoder *decoder = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICPalette *wicPalette = NULL;
	WICColor *wicPaletteColors = NULL;
	IWICFormatConverter *converter = NULL;
	WICPixelFormatGUID trueColorFormat, pixelFormat;
	COLOR32 *pxBuffer = NULL;
	unsigned char *scan0 = NULL, *indices = NULL;

	//get factory instance
	HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &factory);
	CHECK_RESULT(result);

	//create stream
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateStream(factory, &stream));
	SUCCEEDED(result) && (result = stream->lpVtbl->InitializeFromFilename(stream, path, GENERIC_READ));
	CHECK_RESULT(result);

	//decode image
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateDecoderFromStream(factory, (IStream *) stream, 
		NULL, WICDecodeMetadataCacheOnDemand, &decoder));
	SUCCEEDED(result) && (result = decoder->lpVtbl->GetFrame(decoder, 0, &frame));
	CHECK_RESULT(result);

	//get image size
	result = frame->lpVtbl->GetSize(frame, pWidth, pHeight);
	CHECK_RESULT(result);

	//init palette
	result = factory->lpVtbl->CreatePalette(factory, &wicPalette);
	CHECK_RESULT(result);

	//write palette output
	if (ppPalette != NULL) {
		*ppPalette = NULL;
		*pPaletteSize = 0;
	}
	if (ppPalette != NULL && SUCCEEDED(frame->lpVtbl->CopyPalette(frame, wicPalette))) {
		result = wicPalette->lpVtbl->GetColorCount(wicPalette, pPaletteSize);
		CHECK_RESULT(result);

		UINT nActualColors;
		wicPaletteColors = (WICColor *) calloc(*pPaletteSize, sizeof(WICColor));
		result = wicPalette->lpVtbl->GetColors(wicPalette, *pPaletteSize, wicPaletteColors, &nActualColors);
		CHECK_RESULT(result);

		//same format, will need to swap red and blue
		*ppPalette = (COLOR32 *) wicPaletteColors;
		for (int i = 0; i < *pPaletteSize; i++) {
			WICColor c = wicPaletteColors[i];
			c = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
			wicPaletteColors[i] = c;
		}
	}

	//read RGB pixel data
	memcpy(&trueColorFormat, &GUID_WICPixelFormat32bppBGRA, sizeof(trueColorFormat));
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateFormatConverter(factory, &converter));
	SUCCEEDED(result) && (result = converter->lpVtbl->Initialize(converter, (IWICBitmapSource *) frame,
		&trueColorFormat, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom));
	CHECK_RESULT(result);

	//read converted pixels
	int width = *pWidth, height = *pHeight;
	int rgbStride = *pWidth * 4;
	int pxBufferSize = rgbStride * *pHeight;
	pxBuffer = (COLOR32 *) calloc(pxBufferSize, 1);
	result = converter->lpVtbl->CopyPixels(converter, NULL, rgbStride, pxBufferSize, (BYTE *) pxBuffer);

	//swap red and blue channels
	int nPx = width * height;
	for (int i = 0; i < nPx; i++) {
		COLOR32 c = pxBuffer[i];
		c = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
		pxBuffer[i] = c;
	}
	*ppPixels = pxBuffer;

	//read index data
	if (ppIndices != NULL) {
		//get pixel format
		result = frame->lpVtbl->GetPixelFormat(frame, &pixelFormat);
		CHECK_RESULT(result);

		//check for 8bpp and 4bpp, else don't return any index data
		int depth = 0;
		if (memcmp(&pixelFormat, &GUID_WICPixelFormat8bppIndexed, sizeof(GUID)) == 0) {
			depth = 8;
		} else if (memcmp(&pixelFormat, &GUID_WICPixelFormat4bppIndexed, sizeof(GUID)) == 0) {
			depth = 4;
		} else {
			*ppIndices = NULL;
		}

		//if indexed to 4bpp or 8bpp
		if (depth != 0) {
			int stride = ((width * depth + 7) / 8 + 3) & ~3;
			int scan0Size = stride * height;
			scan0 = (unsigned char *) calloc(scan0Size, 1);

			//read pixels in
			result = frame->lpVtbl->CopyPixels(frame, NULL, stride, scan0Size, scan0);
			CHECK_RESULT(result);

			indices = (unsigned char *) calloc(width * height, 1);

			//copy in rows
			for (int y = 0; y < height; y++) {
				unsigned char *rowSrc = scan0 + y * stride;
				unsigned char *rowDst = indices + y * width;

				if (depth == 8) {
					memcpy(rowDst, rowSrc, width);
				} else {
					for (int x = 0; x < width; x++) {
						rowDst[x] = (rowSrc[x / 2] >> (((x ^ 1) & 1) * 4)) & 0xF;
					}
				}
			}
			*ppIndices = indices;
			free(scan0);
			scan0 = NULL;
		}
	}

cleanup:
	if (converter != NULL)
		converter->lpVtbl->Release(converter);
	if (wicPalette != NULL)
		wicPalette->lpVtbl->Release(wicPalette);
	if (frame != NULL)
		frame->lpVtbl->Release(frame);
	if (decoder != NULL)
		decoder->lpVtbl->Release(decoder);
	if (stream != NULL)
		stream->lpVtbl->Release(stream);
	if (factory != NULL)
		factory->lpVtbl->Release(factory);
	if (scan0 != NULL)
		free(scan0);
	if (!SUCCEEDED(result)) {
		*pWidth = 0;
		*pHeight = 0;
		if (pPaletteSize != NULL)
			*pPaletteSize = 0;
		if (ppPalette != NULL)
			*ppPalette = NULL;
		if (ppPixels != NULL)
			*ppPixels = NULL;
		if (ppIndices != NULL)
			*ppIndices = NULL;
		if (wicPaletteColors != NULL)
			free(wicPaletteColors);
		if (pxBuffer != NULL)
			free(pxBuffer);
		if (indices != NULL)
			free(indices);
	}
	return result;
}

HRESULT imageWriteIndexed(unsigned char *bits, int width, int height, COLOR32 *palette, int paletteSize, LPCWSTR path) {
	int depth = paletteSize <= 16 ? 4 : 8;
	int stride = ((width * depth + 7) / 8 + 3) & ~3;

	//allocate and populate scan0
	int scan0Size = stride * height;
	unsigned char *scan0 = (unsigned char *) calloc(height, stride);
	for (int y = 0; y < height; y++) {
		unsigned char *rowDest = scan0 + y * stride;
		unsigned char *rowSrc = bits + y * width;

		if (depth == 8) {
			memcpy(rowDest, bits + y * width, width);
		} else {
			for (int x = 0; x < width; x++) {
				int index = rowSrc[x];
				rowDest[x / 2] |= index << (((x ^ 1) & 1) * 4);
			}
		}
	}

	//create palette copy to swap red/blue order
	COLOR32 *paletteCopy = (COLOR32 *) calloc(paletteSize, 4);
	for (int i = 0; i < paletteSize; i++) {
		COLOR32 c = palette[i];
		c = (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
		paletteCopy[i] = c;
	}

	//GUID allocated on the stack since it must be writable for some reason
	WICPixelFormatGUID format;
	memcpy(&format, depth == 4 ? &GUID_WICPixelFormat4bppIndexed : &GUID_WICPixelFormat8bppIndexed, sizeof(format));
	HRESULT result = imageWriteInternal(path, scan0, &format, width, height, stride, scan0Size, paletteCopy, paletteSize);
	free(scan0);
	free(paletteCopy);
	return result;
}

HRESULT imageWrite(COLOR32 *px, int width, int height, LPCWSTR path) {
	int stride = width * 4;
	WICPixelFormatGUID format;
	memcpy(&format, &GUID_WICPixelFormat32bppBGRA, sizeof(format));
	HRESULT result = imageWriteInternal(path, px, &format, width, height, stride, stride * height, NULL, 0);
	return result;
}

COLOR32 *imageRead(LPCWSTR path, int *pWidth, int *pHeight) {
	COLOR32 *bits = NULL;
	imageReadInternal(path, &bits, NULL, pWidth, pHeight, NULL, NULL);
	return bits;
}

COLOR32 *imageReadIndexed(LPCWSTR path, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **outPalette, int *outPaletteSize) {
	COLOR32 *bits = NULL;
	imageReadInternal(path, &bits, indices, pWidth, pHeight, outPalette, outPaletteSize);
	return bits;
}



COLOR32 *gdipReadImage(LPCWSTR lpszFileName, int *pWidth, int *pHeight) {
	return gdipReadImageEx(lpszFileName, pWidth, pHeight, NULL, NULL, NULL);
}

COLOR32 *gdipReadImageEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize) {
	//test for valid file, or TGA file, which WIC does not support.
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

	return imageReadIndexed(lpszFileName, pWidth, pHeight, indices, pImagePalette, pPaletteSize);
}

void writeImage(COLOR32 *pixels, int width, int height, LPCWSTR lpszFileName) {
	imageWrite(pixels, width, height, lpszFileName);
}
