#include <string.h>

#include "LZCore.h"
#include "bstream.h"



// ----- LZ77 routines

#define LZ_MIN_DISTANCE        0x01   // minimum distance per LZ encoding
#define LZ_MIN_SAFE_DISTANCE   0x02   // minimum safe distance per BIOS LZ bug
#define LZ_MAX_DISTANCE      0x1000   // maximum distance per LZ encoding
#define LZ_MIN_LENGTH          0x03   // minimum length per LZ encoding
#define LZ_MAX_LENGTH          0x12   // maximum length per LZ encoding


// length of compressed data output by LZ token
static unsigned int CxiLzTokenCost(unsigned int length) {
	unsigned int nBytesToken;
	if (length >= LZ_MIN_LENGTH) {
		nBytesToken = 2;
	} else {
		nBytesToken = 1;
	}
	return 1 + nBytesToken * 8;
}

unsigned char *CxCompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//build the LZ graph
	CxiLzNode *nodes = CxiLzGraphExplore(buffer, size, LZ_MIN_LENGTH, LZ_MAX_LENGTH, LZ_MIN_SAFE_DISTANCE, LZ_MAX_DISTANCE);
	CxiLzGraphCollapse(buffer, nodes, size, LZ_MIN_LENGTH, CxiLzTokenCost);

	//get tokens
	unsigned int nTokens;
	CxiLzToken *tokens = CxiLzGraphToTokens(buffer, nodes, size, &nTokens);
	free(nodes);

	//from here on, we have a direct path to the end of file. All we need to do is traverse it.
	//get max compressed size
	unsigned int maxCompressed = 4 + size + (size + 7) / 8;

	//encode LZ data
	unsigned char *buf = (unsigned char *) calloc(maxCompressed, 1);
	unsigned char *bufpos = buf;

	//LZ stream header
	*(bufpos++) = 0x10;
	*(bufpos++) = (size >>  0) & 0xFF;
	*(bufpos++) = (size >>  8) & 0xFF;
	*(bufpos++) = (size >> 16) & 0xFF;

	for (unsigned int i = 0; i < nTokens; i++) {
		//new grouping
		unsigned char head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int j = 0; j < 8 && i < nTokens; i++, j++) {
			CxiLzToken *tok = &tokens[i];

			if (tok->isReference) {
				//token is reference
				unsigned int length   = tok->length;
				unsigned int distance = tok->distance;

				uint16_t enc = (distance - LZ_MIN_DISTANCE) | ((length - LZ_MIN_LENGTH) << 12);
				*(bufpos++) = (enc >> 8) & 0xFF;
				*(bufpos++) = (enc >> 0) & 0xFF;
				head |= 1 << (7 - j);
			} else {
				//token is literal byte
				*(bufpos++) = tok->symbol;
			}
		}

		//put head byte
		*headpos = head;
	}
	free(tokens);

	unsigned int outSize = bufpos - buf;
	*compressedSize = outSize;
	return CxiShrink(buf, outSize); // reduce buffer size
}



unsigned char *CxDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	if (size < 4) return NULL;

	//find the length of the decompressed buffer.
	uint32_t length = (*(uint32_t *) buffer) >> 8;

	//create a buffer for the decompressed buffer
	unsigned char *result = (unsigned char *) malloc(length);
	if (result == NULL) return NULL;
	*uncompressedSize = length;

	//initialize variables
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		uint8_t head = buffer[offset];
		offset++;
		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if (dstOffset == length) return result;
			} else {
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				//length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;
				for (uint32_t j = 0; j < len; j++) {
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if (dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}



int CxIsCompressedLZ(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (*buffer != 0x10) return 0;
	uint32_t length = (*(uint32_t *) buffer) >> 8;
	if ((length / 144) * 17 + 4 > size) return 0;

	//start a dummy decompression
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		uint8_t head = buffer[offset];
		offset++;

		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				if (dstOffset >= length || offset >= size) return 0;
				dstOffset++, offset++;
				if (dstOffset == length) goto checkSize;
			} else {
				if (offset + 1 >= size) return 0;
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				//length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;

				if (dstOffset < offs) return 0;
				for (uint32_t j = 0; j < len; j++) {
					if (dstOffset >= length) return 0;
					dstOffset++;
					if (dstOffset == length) goto checkSize;
				}
			}
		}
	}

	//check the size of the remaining data
	unsigned int remaining;
checkSize:
	remaining = size - offset;
	if (remaining > 7) return 0;

	return 1;
}



// ----- LZX routines

