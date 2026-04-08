#include <Windows.h>
#include <wincodec.h>
#include <math.h>

#include "gdip.h"

#pragma comment(lib, "windowscodecs.lib")

static void ImgiFlipBits(void *rows, unsigned int width, unsigned int height, int hFlip, int vFlip, unsigned int unitSize);

#define TGA_CMAP_NONE             0 // TGA: no color map
#define TGA_CMAP_PRESENT          1 // TGA: uses color map

#define TGA_CTYPE_NONE         0x00 // color type: no color type
#define TGA_CTYPE_CMAP         0x01 // color type: uses color map
#define TGA_CTYPE_DIRECT       0x02 // color type: direct color
#define TGA_CTYPE_GRAYSCALE    0x03 // color type: grayscale
#define TGA_CTYPE_FMT_MASK     0x03 // color format type mask
#define TGA_CTYPE_RLE          0x08 // RLE flag

void ImgIndexedImageFree(ImgIndexedImage *pIndexed) {
	free(pIndexed->bits);
	free(pIndexed->pltt);
	memset(pIndexed, 0, sizeof(ImgIndexedImage));
}

int ImgIsValidTGA(const unsigned char *buffer, unsigned int size) {
	if (size < 0x12) return 0;

	unsigned int idLength = buffer[0x00];
	int colorMapType = buffer[0x01];
	int colorType = buffer[0x02];
	int colorFormat = colorType & TGA_CTYPE_FMT_MASK;
	unsigned int colorMapStart = *(const uint16_t *) (buffer + 0x03);
	unsigned int colorMapSize = *(const uint16_t *) (buffer + 0x05);
	unsigned int colorMapDepth = buffer[0x07];
	unsigned int depth = buffer[0x10];
	unsigned int attr = buffer[0x11];
	if (idLength > (size - 0x12)) return 0;

	if (colorType & ~(TGA_CTYPE_FMT_MASK | TGA_CTYPE_RLE)) return 0;                 // unallowed format
	if (colorMapType != TGA_CMAP_NONE && colorMapType != TGA_CMAP_PRESENT) return 0; // only valid values

	if (depth != 8 && depth != 24 && depth != 32) return 0; // only 8, 24, 32 bits supported

	if (colorMapStart >= 256) return 0;                 // color map must be within 8bit
	if (colorMapSize > (256 - colorMapStart)) return 0; // color map must be within 8bit
	if (attr & 0xC7) return 0;                          // unsupported alpha depths

	switch (colorType & TGA_CTYPE_FMT_MASK) {
		case TGA_CTYPE_NONE:
			return 0; // invalid specification
		case TGA_CTYPE_CMAP:
			if (colorMapType != TGA_CMAP_PRESENT) return 0; // needs color map present
			if (colorMapDepth == 0) return 0;               // should have nonzero color map depth (?)
			if (colorMapSize == 0) return 0;                // should have a nonzero color map size
			break;
		case TGA_CTYPE_DIRECT:
			break;
		case TGA_CTYPE_GRAYSCALE:
			break;
	}
	return 1;
}

int ImgIsValidPIC(const unsigned char *buffer, unsigned int size) {
	if (size < 0x68) return 0;

	//check file magic
	static const unsigned char signature[] = { 0x53, 0x80, 0xF6, 0x34, 0x40, 0x6C, 0xCC, 0xCD };
	if (memcmp(buffer, signature, sizeof(signature)) != 0) return 0;

	//check PICT data
	const unsigned char *pict = buffer + 0x58;
	if (memcmp(pict, "PICT", 4) != 0) return 0;

	unsigned int w = (pict[0x4] << 8) | (pict[0x5] << 0);
	unsigned int h = (pict[0x6] << 8) | (pict[0x7] << 0);
	unsigned char c = pict[0xD];

	if (w == 0 || h == 0) return 0; // zero size
	if (c > 3) return 0;            // not sure
	
	unsigned char planeMasks[4] = { 0 };
	unsigned char planeFormats[4] = { 0 };
	unsigned int nPlane = 0;

	const unsigned char *planeInfo = pict + 0x10;
	for (unsigned int i = 0; i < 4; i++) {
		//check space for plane data
		if (0x58 + 0x10 + (i + 1) * 4 > size) return 0;

		unsigned char morePlanes = planeInfo[0];
		unsigned char planeBits = planeInfo[1];
		unsigned char planeComp = planeInfo[2];
		unsigned char planeMask = planeInfo[3];

		if (i == 3 && morePlanes) return 0;             // too many planes
		if (planeComp != 0 && planeComp != 2) return 0; // incorrect row format
		if (planeBits != 8) return 0;                   // unsupported data bits

		planeMasks[i] = planeMask;
		planeFormats[i] = planeComp;

		nPlane++;
		planeInfo += 4;

		if (!morePlanes) break; // last plane
	}

	//check bitmap data
	const unsigned char *data = planeInfo;
	unsigned int offset = 0, dataSize = size - 0x58 - 0x10 - 4 * nPlane;
	for (unsigned int y = 0; y < h; y++) {
		//decode row planes
		for (unsigned int plane = 0; plane < nPlane; plane++) {
			unsigned char planeMask = planeMasks[plane];

			unsigned int nPlaneChannel = 0;
			if (planeMask & 0x80) nPlaneChannel++;
			if (planeMask & 0x40) nPlaneChannel++;
			if (planeMask & 0x20) nPlaneChannel++;
			if (planeMask & 0x10) nPlaneChannel++;

			//decode row
			unsigned int x = 0;
			if (planeFormats[plane] == 0) {
				//format: direct.
				//check size
				if ((dataSize - offset) < (w * nPlaneChannel)) return 0;
				offset += w * nPlaneChannel;
			} else {
				//format: RLE row
				while (x < w) {
					if ((dataSize - offset) < 1) return 0;
					unsigned char flag = data[offset++];

					unsigned int nRep, nColRead = 1;
					if (flag & 0x80) {
						//repeat colors
						if ((flag & 0x7F) == 0) {
							//special case length
							if ((dataSize - offset) < 2) return 0;
							nRep = data[offset++] << 8;
							nRep |= data[offset++];
						} else {
							//7-bit length
							nRep = (flag & 0x7F) + 1;
						}
					} else {
						//direct run
						nRep = (flag & 0x7F) + 1;
						nColRead = nRep;
					}

					//check offset of read
					if ((dataSize - offset) < (nColRead * nPlaneChannel)) return 0;
					offset += nColRead * nPlaneChannel;

					//check X position
					if ((w - x) < nRep) return 0;
					x += nRep;
				}
			}
		}
	}

	return 1;
}

