#include <Windows.h>
#include <wincodec.h>

#include "gdip.h"

#pragma comment(lib, "windowscodecs.lib")

#define CMAP_NONE     0
#define CMAP_PRESENT  1

#define CTYPE_NONE         0x00
#define CTYPE_CMAP         0x01
#define CTYPE_DIRECT       0x02
#define CTYPE_GRAYSCALE    0x03
#define CTYPE_FMT_MASK     0x03
#define CTYPE_RLE          0x08

int ImgIsValidTGA(const unsigned char *buffer, unsigned int dwSize) {
	if (dwSize < 0x12) return 0;

	unsigned int commentLength = buffer[0x00];
	int colorMapType = buffer[0x01];
	int colorType = buffer[0x02];
	int colorFormat = colorType & CTYPE_FMT_MASK;
	int colorMapStart = *(uint16_t *) (buffer + 0x03);
	int colorMapSize = *(uint16_t *) (buffer + 0x05);
	int colorMapDepth = buffer[0x07];
	int depth = buffer[0x10];
	int attr = buffer[0x11];
	
	if (dwSize < commentLength + 0x12u) return 0;
	if (colorFormat == CTYPE_NONE) return 0;
	if (colorMapType != CMAP_NONE && colorMapType != CMAP_PRESENT) return 0;
	if (colorFormat == CTYPE_CMAP && colorMapType != CMAP_PRESENT) return 0; //color map not present but should be?
	if (colorType & ~(CTYPE_FMT_MASK | CTYPE_RLE)) return 0; //unallowed format
	if (colorFormat == CTYPE_CMAP && colorMapSize == 0) return 0; //should have a color map size > 0 if required
	if (colorFormat == CTYPE_CMAP && colorMapDepth == 0) return 0; //color depth 0??
	if (colorMapStart > 255 || colorMapSize > 256 || (colorMapStart + colorMapSize) > 256) return 0;
	if (colorFormat != CTYPE_DIRECT && colorFormat != CTYPE_CMAP) return 0; //only direct color and color map supported
	if (attr & 0xC3) return 0; //unsupported pixel arrangements and alpha depths
	if (depth & 3) return 0; //non-multiples-of-8 depths not supported right now (ever?)
	return 1;
}

static void ImgiReadTgaDirect(COLOR32 *pixels, int width, int height, const unsigned char *buffer, int depth, int rle) {
	int nPx = width * height;
	if (!rle) {
		int offset = 0;
		for (int i = 0; i < nPx; i++) {
			int x = i % width, y = i / width;
			int destIndex = y * width + x;

			//read color values
			const uint8_t *rgb = buffer + offset;
			if (depth == 4) {
				pixels[destIndex] = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | (rgb[3] << 24);
			} else if (depth == 3) {
				pixels[destIndex] = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | 0xFF000000;
			}
			offset += depth;
		}
	} else {
		int nPixelsRead = 0, offset = 0, i = 0;
		while (nPixelsRead < nPx) {
			COLOR32 col = 0;
			int b = buffer[offset++];
			int num = (b & 0x7F) + 1, rlFlag = b & 0x80;

			//process run of pixels
			for (i = 0; i < num; i++) {
				//read color values
				const uint8_t *rgb = buffer + offset;
				if (depth == 4) {
					col = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | (rgb[3] << 24);
				} else if (depth == 3) {
					col = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | 0xFF000000;
				}

				//write and increment
				pixels[nPixelsRead + i] = col;
				if (!rlFlag) offset += depth;
			}
			if (rlFlag) offset += depth;
			nPixelsRead += num;
		}
	}
}

static void ImgiReadTgaMapped(COLOR32 *px, int width, int height, const unsigned char *buffer, int tableBase, int tableSize, int tableDepth, int rle) {
	COLOR32 *palette = (COLOR32 *) calloc(tableBase + tableSize, sizeof(COLOR32));
	int nPx = width * height;

	//read palette
	for (int i = 0; i < tableSize; i++) {
		const uint8_t *rgb = buffer + i * tableDepth;
		if (tableDepth == 4) {
			palette[i + tableBase] = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | (rgb[3] << 24);
		} else {
			palette[i + tableBase] = rgb[2] | (rgb[1] << 8) | (rgb[0] << 16) | 0xFF000000;
		}
	}
	buffer += tableSize * tableDepth;

	if (!rle) {
		//read pixel colors from palette
		for (int i = 0; i < nPx; i++) {
			int index = buffer[i];
			if (index < tableBase + tableSize) {
				px[i] = palette[index];
			}
		}
	} else {
		//TODO
	}
	free(palette);
}

