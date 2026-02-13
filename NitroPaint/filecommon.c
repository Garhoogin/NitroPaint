#include "filecommon.h"
#include "nclr.h"
#include "ncgr.h"
#include "nscr.h"
#include "ncer.h"
#include "nsbtx.h"
#include "nanr.h"
#include "nftr.h"
#include "texture.h"
#include "gdip.h"
#include "nns.h"
#include "jlyt.h"
#include "mesg.h"

LPCWSTR g_ObjCompressionNames[] = {
	L"None", 
	L"LZ77", 
	L"LZ11", 
	L"LZ11 COMP", 
	L"Huffman 4", 
	L"Huffman 8", 
	L"RLE",
	L"Diff 8",
	L"Diff 16", 
	L"LZ77 Header", 
	L"MvDK", 
	L"VLX",
	L"ASH",
	NULL
};

static LPCWSTR sCommonPaletteEndings[] = {
	L"ncl.bin",
	L"icl.bin",
	L"plt.bin",
	L"pal.bin",
	L".pltt",
	L".nbfp",    // ntexconv use
	L".icl",     // IS-IRIS-CHARACTER use
	L".acl",     // IS-AGB-CHARACTER use
	L".plb",     // iMageStudio use
	L".pal",     // Homebrew use
	NULL
};

static LPCWSTR sCommonCharacterEndings[] = {
	L"ncg.bin",
	L"icg.bin",
	L"chr.bin",
	L".char",
	L".nbfc",    // ntexconv use
	L".icg",     // IS-IRIS-CHARACTER use
	L".imb",     // iMageStudio use
	L".img",     // Homebrew use
	L".img.bin", // Homebrew use
	NULL
};

static LPCWSTR sCommonScreenEndings[] = {
	L"nsc.bin",
	L"isc.bin",
	L"scr.bin",
	L".nbfs",    // ntexconv use
	L".isc",     // IS-IRIS-CHARACTER use
	L".map",     // Homebrew use
	L".map.bin", // Homebrew use
	NULL
};

LPCWSTR ObjStatusToString(int status) {
	switch (status) {
		case OBJ_STATUS_SUCCESS:
			return L"Success";
		case OBJ_STATUS_INVALID:
			return L"Invalid";
		case OBJ_STATUS_MISMATCH:
			return L"Mismatch";
		case OBJ_STATUS_NO_ACCESS:
			return L"Access denied";
		case OBJ_STATUS_NO_MEMORY:
			return L"Out of memory";
		case OBJ_STATUS_UNSUPPORTED:
			return L"Unsupported";
	}

	return NULL;
}

static int ObjiPathEndsWith(LPCWSTR str, LPCWSTR substr) {
	if (wcslen(substr) > wcslen(str)) return 0;
	LPCWSTR str1 = str + wcslen(str) - wcslen(substr);
	return !_wcsicmp(str1, substr);
}

static int ObjiPathStartsWith(LPCWSTR str, LPCWSTR substr) {
	if (wcslen(substr) > wcslen(str)) return 1;
	return !_wcsnicmp(str, substr, wcslen(substr));
}

static int ObjiPathEndsWithOneOf(LPCWSTR str, LPCWSTR *endings) {
	while (*endings) {
		if (ObjiPathEndsWith(str, *endings)) return 1;
		endings++;
	}
	return 0;
}

LPWSTR ObjGetFileNameFromPath(LPCWSTR path) {
	LPWSTR lastF = wcsrchr(path, L'/');
	LPWSTR lastB = wcsrchr(path, L'\\');
	if (lastF == NULL && lastB != NULL) return lastB + 1;
	if (lastB == NULL && lastF != NULL) return lastF + 1;
	if (lastF == NULL && lastB == NULL) return (LPWSTR) path;
	if (lastF > lastB) return lastF + 1;
	return lastB + 1;
}

void ObjInit(OBJECT_HEADER *header, int type, int format) {
	int size = header->size; //restore this
	memset(header, 0, size);
	header->type = type;
	header->format = format;
	header->compression = COMPRESSION_NONE;
	header->size = size;
	header->link.to = NULL;
	StListCreateInline(&header->link.from, OBJECT_HEADER *, NULL);
}

int ObjIsValid(OBJECT_HEADER *obj) {
	return obj->size != 0;
}

static int ObjiIsValidImage(const unsigned char *buffer, unsigned int size) {
	int width, height;
	COLOR32 *px = ImgReadMem(buffer, size, &width, &height);
	
	//no pixel buffer -> invalid image
	if (px == NULL) return 0;
	free(px);

	//either dimension is zero -> invalid image
	if (width == 0 && height == 0) return 0;

	return 1;
}