static void ImgiReadTgaDirect(COLOR32 *pixels, int width, int height, const unsigned char *buffer, int depth, int rle, ImgIndexedImage *pIndexed) {
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

	//output indexed data (does not exist)
	if (pIndexed != NULL) {
		memset(pIndexed, 0, sizeof(ImgIndexedImage));
	}
}

static void ImgiReadTgaIndexedCommon(COLOR32 *px, unsigned int width, unsigned int height, const unsigned char *buffer, const COLOR32 *palette, unsigned int nColors, int rle, ImgIndexedImage *pIndexed) {
	unsigned int nPx = width * height;

	unsigned char *indexed = (unsigned char *) calloc(nPx, 1);

	if (!rle) {
		//read pixel colors from palette
		for (unsigned int i = 0; i < nPx; i++) {
			indexed[i] = buffer[i];
		}
	} else {
		//read RLE
		unsigned int nPixelsRead = 0, offset = 0;
		while (nPixelsRead < nPx) {
			COLOR32 col = 0;
			unsigned char b = buffer[offset++];
			unsigned int num = (b & 0x7F) + 1, rlFlag = b & 0x80;

			//process run of pixels
			for (unsigned int i = 0; i < num; i++) {
				//write and increment
				indexed[nPixelsRead + i] = buffer[offset];
				if (!rlFlag) offset++;
			}

			if (rlFlag) offset++;
			nPixelsRead += num;
		}
	}

	//map colors
	for (unsigned int i = 0; i < nPx; i++) {
		unsigned int index = indexed[i];
		if (index < nColors) px[i] = palette[indexed[i]];
		else px[i] = 0;
	}

	//output indexed image data
	if (pIndexed != NULL) {
		pIndexed->bits = indexed;
		pIndexed->width = width;
		pIndexed->height = height;
		pIndexed->nPltt = nColors;
		pIndexed->pltt = (COLOR32 *) calloc(nColors, sizeof(COLOR32));
		memcpy(pIndexed->pltt, palette, nColors * sizeof(COLOR32));
	} else {
		memset(pIndexed, 0, sizeof(ImgIndexedImage));
		free(indexed);
	}
}

static void ImgiReadTgaMapped(COLOR32 *px, unsigned int width, unsigned int height, const unsigned char *buffer, int tableBase, int tableSize, int tableDepth, int rle, ImgIndexedImage *pIndexed) {
	COLOR32 palette[256] = { 0 };

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
	ImgiReadTgaIndexedCommon(px, width, height, buffer, palette, tableSize, rle, pIndexed);
}

static void ImgiReadTgaGrayscale(COLOR32 *px, unsigned int width, unsigned int height, const unsigned char *buffer, int rle, ImgIndexedImage *pIndexed) {
	COLOR32 palette[256];
	for (unsigned int i = 0; i < 256; i++) {
		palette[i] = 0xFF000000 | (i << 0) | (i << 8) | (i << 16);
	}

	ImgiReadTgaIndexedCommon(px, width, height, buffer, palette, 256, rle, pIndexed);
}

static COLOR32 *ImgiReadTga(const unsigned char *buffer, unsigned int dwSize, unsigned int *pWidth, unsigned int *pHeight, ImgIndexedImage *pIndexed) {
	int dataOffset = buffer[0x00] + 0x12;
	int colorType = buffer[0x02];
	int depth = buffer[0x10] >> 3;
	int attr = buffer[0x11];
	int colorTableBase = *(uint16_t *) (buffer + 0x03);
	int colorTableLength = *(uint16_t *) (buffer + 0x05);
	int colorTableDepth = buffer[0x07] >> 3;
	int width = *(uint16_t *) (buffer + 0x0C);
	int height = *(uint16_t *) (buffer + 0x0E);
	int colorFormat = colorType & TGA_CTYPE_FMT_MASK;

	*pWidth = width;
	*pHeight = height;
	buffer += dataOffset;

	int needsVFlip = !(attr & 0x20);  // flipped by default, we interpret this backwards for convenience
	int needsHFlip = !!(attr & 0x10); // actual H flip
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	switch (colorFormat) {
		case TGA_CTYPE_DIRECT:
			ImgiReadTgaDirect(pixels, width, height, buffer, depth, colorType & TGA_CTYPE_RLE, pIndexed);
			break;
		case TGA_CTYPE_CMAP:
			ImgiReadTgaMapped(pixels, width, height, buffer, colorTableBase, colorTableLength, colorTableDepth, colorType & TGA_CTYPE_RLE, pIndexed);
			break;
		case TGA_CTYPE_GRAYSCALE: //unsupported
			ImgiReadTgaGrayscale(pixels, width, height, buffer, colorType & TGA_CTYPE_RLE, pIndexed);
			break;
	}

	//perform necessary flips
	ImgFlip(pixels, width, height, needsHFlip, needsVFlip);
	if (pIndexed != NULL && pIndexed->bits != NULL) {
		//flip the indexed bits
		ImgiFlipBits(pIndexed->bits, width, height, needsHFlip, needsVFlip, 1);
	}
	return pixels;
}