static COLOR32 *ImgiReadTga(const BYTE *buffer, DWORD dwSize, int *pWidth, int *pHeight) {
	int dataOffset = buffer[0x00] + 0x12;
	int colorType = buffer[0x02];
	int depth = buffer[0x10] >> 3;
	int attr = buffer[0x11];
	int colorTableBase = *(uint16_t *) (buffer + 0x03);
	int colorTableLength = *(uint16_t *) (buffer + 0x05);
	int colorTableDepth = buffer[0x07] >> 3;
	int width = *(uint16_t *) (buffer + 0x0C);
	int height = *(uint16_t *) (buffer + 0x0E);
	int colorFormat = colorType & CTYPE_FMT_MASK;

	*pWidth = width;
	*pHeight = height;
	buffer += dataOffset;

	int needsVFlip = !(attr & 0x20);  //flipped by default, we interpret this backwards for convenience
	int needsHFlip = !!(attr & 0x10); //actual H flip
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, 4);
	switch (colorFormat) {
		case CTYPE_DIRECT:
			ImgiReadTgaDirect(pixels, width, height, buffer, depth, colorType & CTYPE_RLE);
			break;
		case CTYPE_CMAP:
			ImgiReadTgaMapped(pixels, width, height, buffer, colorTableBase, colorTableLength, colorTableDepth, colorType & CTYPE_RLE);
			break;
		case CTYPE_GRAYSCALE: //unsupported
			break;
	}

	//perform necessary flips
	ImgFlip(pixels, width, height, needsHFlip, needsVFlip);
	return pixels;
}

#define CHECK_RESULT(x) if(!SUCCEEDED(x)) goto cleanup