static StMap sObjRegisteredTypes = { 0 };
static StList sObjRegisteredFormats = { 0 };

const wchar_t *ObjGetFormatNameByType(int type, int format) {
	for (size_t i = 0; i < sObjRegisteredFormats.length; i++) {
		ObjIdEntry *ent = StListGetPtr(&sObjRegisteredFormats, i);
		if (ent->type == type && ent->format == format) return ent->name;
	}

	return NULL;
}

unsigned int ObjGetFormatCountByType(int type) {
	unsigned int n = 0;

	for (size_t i = 0; i < sObjRegisteredFormats.length; i++) {
		ObjIdEntry *ent = StListGetPtr(&sObjRegisteredFormats, i);
		if (ent->format != type) continue;

		n++;
	}
	return n + 1;
}

static int ObjiIdFlagToConfidence(ObjIdFlag flag) {
	int confidence = 0;

	//headers are strong indicators of ability to identify. Footers may be strong, but not as much
	//so since they must be scanned for. If a file had both header and footer, this is stronger.
	if (flag & OBJ_ID_HEADER) confidence += 100;
	if (flag & OBJ_ID_FOOTER) confidence += 50;

	//a recognizable signature is a very good confidene indictator.
	if (flag & OBJ_ID_SIGNATURE) confidence += 400;

	//a block structure enforces restrictions on the file data.
	if (flag & OBJ_ID_CHUNKED) confidence += 100;

	//a file including a checksum should catch lots of incorrect detections.
	if (flag & OBJ_ID_CHECKSUM) confidence += 200;

	//inclusion of compressed data sections enforces stricter detection.
	if (flag & OBJ_ID_COMPRESSION) confidence += 50;

	//For a file with specific valid sizes, we can increase the confidence slightly.
	if (flag & OBJ_ID_SIZE_CHECK) confidence += 25;

	//A Windows codec takes on precedence just under a file with header and signature.
	if (flag & (OBJ_ID_WINCODEC | OBJ_ID_WINCODEC_OVERRIDE)) confidence += 475;

	return confidence;
}

static int ObjiIdEntryComparator(const void *e1, const void *e2) {
	//descending order of confidence
	const ObjIdEntry *ent1 = (const ObjIdEntry *) e1;
	const ObjIdEntry *ent2 = (const ObjIdEntry *) e2;
	return ObjiIdFlagToConfidence(ent1->idFlag) - ObjiIdFlagToConfidence(ent2->idFlag);
}

void ObjInitCommon(void) {
	StMapCreate(&sObjRegisteredTypes, sizeof(int), sizeof(ObjTypeEntry));
	StListCreate(&sObjRegisteredFormats, sizeof(ObjIdEntry), ObjiIdEntryComparator);

	//register default formats
	ObjRegisterType(FILE_TYPE_INVALID, 0, L"Invalid");
	ObjRegisterType(FILE_TYPE_IMAGE, 0, L"Image");
	ObjRegisterFormat(FILE_TYPE_IMAGE, 1, L"", OBJ_ID_WINCODEC, ObjiIsValidImage);
}

void ObjRegisterType(int type, size_t objSize, const wchar_t *name) {
	ObjTypeEntry ent;
	ent.name = _wcsdup(name);
	ent.size = objSize;
	StMapPut(&sObjRegisteredTypes, &type, &ent);
}

void ObjRegisterFormat(int type, int format, const wchar_t *name, ObjIdFlag flag, ObjIdProc proc) {
	ObjIdEntry ent;
	ent.type = type;
	ent.format = format;
	ent.name = _wcsdup(name);
	ent.idFlag = flag;
	ent.idProc = proc;
	StListAdd(&sObjRegisteredFormats, &ent);
}

int ObjIdentifyExByType(const unsigned char *buffer, unsigned int size, int type, int *pFormat) {
	int format = 0;
	int detectType = FILE_TYPE_INVALID;

	for (size_t i = 0; i < sObjRegisteredFormats.length; i++) {
		ObjIdEntry *pEnt = StListGetPtr(&sObjRegisteredFormats, i);
		if (pEnt->type != type && type != FILE_TYPE_INVALID) continue;

		//check validity
		if (pEnt->idProc(buffer, size)) {
			//match
			detectType = pEnt->type;
			format = pEnt->format;
			break;
		}
	}
	
	if (pFormat != NULL) *pFormat = format;
	return detectType;
}

int ObjIdentifyEx(const unsigned char *buffer, unsigned int size, int *pFormat) {
	return ObjIdentifyExByType(buffer, size, FILE_TYPE_INVALID, pFormat);
}