static COLOR32 *ImgiReadPic(const unsigned char *buffer, unsigned int size, unsigned int *pWidth, unsigned int *pHeight, ImgIndexedImage *pIndexed) {
	//check PICT data
	const unsigned char *pict = buffer + 0x58;

	unsigned int w = (pict[0x4] << 8) | (pict[0x5] << 0);
	unsigned int h = (pict[0x6] << 8) | (pict[0x7] << 0);
	const unsigned char *data = pict + 0x10;

	unsigned char planeMasks[4] = { 0 };
	unsigned char planeFormats[4] = { 0 };
	unsigned int nPlane = 0;

	//decode plane info
	for (unsigned int i = 0; i < 4; i++) {
		unsigned char morePlanes = data[0];
		unsigned char format = data[2];
		unsigned char mask = data[3];
		nPlane++;

		planeFormats[i] = format;
		planeMasks[i] = mask;

		data += 4;

		if (!morePlanes) break;
	}

	*pWidth = w;
	*pHeight = h;
	COLOR32 *px = (COLOR32 *) calloc(w * h, sizeof(COLOR32));

	//fill default pixel values
	unsigned int nPx = w * h;
	for (unsigned int i = 0; i < nPx; i++) {
		px[i] = 0xFF000000;
	}

	//decode bitmap scans
	unsigned int offset = 0;
	for (unsigned int y = 0; y < h; y++) {
		COLOR32 *pRow = px + y * w;
		
		//decode row planes
		for (unsigned int plane = 0; plane < nPlane; plane++) {
			unsigned char planeMask = planeMasks[plane];

			COLOR32 planeColorMask = 0;
			if (planeMask & 0x10) planeColorMask |= 0xFF000000;
			if (planeMask & 0x20) planeColorMask |= 0x00FF0000;
			if (planeMask & 0x40) planeColorMask |= 0x0000FF00;
			if (planeMask & 0x80) planeColorMask |= 0x000000FF;

			//decode row
			unsigned int x = 0;
			while (x < w) {
				if (planeFormats[plane] == 2) {
					//format: RLE row
					unsigned char flag = data[offset++];

					if (flag & 0x80) {
						//repeat
						unsigned int nRep;
						if ((flag & 0x7F) == 0) {
							//special case length
							nRep = data[offset++] << 8;
							nRep |= data[offset++];
						} else {
							//7-bit length
							nRep = (flag & 0x7F) + 1;
						}

						//decode the channels that exist
						unsigned char rgb[4] = { 0 };
						if (planeMask & 0x80) rgb[0] = data[offset++];
						if (planeMask & 0x40) rgb[1] = data[offset++];
						if (planeMask & 0x20) rgb[2] = data[offset++];
						if (planeMask & 0x10) rgb[3] = data[offset++];

						for (unsigned int i = 0; i < nRep; i++) {
							pRow[x] = (pRow[x] & ~planeColorMask) | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16) | (rgb[3] << 24);
							x++;
						}
					} else {
						//direct run
						unsigned int nRep = (flag & 0x7F) + 1;
						for (unsigned int i = 0; i < nRep; i++) {

							//decode the channels that exist
							unsigned char rgb[4] = { 0 };
							if (planeMask & 0x80) rgb[0] = data[offset++];
							if (planeMask & 0x40) rgb[1] = data[offset++];
							if (planeMask & 0x20) rgb[2] = data[offset++];
							if (planeMask & 0x10) rgb[3] = data[offset++];

							pRow[x] = (pRow[x] & ~planeColorMask) | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16) | (rgb[3] << 24);
							x++;
						}
					}
				} else {
					//format: direct

					//decode the channels that exist
					unsigned char rgb[4] = { 0 };
					if (planeMask & 0x80) rgb[0] = data[offset++];
					if (planeMask & 0x40) rgb[1] = data[offset++];
					if (planeMask & 0x20) rgb[2] = data[offset++];
					if (planeMask & 0x10) rgb[3] = data[offset++];

					pRow[x] = (pRow[x] & ~planeColorMask) | (rgb[0] << 0) | (rgb[1] << 8) | (rgb[2] << 16) | (rgb[3] << 24);
					x++;
				}
			}
		}
	}

	//return indexed image data (does not exist)
	if (pIndexed != NULL) {
		memset(pIndexed, 0, sizeof(ImgIndexedImage));
	}
	return px;
}

#define CHECK_RESULT(x) if(!SUCCEEDED(x)) goto cleanup

static HRESULT ImgiWrite(const void *scan0, WICPixelFormatGUID *format, unsigned int width, unsigned int height, unsigned int stride, unsigned int scan0Size, const COLOR32 *palette, int paletteSize, void **pBuffer, unsigned int *pBufferSize) {
	//WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
	IWICImagingFactory *factory = NULL;
	IStream *stream = NULL;
	IWICPalette *wicPalette = NULL;
	IWICBitmapEncoder *encoder = NULL;
	IWICBitmapFrameEncode *frameEncode = NULL;
	WICColor *pWicColors = NULL;

	//get factory instance
	HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &factory);
	CHECK_RESULT(result);

	//create stream
	result = CreateStreamOnHGlobal(NULL, TRUE, &stream);
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
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->WritePixels(frameEncode, height, stride, scan0Size, (void *) scan0));

	//flush data
	SUCCEEDED(result) && (result = frameEncode->lpVtbl->Commit(frameEncode));
	SUCCEEDED(result) && (result = encoder->lpVtbl->Commit(encoder));
	SUCCEEDED(result) && (result = stream->lpVtbl->Commit(stream, STGC_DEFAULT));

	//data
	if (SUCCEEDED(result)) {
		HGLOBAL hGlobal = NULL;
		STATSTG stats = { 0 };

		SUCCEEDED(result) && (result = GetHGlobalFromStream(stream, &hGlobal));
		SUCCEEDED(result) && (result = stream->lpVtbl->Stat(stream, &stats, STATFLAG_NONAME));

		void *src = GlobalLock(hGlobal);

		*pBufferSize = stats.cbSize.LowPart;
		*pBuffer = malloc(*pBufferSize);
		memcpy(*pBuffer, src, *pBufferSize);

		GlobalUnlock(hGlobal);
	}

cleanup:
	if (stream != NULL) stream->lpVtbl->Release(stream);
	if (frameEncode != NULL) frameEncode->lpVtbl->Release(frameEncode);
	if (encoder != NULL) encoder->lpVtbl->Release(encoder);
	if (wicPalette != NULL) wicPalette->lpVtbl->Release(wicPalette);
	if (factory != NULL) factory->lpVtbl->Release(factory);
	if (pWicColors != NULL) free(pWicColors);

	if (!SUCCEEDED(result)) {
		*pBuffer = NULL;
		*pBufferSize = 0;
	}
	return result;
}