#define LZX_MIN_DISTANCE        0x01   // minimum distance per LZX encoding
#define LZX_MIN_SAFE_DISTANCE   0x02   // minimum safe distance per BIOS LZ bug
#define LZX_MAX_DISTANCE      0x1000   // maximum distance per LZX encoding
#define LZX_MIN_LENGTH          0x03   // minimum length per LZX encoding
#define LZX_MAX_LENGTH       0x10110   // maximum length per LZX encoding
#define LZX_MIN_LENGTH_1        0x03   // size bracket 1: min length
#define LZX_MAX_LENGTH_1        0x10   // size bracket 1: max length
#define LZX_MIN_LENGTH_2        0x11   // size bracket 2: min length
#define LZX_MAX_LENGTH_2       0x110   // size bracket 2: max length
#define LZX_MIN_LENGTH_3       0x111   // size bracket 3: min length
#define LZX_MAX_LENGTH_3     0x10110   // size bracket 3: max length

// length of compressed data output by LZ token
static unsigned int CxiLzxTokenCost(unsigned int length) {
	unsigned int nBytesToken;
	if (length >= LZX_MIN_LENGTH_3) {
		nBytesToken = 4;
	} else if (length >= LZX_MIN_LENGTH_2) {
		nBytesToken = 3;
	} else if (length >= LZX_MIN_LENGTH_1) {
		nBytesToken = 2;
	} else {
		nBytesToken = 1;
	}
	return 1 + nBytesToken * 8;
}

unsigned char *CxCompressLZX(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//build the LZ graph
	CxiLzNode *nodes = CxiLzGraphExplore(buffer, size, LZX_MIN_LENGTH, LZX_MAX_LENGTH_3, LZX_MIN_SAFE_DISTANCE, LZX_MAX_DISTANCE);
	CxiLzGraphCollapse(buffer, nodes, size, LZX_MIN_LENGTH, CxiLzxTokenCost);

	unsigned int nTokens;
	CxiLzToken *tokens = CxiLzGraphToTokens(buffer, nodes, size, &nTokens);
	free(nodes);

	//from here on, we have a direct path to the end of file. All we need to do is traverse it.
	//get max compressed size
	unsigned int maxCompressed = 4 + size + (size + 7) / 8;

	//encode LZ data
	unsigned char *buf = (unsigned char *) calloc(maxCompressed, 1);
	unsigned char *bufpos = buf;

	//LZ extended stream header
	*(bufpos++) = 0x11;
	*(bufpos++) = (size >>  0) & 0xFF;
	*(bufpos++) = (size >>  8) & 0xFF;
	*(bufpos++) = (size >> 16) & 0xFF;

	for (unsigned int i = 0; i < nTokens; i++) {
		unsigned char head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int j = 0; j < 8 && i < size; i++, j++) {
			CxiLzToken *tok = &tokens[i];

			if (tok->isReference) {
				//node is reference
				unsigned int length   = tok->length;
				unsigned int distance = tok->distance;

				uint32_t enc = (distance - LZX_MIN_DISTANCE) & 0xFFF;
				if (length >= LZX_MIN_LENGTH_3) {
					enc |= ((length - LZX_MIN_LENGTH_3) << 12) | (1 << 28);
					*(bufpos++) = (enc >> 24) & 0xFF;
					*(bufpos++) = (enc >> 16) & 0xFF;
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				} else if (length >= LZX_MIN_LENGTH_2) {
					enc |= ((length - LZX_MIN_LENGTH_2) << 12) | (0 << 20);
					*(bufpos++) = (enc >> 16) & 0xFF;
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				} else if (length >= LZX_MIN_LENGTH_1) {
					enc |= ((length - LZX_MIN_LENGTH_1 + 2) << 12);
					*(bufpos++) = (enc >>  8) & 0xFF;
					*(bufpos++) = (enc >>  0) & 0xFF;
				}

				head |= 1 << (7 - j);
			} else {
				//node is literal byte
				*(bufpos++) = tok->symbol;
			}
		}

		//put head byte
		*headpos = head;
	}
	free(tokens);

	unsigned int outSize = bufpos - buf;
	*compressedSize = outSize;
	return CxiShrink(buf, outSize); // reduce buffer size
}