static HRESULT ImgiWrite(LPCWSTR path, void *scan0, WICPixelFormatGUID *format, int width, int height, int stride, int scan0Size, COLOR32 *palette, int paletteSize) {
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

static HRESULT ImgiRead(LPCWSTR path, COLOR32 **ppPixels, unsigned char **ppIndices, int *pWidth, int *pHeight, COLOR32 **ppPalette, int *pPaletteSize) {
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

HRESULT ImgWriteIndexed(unsigned char *bits, int width, int height, COLOR32 *palette, int paletteSize, LPCWSTR path) {
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
	HRESULT result = ImgiWrite(path, scan0, &format, width, height, stride, scan0Size, paletteCopy, paletteSize);
	free(scan0);
	free(paletteCopy);
	return result;
}

HRESULT ImgWrite(COLOR32 *px, int width, int height, LPCWSTR path) {
	COLOR32 *bits = (COLOR32 *) calloc(height, width * 4);
	for (int i = 0; i < width * height; i++) {
		COLOR32 c = px[i];
		c = (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
		bits[i] = c;
	}

	int stride = width * 4;
	WICPixelFormatGUID format;
	memcpy(&format, &GUID_WICPixelFormat32bppBGRA, sizeof(format));
	HRESULT result = ImgiWrite(path, bits, &format, width, height, stride, stride * height, NULL, 0);
	free(bits);
	return result;
}

COLOR32 *ImgReadEx(LPCWSTR lpszFileName, int *pWidth, int *pHeight, unsigned char **indices, COLOR32 **pImagePalette, int *pPaletteSize) {
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
	if (ImgIsValidTGA(buffer, dwSizeLow)) {
		COLOR32 *pixels = NULL;
		pixels = ImgiReadTga(buffer, dwSizeLow, pWidth, pHeight);
		free(buffer);
		return pixels;
	}
	free(buffer);

	COLOR32 *bits = NULL;
	ImgiRead(lpszFileName, &bits, indices, pWidth, pHeight, pImagePalette, pPaletteSize);
	return bits;
}

COLOR32 *ImgRead(LPCWSTR path, int *pWidth, int *pHeight) {
	return ImgReadEx(path, pWidth, pHeight, NULL, NULL, NULL);
}

void ImgFlip(COLOR32 *px, int width, int height, int hFlip, int vFlip) {
	//V flip
	if (vFlip) {
		COLOR32 *rowbuf = (COLOR32 *) calloc(width, sizeof(COLOR32));
		for (int y = 0; y < height / 2; y++) {
			memcpy(rowbuf, px + y * width, width * sizeof(COLOR32));
			memcpy(px + y * width, px + (height - 1 - y) * width, width * sizeof(COLOR32));
			memcpy(px + (height - 1 - y) * width, rowbuf, width * sizeof(COLOR32));
		}
		free(rowbuf);
	}

	//H flip
	if (hFlip) {
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width / 2; x++) {
				COLOR32 left = px[x + y * width];
				COLOR32 right = px[width - 1 - x + y * width];
				px[x + y * width] = right;
				px[width - 1 - x + y * width] = left;
			}
		}
	}
}

void ImgSwapRedBlue(COLOR32 *px, int width, int height) {
	for (int i = 0; i < width * height; i++) {
		COLOR32 c = px[i];
		px[i] = REVERSE(c);
	}
}

static int ImgiPixelComparator(const void *p1, const void *p2) {
	return *(COLOR32 *) p1 - (*(COLOR32 *) p2);
}

int ImgCountColors(COLOR32 *px, int nPx) {
	//sort the colors by raw RGB value. This way, same colors are grouped.
	COLOR32 *copy = (COLOR32 *) malloc(nPx * 4);
	memcpy(copy, px, nPx * 4);
	qsort(copy, nPx, 4, ImgiPixelComparator);
	int nColors = 0;
	int hasTransparent = 0;
	for (int i = 0; i < nPx; i++) {
		int a = copy[i] >> 24;
		if (!a) hasTransparent = 1;
		else {
			COLOR32 color = copy[i] & 0xFFFFFF;
			//has this color come before?
			int repeat = 0;
			if (i) {
				COLOR32 comp = copy[i - 1] & 0xFFFFFF;
				if (comp == color) {
					repeat = 1;
				}
			}
			if (!repeat) {
				nColors++;
			}
		}
	}
	free(copy);
	return nColors + hasTransparent;
}

void ImgCropInPlace(COLOR32 *px, int width, int height, COLOR32 *out, int srcX, int srcY, int srcWidth, int srcHeight) {
	//copy from px to out
	for (int y = srcY; y < srcY + srcHeight; y++) {
		for (int x = srcX; x < srcX + srcWidth; x++) {
			if (x < 0 || x >= width || y < 0 || y >= height) {
				//fill with transparent for out of bounds
				out[x - srcX + (y - srcY) * srcWidth] = 0;
			} else {
				//write pixel
				out[x - srcX + (y - srcY) * srcWidth] = px[x + y * width];
			}
		}
	}
}

COLOR32 *ImgCrop(COLOR32 *px, int width, int height, int srcX, int srcY, int srcWidth, int srcHeight) {
	COLOR32 *out = (COLOR32 *) calloc(srcWidth * srcHeight, sizeof(COLOR32));
	ImgCropInPlace(px, width, height, out, srcX, srcY, srcWidth, srcHeight);
	return out;
}

COLOR32 *ImgComposite(COLOR32 *back, int backWidth, int backHeight, COLOR32 *front, int frontWidth, int frontHeight, int *outWidth, int *outHeight) {
	//create output image with dimension min(<backWidth, backHeight>, <frontWidth, frontHeight>)
	int width = min(backWidth, frontWidth);
	int height = min(backHeight, frontHeight);
	*outWidth = width;
	*outHeight = height;

	COLOR32 *out = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			COLOR32 fg = front[x + y * frontWidth];
			COLOR32 bg = back[x + y * backWidth];
			unsigned int af = fg >> 24;
			unsigned int ab = bg >> 24;

			if (af == 255) {
				//if foreground opaque, write foreground
				out[x + y * width] = fg;
			} else if (af == 0) {
				//if foreground transparent, write background
				out[x + y * width] = bg;
			} else {
				//compute coefficients
				unsigned int wf = 255 * af;
				unsigned int wb = (255 - af) * ab;
				unsigned int wTotal = wf + wb;

				unsigned int r = (((fg >>  0) & 0xFF) * wf + ((bg >>  0) & 0xFF) * wb) / wTotal;
				unsigned int g = (((fg >>  8) & 0xFF) * wf + ((bg >>  8) & 0xFF) * wb) / wTotal;
				unsigned int b = (((fg >> 16) & 0xFF) * wf + ((bg >> 16) & 0xFF) * wb) / wTotal;
				unsigned int a = wTotal / 255;
				out[x + y * width] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
			}
		}
	}
	return out;
}