static HRESULT ImgiWriteFile(LPCWSTR path, const void *scan0, WICPixelFormatGUID *format, unsigned int width, unsigned int height, unsigned int stride, unsigned int scan0Size, const COLOR32 *palette, int paletteSize) {
	void *buffer;
	unsigned int size;
	HRESULT hr = ImgiWrite(scan0, format, width, height, stride, scan0Size, palette, paletteSize, &buffer, &size);
	if (!SUCCEEDED(hr)) {
		return hr;
	}

	HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		free(buffer);
		return E_OUTOFMEMORY;
	}

	DWORD dwWritten;
	WriteFile(hFile, buffer, size, &dwWritten, NULL);
	CloseHandle(hFile);

	free(buffer);
	return hr;
}

HRESULT ImgWriteAnimatedGif(LPCWSTR path, const COLOR32 *const *pFrames, unsigned int width, unsigned int height, const int *pDurations, int nFrames) {
	WICPixelFormatGUID fmtSrc = GUID_WICPixelFormat32bppRGBA, fmtDst = GUID_WICPixelFormat8bppIndexed;
	IWICImagingFactory *factory = NULL;
	IWICBitmapEncoder *encoder = NULL;
	IWICStream *stream = NULL;
	IWICMetadataQueryWriter *encMdWriter = NULL;
	HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &factory);
	CHECK_RESULT(result);

	result = factory->lpVtbl->CreateStream(factory, &stream);
	CHECK_RESULT(result);

	result = stream->lpVtbl->InitializeFromFilename(stream, path, GENERIC_WRITE);
	CHECK_RESULT(result);

	//create bitmap encoder
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatGif, NULL, &encoder));
	SUCCEEDED(result) && (result = encoder->lpVtbl->Initialize(encoder, (IStream *) stream, WICBitmapEncoderNoCache));

	//setup metadata attributes
	static UCHAR applicationStr[] = "NETSCAPE2.0";
	PROPVARIANT pvApplication = { 0 };
	pvApplication.vt = VT_UI1 | VT_VECTOR;
	pvApplication.caub.cElems = sizeof(applicationStr) - 1; // no null terminator
	pvApplication.caub.pElems = applicationStr;

	UCHAR gifInfo[5] = { 3, 1, 0, 0, 0 };
	PROPVARIANT pvData = { 0 };
	pvData.vt = VT_UI1 | VT_VECTOR;
	pvData.caub.cElems = sizeof(gifInfo);
	pvData.caub.pElems = gifInfo;

	//write metadata
	SUCCEEDED(result) && (result = encoder->lpVtbl->GetMetadataQueryWriter(encoder, &encMdWriter));
	SUCCEEDED(result) && (result = encMdWriter->lpVtbl->SetMetadataByName(encMdWriter, L"/appext/Application", &pvApplication));
	SUCCEEDED(result) && (result = encMdWriter->lpVtbl->SetMetadataByName(encMdWriter, L"/appext/Data", &pvData));
	SUCCEEDED(result) && (result = encMdWriter->lpVtbl->Release(encMdWriter));

	int delayBias = 0;
	for (int i = 0; i < nFrames; i++) {
		IWICBitmapFrameEncode *frameEncode = NULL;
		IWICFormatConverter *converter = NULL;
		IWICPalette *plt = NULL;
		IWICBitmap *pInBmp = NULL;
		IWICMetadataQueryWriter *frameMdWriter = NULL;

		//compute effective delay diffusing errors
		int targetDelay = pDurations[i] + delayBias;
		int effectiveDelay = (targetDelay + 5) / 10 * 10; // round to nearest multiple of 10ms
		if (effectiveDelay < 10) effectiveDelay = 10;     // min frame delay (10ms)
		delayBias = targetDelay - effectiveDelay;

		PROPVARIANT pvDelay = { 0 };
		pvDelay.vt = VT_UI2;
		pvDelay.uiVal = (uint16_t) (effectiveDelay / 10);

		PROPVARIANT pvDisposal = { 0 };
		pvDisposal.vt = VT_UI1;
		pvDisposal.uintVal = 2; // for transparent area overwrite

		PROPVARIANT pvTransparency = { 0 };
		pvTransparency.vt = VT_BOOL;
		pvTransparency.boolVal = VARIANT_TRUE;

		//create bitmap from frame pixels
		SUCCEEDED(result) && (result = factory->lpVtbl->CreateBitmapFromMemory(factory, width, height, &fmtSrc, width * sizeof(COLOR32),
			height * width * sizeof(COLOR32), (BYTE *) pFrames[i], &pInBmp));

		//create palette from bitmap
		SUCCEEDED(result) && (result = factory->lpVtbl->CreatePalette(factory, &plt));
		SUCCEEDED(result) && (result = plt->lpVtbl->InitializeFromBitmap(plt, (IWICBitmapSource *) pInBmp, 256, TRUE));

		//convert bitmap to 8-bit indexed
		SUCCEEDED(result) && (result = factory->lpVtbl->CreateFormatConverter(factory, &converter));
		SUCCEEDED(result) && (result = converter->lpVtbl->Initialize(converter, (IWICBitmapSource *) pInBmp, &fmtDst,
			WICBitmapDitherTypeNone, plt, 0.0, WICBitmapPaletteTypeCustom));
		
		//write frame to encoder
		SUCCEEDED(result) && (result = encoder->lpVtbl->CreateNewFrame(encoder, &frameEncode, NULL));
		SUCCEEDED(result) && (result = frameEncode->lpVtbl->Initialize(frameEncode, NULL));
		SUCCEEDED(result) && (result = frameEncode->lpVtbl->GetMetadataQueryWriter(frameEncode, &frameMdWriter));
		SUCCEEDED(result) && (result = frameMdWriter->lpVtbl->SetMetadataByName(frameMdWriter, L"/grctlext/Delay", &pvDelay));
		SUCCEEDED(result) && (result = frameMdWriter->lpVtbl->SetMetadataByName(frameMdWriter, L"/grctlext/Disposal", &pvDisposal));
		SUCCEEDED(result) && (result = frameMdWriter->lpVtbl->SetMetadataByName(frameMdWriter, L"/grctlext/TransparencyFLag", &pvTransparency));
		SUCCEEDED(result) && (result = frameEncode->lpVtbl->WriteSource(frameEncode, (IWICBitmapSource *) converter, NULL));
		SUCCEEDED(result) && (result = frameEncode->lpVtbl->Commit(frameEncode));
		
		if (frameMdWriter != NULL) frameMdWriter->lpVtbl->Release(frameMdWriter);
		if (pInBmp != NULL) pInBmp->lpVtbl->Release(pInBmp);
		if (converter != NULL) converter->lpVtbl->Release(converter);
		if (plt != NULL) plt->lpVtbl->Release(plt);
		if (frameEncode != NULL) frameEncode->lpVtbl->Release(frameEncode);
		CHECK_RESULT(result);
	}

	//commit frames to file
	SUCCEEDED(result) && (result = encoder->lpVtbl->Commit(encoder));
	SUCCEEDED(result) && (result = stream->lpVtbl->Commit(stream, STGC_DEFAULT));