unsigned char *CxDecompressLZX(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	//decompress the input buffer. 
	if (size < 4) return NULL;

	//find the length of the decompressed buffer.
	uint32_t length = *(uint32_t *) (buffer) >> 8;

	//create a buffer for the decompressed buffer
	unsigned char *result = (unsigned char *) malloc(length);
	if (result == NULL) return NULL;
	*uncompressedSize = length;

	//initialize variables
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		uint8_t head = buffer[offset];
		offset++;

		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if (dstOffset == length) return result;
			} else {
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];
				uint8_t low2, low3;
				int mode = high >> 4;

				uint32_t len = 0, offs = 0;
				switch (mode) {
					case 0:
						low2 = buffer[offset++];
						len = ((high << 4) | (low >> 4)) + 0x11; //8-bit length +0x11
						offs = (((low & 0xF) << 8) | low2) + 1; //12-bit offset
						break;
					case 1:
						low2 = buffer[offset++];
						low3 = buffer[offset++];
						len = (((high & 0xF) << 12) | (low << 4) | (low2 >> 4)) + 0x111; //16-bit length +0x111
						offs = (((low2 & 0xF) << 8) | low3) + 1; //12-bit offset
						break;
					default:
						len = (high >> 4) + 1; //4-bit length +0x1 (but >= 3)
						offs = (((high & 0xF) << 8) | low) + 1; //12-bit offset
						break;
				}

				//write back
				for (uint32_t j = 0; j < len; j++) {
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if (dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}

unsigned char *CxAdvanceLZX(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (*buffer != 0x11) return 0;

	uint32_t length = (*(uint32_t *) buffer) >> 8;

	//perform a test decompression.
	uint32_t offset = 4;
	uint32_t dstOffset = 0;
	while (1) {
		if (offset >= size) return NULL;
		uint8_t head = buffer[offset];
		uint8_t origHead = head;
		offset++;

		//loop 8 times
		for (int i = 0; i < 8; i++) {
			int flag = head >> 7;
			head <<= 1;

			if (!flag) {
				if (offset >= size || dstOffset >= length) return NULL;
				dstOffset++, offset++;
				if (dstOffset == length) return (unsigned char *) (buffer + offset);
			} else {
				if (offset + 1 >= size) return NULL;
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];
				uint8_t low2, low3;
				int mode = high >> 4;

				uint32_t len = 0, offs = 0;
				switch (mode) {
					case 0:
						if (offset >= size) return NULL;
						low2 = buffer[offset++];
						len = ((high << 4) | (low >> 4)) + 0x11; // 8-bit length +0x11
						offs = (((low & 0xF) << 8) | low2) + 1;  // 12-bit offset
						break;
					case 1:
						if (offset + 1 >= size) return NULL;
						low2 = buffer[offset++];
						low3 = buffer[offset++];
						len = (((high & 0xF) << 12) | (low << 4) | (low2 >> 4)) + 0x111; // 16-bit length +0x111
						offs = (((low2 & 0xF) << 8) | low3) + 1;                         // 12-bit offset
						break;
					default:
						len = (high >> 4) + 1;                  // 4-bit length +0x1 (but >= 3)
						offs = (((high & 0xF) << 8) | low) + 1; // 12-bit offset
						break;
				}

				//test write
				if (dstOffset < offs) return NULL; // would we write before our buffer decompressing? (weird because unsigned)
				for (uint32_t j = 0; j < len; j++) {
					if (dstOffset >= length) return NULL;
					dstOffset++;
					if (dstOffset == length) return (unsigned char *) (buffer + offset);
				}
			}
		}
	}

	return (unsigned char *) (buffer + offset);
}



int CxIsCompressedLZX(const unsigned char *buffer, unsigned int size) {
	const unsigned char *end = CxAdvanceLZX(buffer, size);
	if (end == NULL) return 0;

	//allow for up to 7 bytes tail
	unsigned int complen = end - buffer;
	unsigned int leftover = size - complen;
	if (leftover <= 7) return 1;
	return 0;
}



// ----- LZ77 with header routines

unsigned char *CxCompressLZHeader(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	char *compressed = CxCompressLZ(buffer, size, compressedSize);
	if (compressed == NULL) return NULL;
	*compressedSize += 4;
	compressed = realloc(compressed, *compressedSize);
	memmove(compressed + 4, compressed, *compressedSize - 4);
	compressed[0] = 'L';
	compressed[1] = 'Z';
	compressed[2] = '7';
	compressed[3] = '7';
	return compressed;
}

unsigned char *CxDecompressLZHeader(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	if (size < 8) return NULL;
	return CxDecompressLZ(buffer + 4, size - 4, uncompressedSize);
}

int CxIsFilteredLZHeader(const unsigned char *buffer, unsigned int size) {
	//must have "LZ77" prefix
	if (size < 4 || memcmp(buffer, "LZ77", 4) != 0) return 0;
	return CxIsCompressedLZ(buffer + 4, size - 4);
}