int ObjiScreenCharComparator(const void *v1, const void *v2) {
	uint16_t u1 = *(const uint16_t *) v1;
	uint16_t u2 = *(const uint16_t *) v2;
	return ((int) u1) - ((int) u2);
}

//
// Attempt to guess if a file is a palette, character graphics, or screen.
//
static int ObjiGuessPltChrScr(const unsigned char *ptr, unsigned int size) {
	int canBePalette = 1, canBeChar = 1, canBeScreen = 1;
	int n16 = size / 2;

	if (size & 1 || size > 32768) canBePalette = 0;
	if (size & 0x1F || !size) canBeChar = 0;
	if (!ScrIsValidBin(ptr, size)) canBeScreen = 0;

	if(canBePalette) {
		COLOR *asColors = (COLOR *) ptr;
		for (int i = 0; i < n16; i++) {
			if (asColors[i] & 0x8000) {
				canBePalette = 0;
				break;
			}
		}
	}

	if (!canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_INVALID;
	if (canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_PALETTE;
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;
	if (!canBePalette && !canBeChar && canBeScreen) return FILE_TYPE_SCREEN;

	//Use some heuristics. Check the flip bytes for screen.
	if (canBeScreen) {
		unsigned int nFlipped = 0;
		uint16_t *screen = (uint16_t *) ptr;
		for (int i = 0; i < n16; i++) {
			int f = (screen[i] >> 10) & 3;
			if (f) nFlipped++;
		}
		if (nFlipped <= (size / 2) / 4) {
			canBeScreen++;
		}
		if (nFlipped > (size / 2) / 2) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
	}

	//double check valid returns
	if (canBePalette && !canBeChar && !canBeScreen) return FILE_TYPE_PALETTE;
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;

	//heuristics for palettes
	if (canBePalette) {
		//
	}

	//Try a different heuristic for screens. Check number of tiles with a
	//character index greater than the previous one.
	if (canBeScreen) {
		uint16_t *screen = (uint16_t *) ptr;
		int lastChar = -1, nIncreasing = 0;
		for (int i = 0; i < n16; i++) {
			int c = screen[i] & 0x3FF;
			if (c >= lastChar) {
				nIncreasing++;
			}
			lastChar = c;
		}

		if (nIncreasing * 2 < n16) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
		if (nIncreasing * 4 >= 3 * n16) {
			canBeScreen++;
		}
	}

	//Try another heuristic for screens. Find min and max char, and check total usage.
	if (canBeScreen) {
		uint16_t *screen = (uint16_t *) ptr;

		uint16_t *sorted = (uint16_t *) calloc(size, 1);
		memcpy(sorted, ptr, size);
		qsort(sorted, size / 2, 2, ObjiScreenCharComparator);
		int minChar = sorted[0] & 0x3FF;
		int maxChar = sorted[n16 - 1] & 0x3FF;
		int charRange = maxChar - minChar + 1;

		int lastChar = minChar - 1;
		int nCounted = 0;
		for (int i = 0; i < n16; i++) {
			int chr = sorted[i] & 0x3FF;

			if (chr != lastChar) {
				nCounted++;
			}
			lastChar = chr;
		}

		//shoot for >=25%
		if (nCounted * 4 < charRange) {
			canBeScreen--;
			if (canBeScreen < 0) canBeScreen = 0;
		}
		if (nCounted * 4 >= 3 * charRange) {
			canBeScreen++;
		}
		free(sorted);
	}

	//double check valid returns
	if (!canBePalette && canBeChar && !canBeScreen) return FILE_TYPE_CHARACTER;
	if (!canBePalette && !canBeChar && canBeScreen) return FILE_TYPE_SCREEN;

	//heuristics for character
	if (canBeChar) {
		//check for identical runs of nonzero bytes
		int runLength = 0;
		for (unsigned int i = 0; i < size; i++) {
			unsigned char c = ptr[i];
			if (!c) continue;

			int len = 1;
			for (; i < size; i++) {
				unsigned char c2 = ptr[i];
				if (c2 == c) len++;
				else break;
			}
			if (len > runLength) runLength = len;
		}
		if (runLength < 0x10) {
			canBeChar--;
			if (canBeChar < 0) canBeChar = 0;
		}
		if (runLength >= 0x18) {
			canBeChar++;
		}
	}

	//final judgement
	if (canBePalette > canBeScreen && canBePalette > canBeChar) return FILE_TYPE_PALETTE;
	if (canBeScreen > canBePalette && canBeScreen > canBeChar) return FILE_TYPE_SCREEN;
	if (canBeChar > canBePalette && canBeChar > canBeScreen) return FILE_TYPE_CHAR;
	if (canBePalette) return FILE_TYPE_PALETTE;
	if (canBeScreen) return FILE_TYPE_SCREEN;
	if (canBeChar) return FILE_TYPE_CHAR;

	//when in doubt, it's character graphics
	return FILE_TYPE_CHAR;
}

int ObjIdentify(char *file, int size, LPCWSTR path) {
	char *buffer = file;
	int bufferSize = size;
	if (CxGetCompressionType(file, size) != COMPRESSION_NONE) {
		buffer = CxDecompress(file, size, &bufferSize);
	}

	//TODO:
	//  Check: NNS TGA files take precedence over TGA files
	//  Files not mis-identified as ordinary TGA due to "Windows Codec" priority boost
	//  Raw data files not misclassed due to too broad of name list
	//  Mario Party DS files might be incorrectly identified

	int type = FILE_TYPE_INVALID;

	if (path != NULL) {
		//test common suffixes for raw binary data
		if (PalIsValidBin(buffer, bufferSize) && ObjiPathEndsWithOneOf(path, sCommonPaletteEndings)) type = FILE_TYPE_PALETTE;
		else if (PalIsValidNtfp(buffer, bufferSize) && ObjiPathEndsWith(path, L".ntfp")) type = FILE_TYPE_PALETTE;
		else if (ScrIsValidBin(buffer, bufferSize) && ObjiPathEndsWithOneOf(path, sCommonScreenEndings)) type = FILE_TYPE_SCREEN;
		else if (ChrIsValidBin(buffer, bufferSize) && ObjiPathEndsWithOneOf(path, sCommonCharacterEndings)) type = FILE_TYPE_CHARACTER;
	}

	if (type == FILE_TYPE_INVALID) {
		//if the file name detection rules did not trigger, try identifying the file with our formats
		type = ObjIdentifyEx(buffer, bufferSize, NULL);
	}
	
	//no matches?
	if (type == FILE_TYPE_INVALID) {
		//double check, without respect to the file name.
		type = ObjiGuessPltChrScr(buffer, bufferSize);

		//last ditch effort
		if (type == FILE_TYPE_INVALID) {
			if (PalIsValidBin(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
			else if (ScrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_SCREEN;
			else if (ChrIsValidBin(buffer, bufferSize)) type = FILE_TYPE_CHARACTER;
			else if (PalIsValidNtfp(buffer, bufferSize)) type = FILE_TYPE_PALETTE;
		}
	}

	if (buffer != file) {
		free(buffer);
	}
	return type;
}

unsigned short ObjComputeCrc16(const unsigned char *data, int length, unsigned short init) {
	unsigned short r = init;
	const unsigned short tbl[] = { 
		0x0000, 0xCC01, 0xD801, 0x1400, 
		0xF001, 0x3C00, 0x2800, 0xE401, 
		0xA001, 0x6C00, 0x7800, 0xB401, 
		0x5000, 0x9C01, 0x8801, 0x4400 
	};

	for (int i = 0; i < length; i++) {
		unsigned short c = (tbl[*data & 0xF] ^ (r >> 4)) ^ tbl[r & 0xF];
		r = (tbl[*data >> 4] ^ (c >> 4)) ^ tbl[c & 0xF];
		data++;
	}
	return r;
}

void ObjCompressFile(LPWSTR name, int compression) {
	HANDLE hFile = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwSizeLow, dwSizeHigh, dwRead, dwWritten;
	dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
	char *buffer = (char *) calloc(dwSizeLow, 1);
	ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	CloseHandle(hFile);
	int compressedSize;
	char *compressedBuffer;
	compressedBuffer = CxCompress(buffer, dwSizeLow, compression, &compressedSize);

	hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, compressedBuffer, compressedSize, &dwWritten, NULL);
	CloseHandle(hFile);
	if (compressedBuffer != buffer) free(compressedBuffer);
	free(buffer);
}

void ObjFree(OBJECT_HEADER *header) {
	//clean up object outgoing links
	OBJECT_HEADER *to = header->link.to;
	if (to != NULL) {
		ObjUnlinkObjects(to, header);
	}

	//clean up object incoming links
	while (header->link.from.length > 0) {
		OBJECT_HEADER *other;
		StListGet(&header->link.from, 0, &other);
		ObjUnlinkObjects(header, other);
	}
	StListFree(&header->link.from);

	//free strings
	if (header->fileLink != NULL) free(header->fileLink);
	if (header->comment != NULL) free(header->comment);
	header->fileLink = NULL;
	header->comment = NULL;

	//free object resources
	if (header->dispose != NULL) header->dispose(header);

	memset(header, 0, header->size);
}

void *ObjReadWholeFile(const wchar_t *name, unsigned int *size) {
	HANDLE hFile = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD dwRead, dwSizeLow, dwSizeHigh = 0;

	if (hFile == INVALID_HANDLE_VALUE) {
		*size = 0;
		return NULL;
	}

	void *buffer;
	if (ObjiPathStartsWith(name, L"\\\\.\\pipe\\")) {
		//pipe protocol: first 4 bytes file size, followed by file data.
		ReadFile(hFile, &dwSizeLow, 4, &dwRead, NULL);
		buffer = malloc(dwSizeLow);
		ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	} else {
		dwSizeLow = GetFileSize(hFile, &dwSizeHigh);
		buffer = malloc(dwSizeLow);
		ReadFile(hFile, buffer, dwSizeLow, &dwRead, NULL);
	}

	CloseHandle(hFile);
	*size = dwSizeLow;
	return buffer;
}

int ObjReadFile(LPCWSTR name, OBJECT_HEADER *object, OBJECT_READER reader) {
	unsigned int size;
	void *buffer = ObjReadWholeFile(name, &size);
	if (buffer == NULL) {
		return OBJ_STATUS_NO_ACCESS;
	}

	int status;
	int compType = CxGetCompressionType(buffer, size);
	if (compType == COMPRESSION_NONE) {
		status = reader(object, buffer, size);
	} else {
		int decompressedSize;
		void *decompressed = CxDecompress(buffer, size, &decompressedSize);
		status = reader(object, decompressed, decompressedSize);
		free(decompressed);
		object->compression = compType;
	}

	free(buffer);
	return status;
}

int ObjWrite(OBJECT_HEADER *object, BSTREAM *stream) {
	if (object->combo != NULL) {
		COMBO2D *combo = (COMBO2D *) object->combo;
		return ObjWrite(&combo->header, stream);
	} else {
		return object->writer(object, stream);
	}
}

int ObjWriteFile(OBJECT_HEADER *object, const wchar_t *name) {
	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);
	int status = ObjWrite(object, &stream);

	//to byte array (releases buffer from stream)
	unsigned int outSize;
	unsigned char *outbuf = bstreamToByteArray(&stream, &outSize);

	if (OBJ_SUCCEEDED(status)) {
		if (object->compression != COMPRESSION_NONE) {
			//compress buffer
			unsigned int compSize;
			unsigned char *comp = CxCompress(outbuf, outSize, object->compression, &compSize);
			
			//replace buffer
			free(outbuf);
			outbuf = comp;
			outSize = compSize;
		}

		DWORD dwWritten;
		HANDLE hFile = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			free(outbuf);
			return OBJ_STATUS_NO_ACCESS;
		}
		WriteFile(hFile, outbuf, outSize, &dwWritten, NULL);
		CloseHandle(hFile);
	}

	free(outbuf);

	return status;
}

void ObjLinkObjects(OBJECT_HEADER *to, OBJECT_HEADER *from) {
	//if the from object is already linking an object, unlink it
	if (from->link.to != NULL) {
		ObjUnlinkObjects(to, from);
	}

	//link both ways
	StListAdd(&to->link.from, &from);
	from->link.to = to;
}

void ObjUnlinkObjects(OBJECT_HEADER *to, OBJECT_HEADER *from) {
	//if not linked in this direction...
	if (from->link.to != to) return;

	from->link.to = NULL;

	//scan for link in to object
	for (unsigned int i = 0; i < to->link.from.length; i++) {
		OBJECT_HEADER *objI;
		StListGet(&to->link.from, i, &objI);

		if (objI == from) {
			//remove
			StListRemove(&to->link.from, i);
			return;
		}
	}
}

void ObjSetFileLink(OBJECT_HEADER *obj, const wchar_t *link) {
	if (obj->fileLink != NULL) free(obj->fileLink);

	int len = wcslen(link);
	obj->fileLink = (char *) calloc(len + 1, 1);
	for (int i = 0; i < len; i++) {
		obj->fileLink[i] = (char) link[i];
	}
}

void ObjUpdateLinks(OBJECT_HEADER *obj, const wchar_t *path) {
	for (unsigned int i = 0; i < obj->link.from.length; i++) {
		OBJECT_HEADER *linked;
		StListGet(&obj->link.from, i, &linked);
		ObjSetFileLink(linked, path);
	}
}