cleanup:
	if (stream != NULL) stream->lpVtbl->Release(stream);
	if (encoder != NULL) encoder->lpVtbl->Release(encoder);
	if (factory != NULL) factory->lpVtbl->Release(factory);
	return result;
}

static HRESULT ImgiRead(const void *buffer, DWORD size, COLOR32 **ppPixels, unsigned int *pWidth, unsigned int *pHeight, ImgIndexedImage *pIndexed) {
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

	//zero the indexed image return
	if (pIndexed != NULL) {
		memset(pIndexed, 0, sizeof(ImgIndexedImage));
	}

	//get factory instance
	HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, &factory);
	CHECK_RESULT(result);

	//create stream
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateStream(factory, &stream));
	SUCCEEDED(result) && (result = stream->lpVtbl->InitializeFromMemory(stream, (WICInProcPointer) buffer, size));
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
	if (pIndexed != NULL && SUCCEEDED(frame->lpVtbl->CopyPalette(frame, wicPalette))) {
		UINT paletteSize;
		result = wicPalette->lpVtbl->GetColorCount(wicPalette, &paletteSize);
		CHECK_RESULT(result);

		UINT nActualColors;
		wicPaletteColors = (WICColor *) calloc(paletteSize, sizeof(WICColor));
		result = wicPalette->lpVtbl->GetColors(wicPalette, paletteSize, wicPaletteColors, &nActualColors);
		CHECK_RESULT(result);

		//same format, will need to swap red and blue
		pIndexed->pltt = (COLOR32 *) wicPaletteColors;
		pIndexed->nPltt = paletteSize;
		for (unsigned int i = 0; i < paletteSize; i++) {
			WICColor c = wicPaletteColors[i];
			wicPaletteColors[i] = REVERSE(c);
		}
	}

	//read RGB pixel data
	memcpy(&trueColorFormat, &GUID_WICPixelFormat32bppBGRA, sizeof(trueColorFormat));
	SUCCEEDED(result) && (result = factory->lpVtbl->CreateFormatConverter(factory, &converter));
	SUCCEEDED(result) && (result = converter->lpVtbl->Initialize(converter, (IWICBitmapSource *) frame,
		&trueColorFormat, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom));
	CHECK_RESULT(result);

	//read converted pixels
	unsigned int width = *pWidth, height = *pHeight;
	unsigned int rgbStride = *pWidth * 4;
	unsigned int pxBufferSize = rgbStride * *pHeight;
	pxBuffer = (COLOR32 *) calloc(pxBufferSize, 1);
	result = converter->lpVtbl->CopyPixels(converter, NULL, rgbStride, pxBufferSize, (BYTE *) pxBuffer);

	//swap red and blue channels
	unsigned int nPx = width * height;
	for (unsigned int i = 0; i < nPx; i++) {
		COLOR32 c = pxBuffer[i];
		pxBuffer[i] = REVERSE(c);
	}
	*ppPixels = pxBuffer;

	//read index data
	if (pIndexed != NULL) {
		//get pixel format
		result = frame->lpVtbl->GetPixelFormat(frame, &pixelFormat);
		CHECK_RESULT(result);

		//check for 8bpp and 4bpp, else don't return any index data
		unsigned int depth = 0;
		if (memcmp(&pixelFormat, &GUID_WICPixelFormat8bppIndexed, sizeof(GUID)) == 0) {
			depth = 8;
		} else if (memcmp(&pixelFormat, &GUID_WICPixelFormat4bppIndexed, sizeof(GUID)) == 0) {
			depth = 4;
		} else {
			//no supported indexed data exists
		}

		//if indexed to 4bpp or 8bpp
		if (depth != 0) {
			unsigned int stride = ((width * depth + 7) / 8 + 3) & ~3;
			unsigned int scan0Size = stride * height;
			scan0 = (unsigned char *) calloc(scan0Size, 1);

			//read pixels in
			result = frame->lpVtbl->CopyPixels(frame, NULL, stride, scan0Size, scan0);
			CHECK_RESULT(result);

			indices = (unsigned char *) calloc(width * height, 1);

			//copy in rows
			for (unsigned int y = 0; y < height; y++) {
				unsigned char *rowSrc = scan0 + y * stride;
				unsigned char *rowDst = indices + y * width;

				if (depth == 8) {
					memcpy(rowDst, rowSrc, width);
				} else {
					for (unsigned int x = 0; x < width; x++) {
						rowDst[x] = (rowSrc[x / 2] >> (((x ^ 1) & 1) * 4)) & 0xF;
					}
				}
			}
			pIndexed->bits = indices;
			pIndexed->width = width;
			pIndexed->height = height;

			free(scan0);
			scan0 = NULL;
		}
	}