// ------ LZX "COMP" routines

unsigned char *CxCompressLZXComp(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	uint32_t nSegments = (size + 0xFFF) / 0x1000;  //following LEGO Battles precedent
	uint32_t headerSize = 0x10 + 4 * nSegments;
	char *header = (char *) calloc(headerSize, 1);

	*(uint32_t *) (header + 0) = 'COMP';
	*(uint32_t *) (header + 4) = size;
	*(uint32_t *) (header + 8) = nSegments;

	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);
	bstreamWrite(&stream, header, headerSize); //bstreamCreate bug workaround
	free(header);

	uint32_t longestCompress = 0;
	uint32_t bytesRemaining = size;

	int i = 0;
	uint32_t offs = 0;
	while (bytesRemaining > 0) {
		unsigned thisRunLength = 0x1000;
		if (thisRunLength > bytesRemaining) thisRunLength = bytesRemaining;

		uint32_t thisRunCompressedSize;
		char *thisRunCompressed = CxCompressLZX(buffer + offs, thisRunLength, &thisRunCompressedSize);
		bstreamWrite(&stream, thisRunCompressed, thisRunCompressedSize);
		free(thisRunCompressed);

		if (thisRunCompressedSize > longestCompress) longestCompress = thisRunCompressedSize;
		bytesRemaining -= thisRunLength;

		*(uint32_t *) (stream.buffer + 0x10 + i * 4) = thisRunCompressedSize;
		offs += thisRunLength;
		i++;
	}
	*(uint32_t *) (stream.buffer + 0xC) = longestCompress;

	*compressedSize = stream.size;
	return stream.buffer;
}

unsigned char *CxDecompressLZXComp(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t totalSize = *(uint32_t *) (buffer + 0x4);
	uint32_t nSegments = *(uint32_t *) (buffer + 0x8);
	*uncompressedSize = totalSize;

	char *out = (char *) malloc(totalSize);
	uint32_t dstOffs = 0;
	uint32_t offset = 0x10 + 4 * nSegments;
	for (uint32_t i = 0; i < nSegments; i++) {

		//parse segment length & compression setting
		int segCompressed = 1;
		int32_t thisSegmentSize = *(int32_t *) (buffer + 0x10 + i * 4);
		if (thisSegmentSize < 0) {
			segCompressed = 0;
			thisSegmentSize = -thisSegmentSize;
		}

		//decompress (if applicable)
		uint32_t thisSegmentUncompressedSize;
		if (segCompressed) {
			char *thisSegment = CxDecompressLZX(buffer + offset, thisSegmentSize, &thisSegmentUncompressedSize);
			memcpy(out + dstOffs, thisSegment, thisSegmentUncompressedSize);
			free(thisSegment);
		} else {
			thisSegmentUncompressedSize = thisSegmentSize;
			memcpy(out + dstOffs, buffer + offset, thisSegmentSize);
		}
		dstOffs += thisSegmentUncompressedSize;
		offset += thisSegmentSize;
	}

	return out;
}

int CxIsCompressedLZXComp(const unsigned char *buffer, unsigned int size) {
	if (size < 0x14) return 0;

	uint32_t magic = *(uint32_t *) buffer;
	if (magic != 'COMP' && magic != 'PMOC') return 0;

	//validate headers
	uint32_t nSegments = *(uint32_t *) (buffer + 0x8);
	uint32_t headerSize = 0x10 + 4 * nSegments;
	uint32_t offset = headerSize;
	uint32_t uncompSize = 0;
	if (nSegments == 0) return 0;
	for (uint32_t i = 0; i < nSegments; i++) {

		//parse segment length & compression setting
		int32_t thisSegmentLength = *(int32_t *) (buffer + 0x10 + i * 4);
		int segCompressed = thisSegmentLength >= 0; //length >= 0 means segment is compressed
		if (thisSegmentLength < 0) {
			thisSegmentLength = -thisSegmentLength;
		}
		if (offset + thisSegmentLength > size) return 0;

		//decompression (if applicable)
		if (segCompressed) {
			if (!CxIsCompressedLZX(buffer + offset, thisSegmentLength)) return 0;
			uncompSize += *(uint32_t *) (buffer + offset) >> 8;
		} else {
			uncompSize += thisSegmentLength;
		}
		offset += thisSegmentLength;
	}
	if (uncompSize != *(uint32_t *) (buffer + 0x4)) return 0;

	return 1;
}