cleanup:
	if (converter != NULL) converter->lpVtbl->Release(converter);
	if (wicPalette != NULL) wicPalette->lpVtbl->Release(wicPalette);
	if (frame != NULL) frame->lpVtbl->Release(frame);
	if (decoder != NULL) decoder->lpVtbl->Release(decoder);
	if (stream != NULL) stream->lpVtbl->Release(stream);
	if (factory != NULL) factory->lpVtbl->Release(factory);
	if (scan0 != NULL) free(scan0);
	if (!SUCCEEDED(result)) {
		*pWidth = 0;
		*pHeight = 0;
		if (ppPixels != NULL) *ppPixels = NULL;
		if (wicPaletteColors != NULL) free(wicPaletteColors);
		if (pxBuffer != NULL) free(pxBuffer);
		if (indices != NULL) free(indices);
	}
	return result;
}

HRESULT ImgWriteMemIndexed(const unsigned char *bits, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int paletteSize, void **pBuffer, unsigned int *pSize) {
	int depth = paletteSize <= 16 ? 4 : 8;
	int stride = ((width * depth + 7) / 8 + 3) & ~3;

	//allocate and populate scan0
	int scan0Size = stride * height;
	unsigned char *scan0 = (unsigned char *) calloc(height, stride);
	for (unsigned int y = 0; y < height; y++) {
		const unsigned char *rowSrc = bits + y * width;
		unsigned char *rowDest = scan0 + y * stride;

		if (depth == 8) {
			memcpy(rowDest, bits + y * width, width);
		} else {
			for (unsigned int x = 0; x < width; x++) {
				rowDest[x / 2] |= rowSrc[x] << (((x ^ 1) & 1) * 4);
			}
		}
	}

	//create palette copy to swap red/blue order
	COLOR32 *paletteCopy = (COLOR32 *) calloc(paletteSize, 4);
	for (unsigned int i = 0; i < paletteSize; i++) {
		COLOR32 c = palette[i];
		c = (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
		paletteCopy[i] = c;
	}

	//GUID allocated on the stack since it must be writable for some reason
	WICPixelFormatGUID format;
	memcpy(&format, depth == 4 ? &GUID_WICPixelFormat4bppIndexed : &GUID_WICPixelFormat8bppIndexed, sizeof(format));
	HRESULT result = ImgiWrite(scan0, &format, width, height, stride, scan0Size, paletteCopy, paletteSize, pBuffer, pSize);
	free(scan0);
	free(paletteCopy);
	return result;
}

HRESULT ImgWriteIndexed(const unsigned char *bits, unsigned int width, unsigned int height, const COLOR32 *palette, unsigned int paletteSize, LPCWSTR path) {
	void *buffer;
	unsigned int size;
	HRESULT result = ImgWriteMemIndexed(bits, width, height, palette, paletteSize, &buffer, &size);
	if (!SUCCEEDED(result)) {
		return result;
	}

	HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		free(buffer);
		return E_OUTOFMEMORY;
	}

	DWORD dwWritten;
	WriteFile(hFile, buffer, size, &dwWritten, NULL);
	CloseHandle(hFile);

	free(buffer);
	return result;
}

HRESULT ImgWrite(const COLOR32 *px, unsigned int width, unsigned int height, LPCWSTR path) {
	COLOR32 *bits = (COLOR32 *) calloc(height, width * 4);
	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 c = px[i];
		bits[i] = REVERSE(c);
	}

	int stride = width * 4;
	WICPixelFormatGUID format;
	memcpy(&format, &GUID_WICPixelFormat32bppBGRA, sizeof(format));
	HRESULT result = ImgiWriteFile(path, bits, &format, width, height, stride, stride * height, NULL, 0);
	free(bits);
	return result;
}

COLOR32 *ImgReadMemEx(const unsigned char *buffer, unsigned int size, unsigned int *pWidth, unsigned int *pHeight, ImgIndexedImage *pIndexed) {
	COLOR32 *bits = NULL;

	//WIC doesn't support PIC or TGA format by default, so try that first.
	if (ImgIsValidPIC(buffer, size)) {
		bits = ImgiReadPic(buffer, size, pWidth, pHeight, pIndexed);
	} else if (ImgIsValidTGA(buffer, size)) {
		bits = ImgiReadTga(buffer, size, pWidth, pHeight, pIndexed);
	} else {
		HRESULT hr = ImgiRead(buffer, size, &bits, pWidth, pHeight, pIndexed);
		if (!SUCCEEDED(hr)) bits = NULL;
	}

	return bits;
}

COLOR32 *ImgReadEx(LPCWSTR lpszFileName, unsigned int *pWidth, unsigned int *pHeight, ImgIndexedImage *pIndexed) {
	//test for valid file, or TGA file, which WIC does not support.
	HANDLE hFile = CreateFile(lpszFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	DWORD dwSizeHigh, dwSizeLow, dwRead;
	dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
	BYTE *buffer = (BYTE *) malloc(dwSizeLow);
	ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	CloseHandle(hFile);

	COLOR32 *bits = ImgReadMemEx(buffer, dwSizeLow, pWidth, pHeight, pIndexed);
	free(buffer);

	return bits;
}

COLOR32 *ImgReadMem(const unsigned char *buffer, unsigned int size, unsigned int *pWidth, unsigned int *pHeight) {
	return ImgReadMemEx(buffer, size, pWidth, pHeight, NULL);
}

COLOR32 *ImgRead(LPCWSTR path, unsigned int *pWidth, unsigned int *pHeight) {
	return ImgReadEx(path, pWidth, pHeight, NULL);
}

static void ImgiFlipBits(void *rows, unsigned int width, unsigned int height, int hFlip, int vFlip, unsigned int unitSize) {
	//V flip
	if (vFlip) {
		void *rowbuf = calloc(width, unitSize);
		for (unsigned int y = 0; y < height / 2; y++) {
			void *row1 = ((unsigned char *) rows) + (y * width * unitSize);
			void *row2 = ((unsigned char *) rows) + ((height - 1 - y) * width * unitSize);

			memcpy(rowbuf, row1, width * unitSize);
			memcpy(row1, row2, width * unitSize);
			memcpy(row2, rowbuf, width * unitSize);
		}
		free(rowbuf);
	}

	//H flip
	if (hFlip) {
		unsigned char temp[4];
		for (unsigned int y = 0; y < height; y++) {
			for (unsigned int x = 0; x < width / 2; x++) {
				void *left = ((unsigned char *) rows) + (x + y * width) * unitSize;
				void *right = ((unsigned char *) rows) + (width - 1 - x + y * width) * unitSize;

				memcpy(temp, left, unitSize);
				memcpy(left, right, unitSize);
				memcpy(right, temp, unitSize);
			}
		}
	}
}

void ImgFlip(COLOR32 *px, unsigned int width, unsigned int height, int hFlip, int vFlip) {
	ImgiFlipBits(px, width, height, hFlip, vFlip, sizeof(COLOR32));
}

void ImgSwapRedBlue(COLOR32 *px, unsigned int width, unsigned int height) {
	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 c = px[i];
		px[i] = REVERSE(c);
	}
}

static int ImgiPixelComparator(const void *p1, const void *p2) {
	return *(const COLOR32 *) p1 - (*(const COLOR32 *) p2);
}

unsigned int ImgCountColors(const COLOR32 *px, unsigned int nPx) {
	return ImgCountColorsEx(px, nPx, 1, 0);
}

unsigned int ImgCountColorsEx(
	const COLOR32     *px,
	unsigned int       width,
	unsigned int       height,
	ImgCountColorsMode mode
) {
	//sort the colors by raw RGB value. This way, same colors are grouped.
	unsigned int nPx = width * height;
	COLOR32 *copy = (COLOR32 *) malloc(nPx * sizeof(COLOR32));
	memcpy(copy, px, nPx * sizeof(COLOR32));

	if (mode & IMG_CCM_IGNORE_ALPHA) {
		//when ignore alpha is set, force all pixels opaque
		for (unsigned int i = 0; i < nPx; i++) copy[i] |= 0xFF000000;
	}
	if (mode & IMG_CCM_BINARY_ALPHA) {
		//when binary alpha is set, force all alpha value to 0 or 255
		for (unsigned int i = 0; i < nPx; i++) {
			unsigned int a = copy[i] >> 24;
			if (a < 0x80) copy[i] &= ~0xFF000000;
			else          copy[i] |=  0xFF000000;
		}
	}
	if (!(mode & IMG_CCM_NO_IGNORE_TRANSPARENT_COLOR)) {
		//when we ignore tranparent pixel colors, set them all to black.
		for (unsigned int i = 0; i < nPx; i++) {
			//set transparent pixels to transparent black
			unsigned int a = copy[i] >> 24;
			if (a == 0) copy[i] = 0;
		}
	}

	//sort by raw RGBA value
	qsort(copy, nPx, sizeof(COLOR32), ImgiPixelComparator);

	unsigned int nColors = 0;
	for (unsigned int i = 0; i < nPx; i++) {
		unsigned int a = copy[i] >> 24;
		if (a == 0 && (mode & IMG_CCM_NO_COUNT_TRANSPARENT)) continue;

		//has this color come before?
		if (i == 0 || copy[i - 1] != copy[i]) nColors++;
	}
	free(copy);
	return nColors;
}

void ImgCropInPlace(const COLOR32 *px, unsigned int width, unsigned int height, COLOR32 *out, int srcX, int srcY, unsigned int srcWidth, unsigned int srcHeight) {
	//copy from px to out
	for (unsigned int y = 0; y < srcHeight; y++) {
		for (unsigned int x = 0; x < srcWidth; x++) {
			int pxX = x + srcX, pxY = y + srcY;

			if (pxX < 0 || pxX >= (int) width || pxY < 0 || pxY >= (int) height) {
				//fill with transparent for out of bounds
				out[x - srcX + y * srcWidth] = 0;
				continue;
			}

			//write pixel
			out[x + y * srcWidth] = px[pxX + pxY * width];
		}
	}
}

COLOR32 *ImgCrop(const COLOR32 *px, unsigned int width, unsigned int height, int srcX, int srcY, unsigned int srcWidth, unsigned int srcHeight) {
	COLOR32 *out = (COLOR32 *) calloc(srcWidth * srcHeight, sizeof(COLOR32));
	ImgCropInPlace(px, width, height, out, srcX, srcY, srcWidth, srcHeight);
	return out;
}

COLOR32 *ImgComposite(const COLOR32 *back, unsigned int backWidth, unsigned int backHeight, const COLOR32 *front, unsigned int frontWidth, unsigned int frontHeight, unsigned int *outWidth, unsigned int *outHeight) {
	//create output image with dimension min(<backWidth, backHeight>, <frontWidth, frontHeight>)
	unsigned int width = min(backWidth, frontWidth);
	unsigned int height = min(backHeight, frontHeight);
	*outWidth = width;
	*outHeight = height;

	COLOR32 *out = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	for (unsigned int y = 0; y < height; y++) {
		for (unsigned int x = 0; x < width; x++) {
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

unsigned char *ImgCreateAlphaMask(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int threshold, unsigned int *pRows, unsigned int *pStride) {
	unsigned int stride = ((width + 7) / 8), nRows = height;

	unsigned char *bits = (unsigned char *) calloc(stride * nRows, sizeof(unsigned char));
	for (unsigned int y = 0; y < height; y++) {
		unsigned char *row = bits + (y) * stride;
		for (unsigned int x = 0; x < width; x++) {
			unsigned char *pp = row + (x / 8);
			unsigned int bitno = (x & 7) ^ 7;

			*pp &= ~(1 << bitno);
			*pp |= (((px[x + y * width] >> 24) < threshold) << bitno);
		}
	}

	if (pRows != NULL) *pRows = nRows;
	if (pStride != NULL) *pStride = stride;
	return bits;
}

unsigned char *ImgCreateColorMask(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int *pRows, unsigned int *pStride) {
	unsigned int stride = (width * 4 + 3) & ~3, nRows = height;

	unsigned char *bits = (unsigned char *) calloc(stride * nRows, 1);
	for (unsigned int y = 0; y < height; y++) {
		unsigned char *row = bits + (y) * stride;
		for (unsigned int x = 0; x < width; x++) {
			COLOR32 c = px[x + y * width];

			row[x * 4 + 0] = (c >> 16) & 0xFF;
			row[x * 4 + 1] = (c >>  8) & 0xFF;
			row[x * 4 + 2] = (c >>  0) & 0xFF;
		}
	}

	if (pRows != NULL) *pRows = nRows;
	if (pStride != NULL) *pStride = stride;
	return bits;
}

COLOR32 *ImgScale(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int outWidth, unsigned int outHeight) {
	//alloc out
	COLOR32 *out = (COLOR32 *) calloc(outWidth * outHeight, sizeof(COLOR32));

	//trivial case: inSize == outSize
	if (width == outWidth && height == outHeight) {
		memcpy(out, px, width * height * sizeof(COLOR32));
		return out;
	}

	//0x0 input image case: render transparent bitmap
	if (width == 0 || height == 0) {
		return out;
	}

	//scale image
	for (unsigned int y = 0; y < outHeight; y++) {
		for (unsigned int x = 0; x < outWidth; x++) {
			//determine sample rectangle in source image
			unsigned int sX1N = (x + 0) * width , sX1D = outWidth;
			unsigned int sY1N = (y + 0) * height, sY1D = outHeight;
			unsigned int sX2N = (x + 1) * width , sX2D = outWidth;
			unsigned int sY2N = (y + 1) * height, sY2D = outHeight;

			//determine sample rectangle in source image
			double sX1 = ((double) sX1N) / ((double) sX1D);
			double sY1 = ((double) sY1N) / ((double) sY1D);
			double sX2 = ((double) sX2N) / ((double) sX2D);
			double sY2 = ((double) sY2N) / ((double) sY2D);

			//compute sample
			double tr = 0.0, tg = 0.0, tb = 0.0, ta = 0.0;
			double sampleArea = (sX2 - sX1) * (sY2 - sY1);

			//determine the pixel rectangle to sample. Float coordinates are between pixels, and integer
			//coordinates are in the centers of pixels.
			unsigned int sampleRectX = sX1N / sX1D;
			unsigned int sampleRectY = sY1N / sY1D;
			unsigned int sampleRectW = (sX2N + sX2D - 1) / sX2D - sampleRectX;
			unsigned int sampleRectH = (sY2N + sY2D - 1) / sY2D - sampleRectY;

			//compute rectangle trims
			double trimL = sX1 - (double) sampleRectX;
			double trimR = ((double) ((sX2N + sX2D - 1) / sX2D)) - sX2;
			double trimU = sY1 - (double) sampleRectY;
			double trimD = ((double) ((sY2N + sY2D - 1) / sY2D)) - sY2;

			for (unsigned int sy = 0; sy < sampleRectH && (sampleRectY + sy) < height; sy++) {
				double rowH = 1.0;
				if (sy == 0) rowH -= trimU;                 // trim from top
				if (sy == (sampleRectH - 1)) rowH -= trimD; // trim from bottom


				for (unsigned int sx = 0; sx < sampleRectW && (sampleRectX + sx) < width; sx++) {
					double colW = 1.0;
					if (sx == 0) colW -= trimL;                 // trim from left
					if (sx == (sampleRectW - 1)) colW -= trimR; // trim from right

					//sum colors
					COLOR32 col = px[(sampleRectX + sx) + (sampleRectY + sy) * width];
					unsigned int colA = (col >> 24);
					double weight = colW * rowH * colA;
					tr += ((col >>  0) & 0xFF) * weight;
					tg += ((col >>  8) & 0xFF) * weight;
					tb += ((col >> 16) & 0xFF) * weight;
					ta += weight;
				}
			}

			if (ta > 0) {
				tr /= ta;
				tg /= ta;
				tb /= ta;
				ta /= sampleArea;
			} else {
				tr = tg = tb = ta = 0.0;
			}

			unsigned int sampleR = (unsigned int) (int) (tr + 0.5);
			unsigned int sampleG = (unsigned int) (int) (tg + 0.5);
			unsigned int sampleB = (unsigned int) (int) (tb + 0.5);
			unsigned int sampleA = (unsigned int) (int) (ta + 0.5);
			COLOR32 sample = sampleR | (sampleG << 8) | (sampleB << 16) | (sampleA << 24);
			out[x + y * outWidth] = sample;
		}
	}

	return out;
}

COLOR32 *ImgScaleEx(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int outWidth, unsigned int outHeight, ImgScaleSetting setting) {
	if (width == 0 || height == 0) {
		//size of 0x0: fall back
		return ImgScale(px, width, height, outWidth, outHeight);
	}

	switch (setting) {
		case IMG_SCALE_FILL:
			//fill: operation is default
			return ImgScale(px, width, height, outWidth, outHeight);
		case IMG_SCALE_COVER:
		case IMG_SCALE_FIT:
		{
			//cover and fit may pad the image.
			unsigned int scaleW = outWidth, scaleH = outHeight;
			unsigned int width1 = outWidth, height1 = height * outWidth / width;
			unsigned int width2 = width * outHeight / height, height2 = outHeight;

			//cover: choose the larger of the two scales.
			//fit: choose the smaller of the two scales.
			if (setting == IMG_SCALE_COVER) {
				if (width1 > width2) scaleW = width1, scaleH = height1;
				else scaleW = width2, scaleH = height2;
			} else {
				if (width1 < width2) scaleW = width1, scaleH = height1;
				else scaleW = width2, scaleH = height2;
			}

			//scale to dimensions
			COLOR32 *scaled = ImgScale(px, width, height, scaleW, scaleH);

			//construct output image data
			COLOR32 *out = (COLOR32 *) calloc(outWidth * outHeight, sizeof(COLOR32));
			int transX = -((int) outWidth - (int) scaleW) / 2;
			int transY = -((int) outHeight - (int) scaleH) / 2;
			for (unsigned int y = 0; y < outHeight; y++) {
				for (unsigned int x = 0; x < outWidth; x++) {
					int sampleX = x + transX, sampleY = y + transY;
					if (sampleX >= 0 && sampleY >= 0 && (unsigned int) sampleX < scaleW && (unsigned int) sampleY < scaleH) {
						out[x + y * outWidth] = scaled[sampleX + sampleY * scaleW];
					}
				}
			}
			free(scaled);
			return out;
		}
	}

	//bad mode
	return NULL;
}
