#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "compression.h"
#include "bstream.h"

#ifdef _MSC_VER
#define inline __inline
#endif


//struct for mapping an LZ graph
typedef struct CxiLzNode_ {
	uint16_t distance;         // distance of node if reference
	uint16_t length;           // length of node
	uint32_t weight;           // weight of node
} CxiLzNode;

//struct for representing tokenized LZ data
typedef struct CxiLzToken_ {
	uint8_t isReference;
	union {
		uint8_t symbol;
		struct {
			int16_t length;
			int16_t distance;
		};
	};
} CxiLzToken;


static unsigned int CxiCompareMemory(const unsigned char *b1, const unsigned char *b2, unsigned int nMax, unsigned int nAbsoluteMax) {
	if (nMax > nAbsoluteMax) nMax = nAbsoluteMax;

	if (nAbsoluteMax >= nMax) {
		//compare nAbsoluteMax bytes, do not perform any looping.
		unsigned int nSame = 0;
		while (nAbsoluteMax > 0) {
			if (*(b1++) != *(b2++)) break;
			nAbsoluteMax--;
			nSame++;
		}
		return nSame;
	} else {
		//compare nMax bytes, then repeat the comparison until nAbsoluteMax is 0.
		unsigned int nSame = 0;
		while (nAbsoluteMax > 0) {

			//compare strings once, incrementing b2 (but keeping b1 fixed since it's repeating)
			unsigned int nSameThis = 0;
			for (unsigned int i = 0; i < nMax; i++) {
				if (b1[i] == *(b2++)) {
					nSameThis++;
				} else {
					break;
				}
			}

			nAbsoluteMax -= nSameThis;
			nSame += nSameThis;
			if (nSameThis < nMax) break; //failed comparison
		}
		return nSame;
	}

}

static unsigned int CxiSearchLZ(const unsigned char *buffer, unsigned int size, unsigned int curpos, unsigned int minDistance, unsigned int maxDistance, unsigned int maxLength, unsigned int *pDistance) {
	//nProcessedBytes = curpos
	unsigned int nBytesLeft = size - curpos;

	//the maximum distance we can search backwards is limited by how far into the buffer we are. It won't
	//make sense to a decoder to copy bytes from before we've started.
	if (maxDistance > curpos) maxDistance = curpos;

	//keep track of the biggest match and where it was
	unsigned int biggestRun = 0, biggestRunIndex = 0;

	//the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//begin searching backwards.
	for (unsigned int j = minDistance; j <= maxDistance; j++) {
		//compare up to 0xF bytes, at most j bytes.
		unsigned int nCompare = maxLength;
		if (nCompare > j) nCompare = j;
		if (nCompare > nMaxCompare) nCompare = nMaxCompare;

		unsigned int nMatched = CxiCompareMemory(buffer - j, buffer, nCompare, nMaxCompare);
		if (nMatched > biggestRun) {
			biggestRun = nMatched;
			biggestRunIndex = j;
			if (biggestRun == nMaxCompare) break;
		}
	}

	*pDistance = biggestRunIndex;
	return biggestRun;
}


// ----- LZ77 Routines

#define LZ_MIN_DISTANCE        0x01   // minimum distance per LZ encoding
#define LZ_MIN_SAFE_DISTANCE   0x02   // minimum safe distance per BIOS LZ bug
#define LZ_MAX_DISTANCE      0x1000   // maximum distance per LZ encoding
#define LZ_MIN_LENGTH          0x03   // minimum length per LZ encoding
#define LZ_MAX_LENGTH          0x12   // maximum length per LZ encoding

static inline int CxiLzNodeIsReference(const CxiLzNode *node) {
	return node->length >= LZ_MIN_LENGTH;
}

//length of compressed data output by LZ token
static inline unsigned int CxiLzTokenCost(unsigned int length) {
	unsigned int nBytesToken;
	if (length >= LZ_MIN_LENGTH) {
		nBytesToken = 2;
	} else {
		nBytesToken = 1;
	}
	return 1 + nBytesToken * 8;
}


unsigned char *CxCompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//create node list
	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));

	//work backwards from the end of file
	unsigned int pos = size;
	while (pos) {
		//decrement
		pos--;

		//get node at pos
		CxiLzNode *node = nodes + pos;

		//optimization: limit max search length towards end of file
		unsigned int maxSearchLen = LZ_MAX_LENGTH;
		if (maxSearchLen > (size - pos)) maxSearchLen = size - pos;
		if (maxSearchLen < LZ_MIN_LENGTH) maxSearchLen = 1;

		//search for largest LZ string match
		unsigned int len, dist;
		if (maxSearchLen >= LZ_MIN_LENGTH) {
			len = CxiSearchLZ(buffer + pos, size, pos, LZ_MIN_SAFE_DISTANCE, LZ_MAX_DISTANCE, maxSearchLen, &dist);
		} else {
			//dummy
			len = 1, dist = 1;
		}

		//if len < LZ_MIN_LENGTH, treat as literal byte node.
		if (len == 0 || len < LZ_MIN_LENGTH) {
			len = 1;
		}

		//if node takes us to the end of file, set weight to cost of this node.
		if ((pos + len) == size) {
			//token takes us to the end of the file, its weight equals this token cost.
			node->length = len;
			node->distance = dist;
			node->weight = CxiLzTokenCost(len);
		} else {
			//else, search LZ matches from here down.
			unsigned int weightBest = UINT_MAX;
			unsigned int lenBest = 1;
			while (len) {
				//measure cost
				unsigned int weightNext = nodes[pos + len].weight;
				unsigned int weight = CxiLzTokenCost(len) + weightNext;
				if (weight < weightBest) {
					lenBest = len;
					weightBest = weight;
				}

				//decrement length w.r.t. length discontinuity
				len--;
				if (len != 0 && len < LZ_MIN_LENGTH) len = 1;
			}

			//put node
			node->length = lenBest;
			node->distance = dist;
			node->weight = weightBest;
		}
	}

	//from here on, we have a direct path to the end of file. All we need to do is traverse it.

	//get max compressed size
	unsigned int maxCompressed = 4 + size + (size + 7) / 8;

	//encode LZ data
	unsigned char *buf = (unsigned char *) calloc(maxCompressed, 1);
	unsigned char *bufpos = buf;
	*(uint32_t *) (bufpos) = (size << 8) | 0x10;
	bufpos += 4;

	CxiLzNode *curnode = &nodes[0];

	unsigned int srcpos = 0;
	while (srcpos < size) {
		uint8_t head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int i = 0; i < 8 && srcpos < size; i++) {
			unsigned int length = curnode->length;
			unsigned int distance = curnode->distance;

			if (CxiLzNodeIsReference(curnode)) {
				//node is reference
				uint16_t enc = (distance - LZ_MIN_DISTANCE) | ((length - LZ_MIN_LENGTH) << 12);
				*(bufpos++) = (enc >> 8) & 0xFF;
				*(bufpos++) = (enc >> 0) & 0xFF;
				head |= 1 << (7 - i);
			} else {
				//node is literal byte
				*(bufpos++) = buffer[srcpos];
			}

			srcpos += length; //remember: nodes correspond to byte positions
			curnode += length;
		}

		//put head byte
		*headpos = head;
	}

	//nodes no longer needed
	free(nodes);

	unsigned int outSize = bufpos - buf;
	*compressedSize = outSize;
	return realloc(buf, outSize); //reduce buffer size
}

unsigned char *CxDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize){
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
				if(dstOffset == length) return result;
			} else {
				uint8_t high = buffer[offset++];
				uint8_t low = buffer[offset++];

				//length of uncompressed chunk and offset
				uint32_t offs = (((high & 0xF) << 8) | low) + 1;
				uint32_t len = (high >> 4) + 3;
				for (uint32_t j = 0; j < len; j++) {
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if(dstOffset == length) return result;
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


// ----- LZ77 With Header Routines

unsigned char *CxDecompressLZHeader(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	if (size < 8) return NULL;
	return CxDecompressLZ(buffer + 4, size - 4, uncompressedSize);
}

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

int CxIsFilteredLZHeader(const unsigned char *buffer, unsigned int size) {
	if (size < 8) return 0;
	if (buffer[0] != 'L' || buffer[1] != 'Z' || buffer[2] != '7' || buffer[3] != '7') return 0;
	return CxIsCompressedLZ(buffer + 4, size - 4);
}




// ----- LZX Routines

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

static inline int CxiLzxNodeIsReference(const CxiLzNode *node) {
	return node->length >= LZX_MIN_LENGTH;
}

//length of compressed data output by LZ token
static inline unsigned int CxiLzxTokenCost(unsigned int length) {
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
	unsigned int compressedMaxSize = 7 + 9 * ((size + 7) >> 3);
	unsigned char *compressed = (unsigned char *) malloc(compressedMaxSize);
	unsigned char *compressedBase = compressed;
	*(uint32_t *) compressed = 0x11 | (size << 8);

	unsigned int nProcessedBytes = 0;
	unsigned int nSize = 4;
	compressed += 4;

	while (nProcessedBytes < size) {
		//make note of where to store the head for later.
		unsigned char *headLocation = compressed++;
		unsigned char head = 0;
		nSize++;

		//repeat 8x (8 bits per byte)
		for (int i = 0; i < 8; i++) {
			head <<= 1;

			if (nProcessedBytes >= size) {
				continue; //allows head byte to shift one place
			}

			unsigned int biggestRun = 0, biggestRunIndex = 0;
			biggestRun = CxiSearchLZ(buffer, size, nProcessedBytes, 2, 0x1000, 0xFFFF + 0x111, &biggestRunIndex);

			//if the biggest run is at least 3, then we use it.
			if (biggestRun >= 3) {
				head |= 1;
				nProcessedBytes += biggestRun;
				//encode the match.

				if (biggestRun <= LZX_MAX_LENGTH_1) {
					//First byte has high nybble as length minus 1, low nybble as the high byte of the offset.
					*(compressed++) = ((biggestRun - 1) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 2;
				} else if (biggestRun <= LZX_MAX_LENGTH_2) {
					//First byte has the high 4 bits of run length minus 0x11
					//Second byte has the low 4 bits of the run length minus 0x11 in the high nybble
					*(compressed++) = (biggestRun - LZX_MIN_LENGTH_2) >> 4;
					*(compressed++) = (((biggestRun - LZX_MIN_LENGTH_2) & 0xF) << 4) | ((biggestRunIndex - 1) >> 8);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 3;
				} else if (biggestRun <= LZX_MAX_LENGTH_3) {
					//First byte is 0x10 ORed with the high 4 bits of run length minus 0x111
					*(compressed++) = 0x10 | (((biggestRun - LZX_MIN_LENGTH_3) >> 12) & 0xF);
					*(compressed++) = ((biggestRun - LZX_MIN_LENGTH_3) >> 4) & 0xFF;
					*(compressed++) = (((biggestRun - LZX_MIN_LENGTH_3) & 0xF) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;

					nSize += 4;
				}
				//advance the buffer
				buffer += biggestRun;
			} else {
				*(compressed++) = *(buffer++);
				nProcessedBytes++;
				nSize++;
			}
		}
		*headLocation = head;
	}

	while (nSize & 3) {
		*(compressed++) = 0;
		nSize++;
	}
	*compressedSize = nSize;
	return realloc(compressedBase, nSize);
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
						len = ((high << 4) | (low >> 4)) + 0x11; //8-bit length +0x11
						offs = (((low & 0xF) << 8) | low2) + 1; //12-bit offset
						break;
					case 1:
						if (offset + 1 >= size) return NULL;
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

				//test write
				if (dstOffset < offs) return NULL; //would we write before our buffer decompressing? (weird because unsigned)
				for (uint32_t j = 0; j < len; j++) {
					if (dstOffset >= length) return NULL;
					dstOffset++;
					if (dstOffset == length) return (unsigned char *) (buffer + offset);
				}
			}
		}
	}

	return (unsigned char *) (buffer + offset);;
}


// ----- Huffman Routines

typedef struct CxiHuffNode_ {
	uint16_t sym;
	uint16_t symMin; //had space to spare, maybe make searches a little simpler
	uint16_t symMax;
	uint16_t nRepresent;
	int freq;
	struct CxiHuffNode_ *left;
	struct CxiHuffNode_ *right;
} CxiHuffNode;

typedef struct BITSTREAM_ {
	uint32_t *bits;
	int nWords;
	int nBitsInLastWord;
	int nWordsAlloc;
	int length;
} BITSTREAM;

unsigned char *CxDecompressHuffman(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	if (size < 5) return NULL;

	uint32_t outSize = (*(uint32_t *) buffer) >> 8;
	unsigned char *out = (unsigned char *) malloc((outSize + 3) & ~3);
	*uncompressedSize = outSize;

	const unsigned char *treeBase = buffer + 4;
	int symSize = *buffer & 0xF;
	int bufferFill = 0;
	int bufferSize = 32 / symSize;
	uint32_t outBuffer = 0;

	int offs = ((*treeBase + 1) << 1) + 4;
	int trOffs = 1;

	unsigned int nWritten = 0;
	while (nWritten < outSize) {

		uint32_t bits = *(uint32_t *) (buffer + offs);
		offs += 4;

		for (int i = 0; i < 32; i++) {
			int lr = (bits >> 31) & 1;
			unsigned char thisNode = treeBase[trOffs];
			int thisNodeOffs = ((thisNode & 0x3F) + 1) << 1; //add to current offset rounded down to get next element offset

			trOffs = (trOffs & ~1) + thisNodeOffs + lr;

			if (thisNode & (0x80 >> lr)) { //reached a leaf node!
				outBuffer >>= symSize;
				outBuffer |= treeBase[trOffs] << (32 - symSize);
				trOffs = 1;
				bufferFill++;

				if (bufferFill >= bufferSize) {
					*(uint32_t *) (out + nWritten) = outBuffer;
					nWritten += 4;
					bufferFill = 0;
				}
			}
			if (nWritten >= outSize) return out;
			bits <<= 1; //next bit
		}
	}

	return out;
}


// ----- RLE Routines

unsigned char *CxCompressRL(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//worst-case size: 4+size*129/128
	int maxSize = 4 + size + (size + 127) / 128 + 3;
	unsigned char *out = (unsigned char *) calloc(maxSize, 1);

	*(uint32_t *) out = 0x30 | (size << 8);
	unsigned int srcOfs = 0, dstOfs = 4;
	while (srcOfs < size) {
		unsigned char b = buffer[srcOfs];

		//scan forward - if found >= 3 bytes, make RL block
		unsigned int blockSize = 1;
		for (unsigned int i = 1; i < 130 && (srcOfs + i) < size; i++) {
			if (buffer[srcOfs + i] == b) {
				blockSize++;
			} else {
				break;
			}
		}

		//if blockSize >= 3, write RL block
		if (blockSize >= 3) {
			out[dstOfs++] = 0x80 | (blockSize - 3);
			out[dstOfs++] = b;
			srcOfs += blockSize;
			continue;
		}

		//else, scan bytes until we reach a run of 3
		int foundRun = 0;
		unsigned int runOffset = 0;
		for (unsigned int i = 1; i < 128 && (srcOfs + i) < size; i++) {
			unsigned char c = buffer[srcOfs + i];
			blockSize = 1;
			for (unsigned int j = 1; j < 3 && (srcOfs + i + j) < size; j++) {
				if (buffer[srcOfs + i + j] == c) {
					blockSize++;
				} else {
					break;
				}
			}

			if (blockSize == 3) {
				foundRun = 1;
				runOffset = i;
				break;
			}
		}

		//if no run found, copy max bytes
		if (!foundRun) {
			runOffset = 128;
			if (srcOfs + runOffset > size) runOffset = size - srcOfs;
		}

		//write uncompressed block
		out[dstOfs++] = (runOffset - 1) & 0x7F;
		memcpy(out + dstOfs, buffer + srcOfs, runOffset);
		srcOfs += runOffset;
		dstOfs += runOffset;
	}

	*compressedSize = dstOfs;
	out = realloc(out, dstOfs);
	return out;
}

unsigned char *CxDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int uncompSize = (*(uint32_t *) buffer) >> 8;
	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);
	*uncompressedSize = uncompSize;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize) {
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed) {
			int chunkLen = (head & 0x7F) + 3;
			unsigned char b = buffer[srcOfs++];
			for (int i = 0; i < chunkLen; i++) {
				out[dstOfs++] = b;
			}
		} else {
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++) {
				out[dstOfs++] = buffer[srcOfs++];
			}
		}
	}

	return out;
}

int CxIsCompressedRL(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (*buffer != 0x30) return 0;
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;

	unsigned int dstOfs = 0;
	unsigned int srcOfs = 4;
	while (dstOfs < uncompSize) {
		if (srcOfs >= size) return 0;
		unsigned char head = buffer[srcOfs++];

		int compressed = head >> 7;
		if (compressed) {
			int chunkLen = (head & 0x7F) + 3;
			if (srcOfs >= size) return 0;
			srcOfs++;

			for (int i = 0; i < chunkLen; i++) {
				dstOfs++;
			}
		} else {
			int chunkLen = (head & 0x7F) + 1;
			for (int i = 0; i < chunkLen; i++) {
				if (srcOfs >= size) return 0;
				dstOfs++;
				srcOfs++;
			}
		}

		if (dstOfs > uncompSize) return 0;
	}

	//allow up to 3 bytes padding
	if (size - srcOfs > 3) return 0;

	return 1;
}


// ----- Diff Routines

unsigned char *CxFilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *filteredSize) {
	char *out = (char *) calloc(size + 4, 1);
	*filteredSize = size + 4;

	*(uint32_t *) out = 0x80 | (size << 8);

	unsigned char last = 0;
	for (unsigned int i = 0; i < size; i++) {
		out[i + 4] = buffer[i] - last;
		last = buffer[i];
	}

	return out;
}

unsigned char *CxFilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *filteredSize) {
	unsigned int outSize = ((size + 1) & ~1) + 4; //round up to multiple of 2
	char *out = (char *) calloc(outSize, 1);
	*filteredSize = outSize;

	*(uint32_t *) out = 0x81 | (size << 8);

	uint16_t last = 0;
	for (unsigned int i = 0; i < size; i += 2) {
		uint16_t hw;
		if (i + 1 < size) { //doesn't run off the end
			hw = *(uint16_t *) (buffer + i);
		} else {
			hw = buffer[i];
		}
		*(uint16_t *) (out + 4 + i) = hw - last;
		last = hw;
	}

	return out;
}

unsigned char *CxUnfilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *unfilteredSize) {
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	unsigned int dstOfs = 0, srcOfs = 4;;
	*unfilteredSize = uncompSize;

	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);

	unsigned char last = 0;
	while (dstOfs < uncompSize) {
		last += buffer[srcOfs++];
		out[dstOfs++] = last;
	}

	return out;
}

unsigned char *CxUnfilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *unfilteredSize) {
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	unsigned int dstOfs = 0, srcOfs = 4;;
	*unfilteredSize = uncompSize;

	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);
	uint16_t last = 0;
	while (dstOfs < uncompSize) {
		last += *(uint16_t *) (buffer + srcOfs);
		srcOfs += 2;
		*(uint16_t *) (out + dstOfs) = last;
		dstOfs += 2;
	}

	return out;
}

int CxIsFilteredDiff8(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;

	uint32_t head = *(uint32_t *) buffer;
	unsigned int uncompSize = head >> 8;
	if (buffer[0] != 0x80) return 0;

	if (uncompSize > size) return 0;

	//round up to a multiple of 4
	if (((size - 4 + 3) & ~3) != ((uncompSize + 3) & ~3)) return 0;
	return 1;
}

int CxIsFilteredDiff16(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return 0;
	if (size & 1) return 0;

	uint32_t head = *(uint32_t *) buffer;
	unsigned int uncompSize = head >> 8;
	if (buffer[0] != 0x81) return 0;

	if (uncompSize > size) return 0;

	//round up to a multiple of 4
	if (((size - 4 + 3) & ~3) != ((uncompSize + 3) & ~3)) return 0;
	return 1;
}



static void CxiBitStreamCreate(BITSTREAM *stream) {
	stream->nWords = 0;
	stream->length = 0;
	stream->nBitsInLastWord = 32;
	stream->nWordsAlloc = 16;
	stream->bits = (uint32_t *) calloc(stream->nWordsAlloc, 4);
}

static void CxiBitStreamFree(BITSTREAM *stream) {
	free(stream->bits);
}

static void CxiBitStreamWrite(BITSTREAM *stream, int bit) {
	if (stream->nBitsInLastWord == 32) {
		stream->nBitsInLastWord = 0;
		stream->nWords++;
		if (stream->nWords > stream->nWordsAlloc) {
			int newAllocSize = (stream->nWordsAlloc + 2) * 3 / 2;
			stream->bits = realloc(stream->bits, newAllocSize * 4);
			stream->nWordsAlloc = newAllocSize;
		}
		stream->bits[stream->nWords - 1] = 0;
	}

	stream->bits[stream->nWords - 1] |= (bit << (31 - stream->nBitsInLastWord));
	stream->nBitsInLastWord++;
	stream->length++;
}

static void *CxiBitStreamGetBytes(BITSTREAM *stream, int wordAlign, int beBytes, int beBits, unsigned int *size) {
	//allocate buffer
	unsigned int outSize = stream->nWords * 4;
	if (!wordAlign) {
		//nBitsInLast word is 32 if last word is full, 0 if empty.
		if (stream->nBitsInLastWord <= 24) outSize--;
		if (stream->nBitsInLastWord <= 16) outSize--;
		if (stream->nBitsInLastWord <=  8) outSize--;
		if (stream->nBitsInLastWord <=  0) outSize--;
	}
	unsigned char *outbuf = (unsigned char *) calloc(outSize, 1);

	//this function handles converting byte and bit orders from the internal
	//representation. Internally, we store the bit sequence as an array of
	//words, where the first bits are inserted at the most significant bit.
	//

	for (unsigned int i = 0; i < outSize; i++) {
		int byteShift = 8 * ((beBytes) ? (3 - (i % 4)) : (i % 4));
		uint32_t word = stream->bits[i / 4];
		uint8_t byte = (word >> byteShift) & 0xFF;

		//if little endian bit order, swap here
		if (!beBits) {
			uint8_t temp = byte;
			byte = 0;
			for (int j = 0; j < 8; j++) byte |= ((temp >> j) & 1) << (7 - j);
		}
		outbuf[i] = byte;
	}

	*size = outSize;
	return outbuf;
}

static void CxiBitStreamWriteBits(BITSTREAM *stream, uint32_t bits, int nBits) {
	for (int i = 0; i < nBits; i++) CxiBitStreamWrite(stream, (bits >> i) & 1);
}

static void CxiBitStreamWriteBitsBE(BITSTREAM *stream, uint32_t bits, int nBits) {
	for (int i = 0; i < nBits; i++) CxiBitStreamWrite(stream, (bits >> (nBits - 1 - i)) & 1);
}

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)

static int CxiHuffmanNodeComparator(const void *p1, const void *p2) {
	return ((CxiHuffNode *) p2)->freq - ((CxiHuffNode *) p1)->freq;
}

static unsigned int CxiHuffmanWriteNode(unsigned char *tree, unsigned int pos, CxiHuffNode *node) {
	CxiHuffNode *left = node->left;
	CxiHuffNode *right = node->right;

	//we will write two bytes. 
	unsigned int afterPos = pos + 2;
	if (ISLEAF(left)) {
		tree[pos] = (unsigned char) left->sym;
	} else {
		CxiHuffNode *leftLeft = left->left;
		CxiHuffNode *leftRight = left->right;
		unsigned char flag = (ISLEAF(leftLeft) << 7) | (ISLEAF(leftRight) << 6);
		unsigned lastAfterPos = afterPos;
		afterPos = CxiHuffmanWriteNode(tree, afterPos, left);
		tree[pos] = flag | ((((lastAfterPos - pos) >> 1) - 1) & 0x3F);
		if (((lastAfterPos - pos) >> 1) - 1 > 0x3F) __debugbreak();
	}

	if (ISLEAF(right)) {
		tree[pos + 1] = (unsigned char) right->sym;
	} else {
		CxiHuffNode *rightLeft = right->left;
		CxiHuffNode *rightRight = right->right;
		unsigned char flag = (ISLEAF(rightLeft) << 7) | (ISLEAF(rightRight) << 6);
		unsigned lastAfterPos = afterPos;
		afterPos = CxiHuffmanWriteNode(tree, afterPos, right);
		tree[pos + 1] = flag | ((((lastAfterPos - pos) >> 1) - 1) & 0x3F);
		if (((lastAfterPos - pos) >> 1) - 1 > 0x3F) __debugbreak();
	}
	return afterPos;
}

static void CxiHuffmanMakeShallowFirst(CxiHuffNode *node) {
	if (ISLEAF(node)) return;
	if (node->left->nRepresent > node->right->nRepresent) {
		CxiHuffNode *left = node->left;
		node->left = node->right;
		node->right = left;
	}
	CxiHuffmanMakeShallowFirst(node->left);
	CxiHuffmanMakeShallowFirst(node->right);
}

static int CxiHuffmanHasSymbol(CxiHuffNode *node, uint16_t sym) {
	if (ISLEAF(node)) return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax) return 0;
	CxiHuffNode *left = node->left;
	CxiHuffNode *right = node->right;
	return CxiHuffmanHasSymbol(left, sym) || CxiHuffmanHasSymbol(right, sym);
}

static void CxiHuffmanWriteSymbol(BITSTREAM *bits, uint16_t sym, CxiHuffNode *tree) {
	if (ISLEAF(tree)) return;
	CxiHuffNode *left = tree->left;
	CxiHuffNode *right = tree->right;
	if (CxiHuffmanHasSymbol(left, sym)) {
		CxiBitStreamWrite(bits, 0);
		CxiHuffmanWriteSymbol(bits, sym, left);
	} else {
		CxiBitStreamWrite(bits, 1);
		CxiHuffmanWriteSymbol(bits, sym, right);
	}
}

static void CxiHuffmanConstructTree(CxiHuffNode *nodes, int nNodes) {
	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nNodes = i;
			break;
		}
	}

	//unflatten the histogram into a huffman tree. 
	int nRoots = nNodes;
	int nTotalNodes = nNodes;
	while (nRoots > 1) {
		//copy bottom two nodes to just outside the current range
		CxiHuffNode *srcA = nodes + nRoots - 2;
		CxiHuffNode *destA = nodes + nTotalNodes;
		memcpy(destA, srcA, sizeof(CxiHuffNode));

		CxiHuffNode *left = destA;
		CxiHuffNode *right = nodes + nRoots - 1;
		CxiHuffNode *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->symMin = min(left->symMin, right->symMin);
		branch->symMax = max(right->symMax, left->symMax);
		branch->nRepresent = left->nRepresent + right->nRepresent; //may overflow for root, but the root doesn't really matter for this

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(CxiHuffNode), CxiHuffmanNodeComparator);
	}

	//just to be sure, make sure the shallow node always comes first
	CxiHuffmanMakeShallowFirst(nodes);
}

unsigned char *CxCompressHuffman(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, int nBits) {
	//create a histogram of each byte in the file.
	CxiHuffNode *nodes = (CxiHuffNode *) calloc(512, sizeof(CxiHuffNode));
	int nSym = 1 << nBits;
	for (int i = 0; i < nSym; i++) {
		nodes[i].sym = i;
		nodes[i].symMin = i;
		nodes[i].symMax = i;
		nodes[i].nRepresent = 1;
	}

	//construct histogram
	if (nBits == 8) {
		for (unsigned int i = 0; i < size; i++) {
			nodes[buffer[i]].freq++;
		}
	} else {
		for (unsigned int i = 0; i < size; i++) {
			nodes[buffer[i] & 0xF].freq++;
			nodes[buffer[i] >> 4].freq++;
		}
	}

	CxiHuffmanConstructTree(nodes, nSym);

	//now we've got a proper Huffman tree. Great! 
	unsigned char *tree = (unsigned char *) calloc(512, 1);
	uint32_t treeSize = CxiHuffmanWriteNode(tree, 2, nodes);
	treeSize = (treeSize + 3) & ~3; //round up
	tree[0] = (treeSize >> 1) - 1;
	tree[1] = 0;

	//now write bits out.
	BITSTREAM stream;
	CxiBitStreamCreate(&stream);
	if (nBits == 8) {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, buffer[i], nodes);
		}
	} else {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, buffer[i] & 0xF, nodes);
			CxiHuffmanWriteSymbol(&stream, buffer[i] >> 4, nodes);
		}
	}

	//combine into one
	uint32_t outSize = 4 + treeSize + stream.nWords * 4;
	char *finBuf = (char *) malloc(outSize);
	*(uint32_t *) finBuf = 0x20 | nBits | (size << 8);
	memcpy(finBuf + 4, tree, treeSize);
	memcpy(finBuf + 4 + treeSize, stream.bits, stream.nWords * 4);
	free(tree);
	free(nodes);
	CxiBitStreamFree(&stream);

	*compressedSize = outSize;
	return finBuf;
}

unsigned char *CxCompressHuffman8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	return CxCompressHuffman(buffer, size, compressedSize, 8);
}

unsigned char *CxCompressHuffman4(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	return CxCompressHuffman(buffer, size, compressedSize, 4);
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

int CxIsCompressedHuffman(const unsigned char *buffer, unsigned int size) {
	if (size < 5) return 0;
	if (*buffer != 0x24 && *buffer != 0x28) return 0;

	uint32_t length = (*(uint32_t *) buffer) >> 8;
	uint32_t bitStreamOffset = ((buffer[5] + 1) << 1) + 4;
	if (bitStreamOffset > size) return 0;

	//process huffman tree
	uint32_t dataOffset = ((buffer[4] + 1) << 1) + 4;
	if (dataOffset > size) return 0;

	//check if the uncompressed size makes sense
	uint32_t bitStreamLength = size - dataOffset;
	if (bitStreamLength * 8 < length) return 0;
	return 1;
}

int CxIsCompressedHuffman4(const unsigned char *buffer, unsigned int size) {
	return size > 0 && *buffer == 0x24 && CxIsCompressedHuffman(buffer, size);
}

int CxIsCompressedHuffman8(const unsigned char *buffer, unsigned int size) {
	return size > 0 && *buffer == 0x28 && CxIsCompressedHuffman(buffer, size);
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



//MvDK compression

#define MVDK_DUMMY       0
#define MVDK_LZ          1
#define MVDK_DEFLATE     2
#define MVDK_RLE         3
#define MVDK_INVALID     -1

typedef struct DEFLATE_TABLE_ENTRY_ {
	uint16_t nMinorBits;
	uint16_t majorPart;
} DEFLATE_TABLE_ENTRY;

typedef struct DEFLATE_TREE_NODE {
	struct DEFLATE_TREE_NODE *left;
	struct DEFLATE_TREE_NODE *right;
	uint8_t depth;
	uint8_t isLeaf;
	uint16_t value;
	uint32_t path;
} DEFLATE_TREE_NODE;

typedef struct DEFLATE_WORK_BUFFER_ {
	DEFLATE_TREE_NODE symbolNodeBuffer[855];
	DEFLATE_TREE_NODE lengthNodeBuffer[855];
	DEFLATE_TREE_NODE *nextAvailable;
} DEFLATE_WORK_BUFFER;

typedef struct BIT_READER_8_ {
	const unsigned char *start;
	const unsigned char *end;
	const unsigned char *pos;
	unsigned char current;
	uint8_t nBitsBuffered;
	uint8_t error;
	uint32_t nBitsRead;
} BIT_READER_8;

static const DEFLATE_TABLE_ENTRY sDeflateLengthTable[] = {
	{ 0, 0x00 }, { 0, 0x01 }, { 0, 0x02 }, { 0, 0x03 }, { 0, 0x04 }, { 0, 0x05 }, { 0, 0x06 }, { 0, 0x07 },
	{ 1, 0x08 }, { 1, 0x0A }, { 1, 0x0C }, { 1, 0x0E }, { 2, 0x10 }, { 2, 0x14 }, { 2, 0x18 }, { 2, 0x1C },
	{ 3, 0x20 }, { 3, 0x28 }, { 3, 0x30 }, { 3, 0x38 }, { 4, 0x40 }, { 4, 0x50 }, { 4, 0x60 }, { 4, 0x70 },
	{ 5, 0x80 }, { 5, 0xA0 }, { 5, 0xC0 }, { 5, 0xE0 }, { 0, 0xFF }
};

static const DEFLATE_TABLE_ENTRY sDeflateOffsetTable[] = {
	{ 0,  0x0000 }, { 0,  0x0001 }, { 0,  0x0002 }, { 0,  0x0003 },
	{ 1,  0x0004 }, { 1,  0x0006 }, { 2,  0x0008 }, { 2,  0x000C },
	{ 3,  0x0010 }, { 3,  0x0018 }, { 4,  0x0020 }, { 4,  0x0030 },
	{ 5,  0x0040 }, { 5,  0x0060 }, { 6,  0x0080 }, { 6,  0x00C0 },
	{ 7,  0x0100 }, { 7,  0x0180 }, { 8,  0x0200 }, { 8,  0x0300 },
	{ 9,  0x0400 }, { 9,  0x0600 }, { 10, 0x0800 }, { 10, 0x0C00 },
	{ 11, 0x1000 }, { 11, 0x1800 }, { 12, 0x2000 }, { 12, 0x3000 },
	{ 13, 0x4000 }, { 13, 0x6000 }
};

// deflate decompress (inflate?)

void CxiInitBitReader(BIT_READER_8 *reader, const unsigned char *pos, const unsigned char *end) {
	reader->pos = pos;
	reader->end = end;
	reader->start = pos;
	reader->nBitsBuffered = 8;
	reader->nBitsRead = 0;
	reader->current = *pos;
	reader->error = 0;
}

uint32_t CxiConsumeBit(BIT_READER_8 *reader) {
	unsigned char byteVal = reader->current;
	reader->nBitsBuffered--;
	reader->nBitsRead++;

	if (reader->nBitsBuffered > 0) {
		reader->current >>= 1;
	} else {
		reader->pos++;
		if (reader->pos >= reader->end) {
			reader->error = 1;
		} else {
			reader->nBitsBuffered = 8;
			reader->current = *reader->pos;
		}
	}

	return byteVal & 1;
}

uint32_t CxiConsumeBits(BIT_READER_8 *bitReader, unsigned int nBits) {
	uint32_t string = 0, i = 0;
	for (i = 0; i < nBits; i++) {
		bitReader->nBitsBuffered--;
		bitReader->nBitsRead++;
		string |= (bitReader->current & 1) << i;

		if (bitReader->nBitsBuffered > 0) {
			bitReader->current >>= 1;
		} else {
			bitReader->pos++;
			if (bitReader->pos >= bitReader->end) {
				bitReader->error = 1;
				return string;
			} else {
				bitReader->nBitsBuffered = 8;
				bitReader->current = *bitReader->pos;
			}
		}
	}

	return string;
}


// ----- Huffman tree construction

void CxiHuffmanInsertNode(DEFLATE_WORK_BUFFER *auxBuffer, DEFLATE_TREE_NODE *root, DEFLATE_TREE_NODE *node2, unsigned int depth) {
	//0 for left, 1 for right
	int pathbit = (node2->path >> depth) & 1;

	//depth=0 means insert here
	if (depth == 0) {
		if (pathbit) {
			root->right = node2;
		} else {
			root->left = node2;
		}
		return;
	}

	if (pathbit) {
		//create a right node if it doesn't exist
		if (root->right == NULL) {
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->right = available;
		}
		CxiHuffmanInsertNode(auxBuffer, root->right, node2, depth - 1);
	} else {
		//create a left node if it doesn't exist
		if (root->left == NULL) {
			DEFLATE_TREE_NODE *available = auxBuffer->nextAvailable;
			auxBuffer->nextAvailable++;
			root->left = available;
		}
		CxiHuffmanInsertNode(auxBuffer, root->left, node2, depth - 1);
	}
}


DEFLATE_TREE_NODE *CxiHuffmanReadTree(DEFLATE_WORK_BUFFER *auxBuffer, BIT_READER_8 *reader, DEFLATE_TREE_NODE *nodeBuffer, unsigned int nNodes) {
	unsigned int i, j;
	int paths[32];
	int depthCounts[32];

	//clear buffers
	memset(nodeBuffer, 0, nNodes * 2 * sizeof(DEFLATE_TREE_NODE));
	memset(depthCounts, 0, sizeof(depthCounts));
	memset(paths, 0, sizeof(paths));

	i = 0;
	while (i < nNodes) {
		//Read 1 bit - determines format of node structure?
		if (CxiConsumeBit(reader)) {
			//read 7-bit number from 2 to 129 (number of loop iterations)
			unsigned int nNodesBlock = CxiConsumeBits(reader, 7) + 2;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			//this 5-bit value gets put into the depth of all nodes written here
			unsigned int depth = CxiConsumeBits(reader, 5);
			if (reader->error) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		} else {
			//read 7-bit number from 1 to 128. Number of loop iterations.
			unsigned int nNodesBlock = CxiConsumeBits(reader, 7) + 1;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				uint8_t depth = CxiConsumeBits(reader, 5);
				if (reader->error) return NULL;

				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		}
	}

	//written too many nodes
	if (i > nNodes) return NULL;

	int depth = 0;
	depthCounts[0] = 0;
	for (i = 1; i < 32; i++) {
		depth = (depth + depthCounts[i - 1]) << 1;
		paths[i] = depth;
	}

	DEFLATE_TREE_NODE *root = nodeBuffer + nNodes;
	auxBuffer->nextAvailable = root + 1;

	for (i = 0; i < nNodes; i++) {
		DEFLATE_TREE_NODE *node = nodeBuffer + i;
		node->isLeaf = 1;

		if (node->depth > 0) {
			node->path = paths[node->depth];
			node->value = i;
			paths[node->depth]++;
			CxiHuffmanInsertNode(auxBuffer, root, node, node->depth - 1);
		}
	}
	return root;
}

uint32_t CxiLookupTreeNode(DEFLATE_TREE_NODE *node, BIT_READER_8 *reader) {
	if (node == NULL) return (uint32_t) -1;

	while (!node->isLeaf) {
		if (CxiConsumeBit(reader)) {
			node = node->right;
		} else {
			node = node->left;
		}
		if (reader->error || node == NULL) return (uint32_t) -1;
	}
	return node->value;
}

unsigned char *CxiDecompressDeflateChunk(DEFLATE_WORK_BUFFER *auxBuffer, unsigned char *destBase, const unsigned char **pPos, unsigned char *dest, 
		unsigned char *end, const unsigned char *srcEnd, int write) {
	//init reader
	BIT_READER_8 reader;
	const unsigned char *pos = *pPos;
	uint32_t nBytesConsumed = 0;
	CxiInitBitReader(&reader, pos, srcEnd);

	int isCompressed = CxiConsumeBit(&reader);
	if (reader.error) return NULL;
	uint32_t chunkLen = CxiConsumeBits(&reader, 31);
	if (reader.error) return NULL;

	if (!isCompressed) {
		//uncompressed chunk, just memcpy out
		if ((dest + chunkLen) > end || (dest + chunkLen) < destBase || (pos + 4 + chunkLen) > srcEnd) return NULL;
		if (write) memcpy(dest, pos + 4, chunkLen);

		nBytesConsumed = chunkLen + 4;
		dest += chunkLen;
	} else {
		const unsigned char *tableBase = reader.pos;

		//Consume a Huffman tree. The length of the tree data (in bits) is given by the next 16 bits in the stream.
		uint32_t lzLen2 = CxiConsumeBits(&reader, 16);
		uint32_t table1SizeBytes = (lzLen2 + 7) >> 3;
		const unsigned char *postTree = reader.pos + table1SizeBytes;
		DEFLATE_TREE_NODE *huffRoot1 = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->symbolNodeBuffer, 0x11D);
		if (huffRoot1 == NULL) return NULL;

		//Reposition stream after the Huffman tree. Read out the LZ distance tree next.
		//Its size in bits is given by the following 16 bits from the stream.
		CxiInitBitReader(&reader, postTree, srcEnd);
		reader.nBitsRead = (postTree - pos) * 8;
		lzLen2 = CxiConsumeBits(&reader, 16);
		uint32_t table2SizeBytes = (lzLen2 + 7) >> 3;

		postTree = reader.pos + table2SizeBytes;
		DEFLATE_TREE_NODE *huffDistancesRoot = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->lengthNodeBuffer, 0x1E);
		if (huffDistancesRoot == NULL) return NULL;

		//Reposition stream after this tree to prepare for reading the compressed sequence.
		CxiInitBitReader(&reader, postTree, srcEnd);
		reader.nBitsRead = (reader.pos - pos) * 8;

		while (reader.nBitsRead < chunkLen && dest < end) {
			uint32_t huffVal = CxiLookupTreeNode(huffRoot1, &reader);
			if (huffVal == (uint32_t) -1) return NULL;

			if (huffVal < 0x100) {
				//simple byte value Huffman
				if (write) *dest = (unsigned char) huffVal;
				dest++;
			} else {
				//LZ part Huffman

				//read out length
				uint32_t nLengthMinorBits = sDeflateLengthTable[huffVal - 0x100].nMinorBits;
				uint32_t lzLen1 = sDeflateLengthTable[huffVal - 0x100].majorPart;
				uint32_t lzLen2 = CxiConsumeBits(&reader, nLengthMinorBits);
				uint32_t lzLen = lzLen1 + lzLen2 + 3;

				//read out offset
				uint32_t nodeVal2 = CxiLookupTreeNode(huffDistancesRoot, &reader);
				if (nodeVal2 == (uint32_t) -1) return NULL;

				uint32_t nOffsetMinorBits = sDeflateOffsetTable[nodeVal2].nMinorBits;
				uint32_t lzOffset1 = sDeflateOffsetTable[nodeVal2].majorPart;
				uint32_t lzOffset2 = CxiConsumeBits(&reader, nOffsetMinorBits);
				uint32_t lzOffset = lzOffset1 + lzOffset2 + 1;

				size_t curoffs = dest - destBase;
				size_t remaining = end - dest;
				if (lzOffset > curoffs) return NULL;
				if (lzLen > remaining) return NULL;

				unsigned char *lzSrc = dest - lzOffset;
				unsigned int i;
				for (i = 0; i < lzLen && dest < end; i++) {
					if (write) *dest = *lzSrc;
					dest++, lzSrc++;
				}
			}
		}
		nBytesConsumed = (chunkLen + 7) >> 3;
	}

	*pPos = pos + nBytesConsumed;
	return dest;
}


void CxDecompressDeflate(const unsigned char *filebuf, unsigned char *dest, void *auxBuffer, unsigned int size) {
	const unsigned char *pos = filebuf + 4;
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *) filebuf) >> 2);

	while (dest < end) {
		dest = CxiDecompressDeflateChunk((DEFLATE_WORK_BUFFER *) auxBuffer, destBase, &pos, dest, end, filebuf + size, 1);
	}
}

static int CxiMvdkIsValidLZ(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (uncompSize << 8);
	int valid = CxIsCompressedLZ(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidRL(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (uncompSize << 8);
	int valid = CxIsCompressedRL(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidDeflate(const unsigned char *buffer, unsigned int size) {
	const unsigned char *pos = buffer + 4;
	unsigned char *dest = NULL; //won't be written to
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *) buffer) >> 2); //for address comparison
	DEFLATE_WORK_BUFFER *work = (DEFLATE_WORK_BUFFER *) calloc(1, sizeof(DEFLATE_WORK_BUFFER));

	while (dest < end) {
		dest = CxiDecompressDeflateChunk(work, destBase, &pos, dest, end, buffer + size, 0);
		if (dest == NULL) {
			free(work);
			return 0;
		}
	}
	free(work);

	//test buffer remaining (allow up to 3 bytes trailing for 4-byte aligned file size)
	unsigned int nConsumed = pos - buffer;
	nConsumed = (nConsumed + 3) & ~3;
	if (nConsumed < ((size + 3) & ~3)) return 0; //bytes unconsumed

	return 1;
}


static unsigned int CxiIlog2(unsigned int x) {
	unsigned int y = 0;
	while (x) {
		x >>= 1;
		y++;
	}
	return y - 1;
}

static CxiLzToken *CxiMvdkTokenizeDeflate(const unsigned char *buffer, unsigned int size, int *pnTokens) {
	int tokenBufferSize = 16;
	int tokenBufferLength = 0;
	CxiLzToken *tokenBuffer = (CxiLzToken *) calloc(tokenBufferSize, sizeof(CxiLzToken));

	unsigned int curpos = 0;
	while (curpos < size) {
		//ensure buffer capacity
		if (tokenBufferLength + 1 > tokenBufferSize) {
			tokenBufferSize = (tokenBufferSize + 2) * 3 / 2;
			tokenBuffer = (CxiLzToken *) realloc(tokenBuffer, tokenBufferSize * sizeof(CxiLzToken));
		}

		//search backwards
		unsigned int length, distance;
		length = CxiSearchLZ(buffer, size, curpos, 1, 0x7FFF, 0x102, &distance);

		if (length >= 3) {
			//write LZ reference
			tokenBuffer[tokenBufferLength].isReference = 1;
			tokenBuffer[tokenBufferLength].distance = distance;
			tokenBuffer[tokenBufferLength].length = length;

			buffer += length;
			curpos += length;
		} else {
			//write byte literal
			tokenBuffer[tokenBufferLength].isReference = 0;
			tokenBuffer[tokenBufferLength].symbol = *(buffer++);
			curpos++;
		}

		tokenBufferLength++;
	}

	*pnTokens = tokenBufferLength;
	return tokenBuffer;
}


static int CxiMvdkLookupDeflateTableEntry(const DEFLATE_TABLE_ENTRY *table, int tableSize, unsigned int n) {
	for (int i = tableSize - 1; i >= 0; i--) {
		if (n >= table[i].majorPart) return i;
	}
	return 0;
}

static void CxiMvdkInsertDummyNode(CxiHuffNode *nodes, int nNodes) {
	//find first node with a 0 frequency and give it a dummy frequency.
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq > 0) continue;

		nodes[i].freq = 1;
		nodes[i].symMin = nodes[i].symMax = nodes[i].sym = i;
		nodes[i].nRepresent = 1;
		break;
	}
}

typedef struct CxiHuffmanCode_ {
	uint32_t encoding;
	uint16_t value;
	uint16_t length;
} CxiHuffmanCode;

static int CxiMvdkAppendCanonicalNode(CxiHuffNode *tree, CxiHuffmanCode *codes, uint32_t encoding, int depth) {
	if (ISLEAF(tree)) {
		codes[tree->sym].length = depth;
		return 1;
	}

	//recurse
	int nl = CxiMvdkAppendCanonicalNode(tree->left, codes, (encoding << 1) | 0, depth + 1);
	int nr = CxiMvdkAppendCanonicalNode(tree->right, codes, (encoding << 1) | 1, depth + 1);
	return nl + nr;
}

static int CxiMvdkHuffmanCanonicalComparator(const void *p1, const void *p2) {
	const CxiHuffmanCode *c1 = (const CxiHuffmanCode *) p1;
	const CxiHuffmanCode *c2 = (const CxiHuffmanCode *) p2;

	//force 0-length (excluded) symbols to the end
	if (c1->length == 0) return 1;
	if (c2->length == 0) return -1;

	if (c1->length < c2->length) return -1;
	if (c1->length > c2->length) return 1;
	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return 1;
	return 0;
}

static int CxiMvdkHuffmanSymbolComparator(const void *p1, const void *p2) {
	const CxiHuffmanCode *c1 = (const CxiHuffmanCode *) p1;
	const CxiHuffmanCode *c2 = (const CxiHuffmanCode *) p2;

	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return 1;
	return 0;
}

static void CxiMvdkMakeCanonicalTree(CxiHuffNode *tree, CxiHuffmanCode *codes, int nMaxNodes) {
	//first, recursively append to the list.
	int nNodes = CxiMvdkAppendCanonicalNode(tree, codes, 0, 1);
	for (int i = 0; i < nMaxNodes; i++) {
		codes[i].value = i;
	}

	//next, apply sort. Unassigned codes are pushed to the end of the list.
	qsort(codes, nMaxNodes, sizeof(CxiHuffmanCode), CxiMvdkHuffmanCanonicalComparator);

	//next, we can start assigning codes.
	uint32_t curcode = 0, curbits = 0, curmask = 0;
	for (int i = 0; i < nNodes; i++) {
		//shift code
		while (curbits < codes[i].length) {
			curcode <<= 1;
			curmask = (curmask << 1) | 1;
			curbits++;
		}
		codes[i].encoding = curcode;

		//increment current code
		curcode++;
		if ((curcode & curmask) == 0) {
			curmask = (curmask << 1) | 1;
			curbits++;
		}
	}

	//sort codes by symbol value again (for constant code lookup time)
	qsort(codes, nMaxNodes, sizeof(CxiHuffmanCode), CxiMvdkHuffmanSymbolComparator);
}

static void CxiMvdkWriteHuffmanTree(BITSTREAM *stream, CxiHuffmanCode *codes, int nCodes) {
	//write tree
	for (int i = 0; i < nCodes;) {
		//same as next nodes?
		int nRunLength = 1, repeatedRun = 0;
		for (int j = i + 1; j < nCodes; j++) {
			if (codes[j].length == codes[i].length) nRunLength++;
			else break;
		}

		if (nRunLength >= 2) {
			repeatedRun = 1;
			if (nRunLength > 0x81) nRunLength = 0x81;
		} else {
			//find next position of repeated run
			nRunLength = 1;
			for (int j = i + 1; j < nCodes; j++) {
				if (j == (nCodes - 1)) nRunLength++; //to catch the last element
				else if (codes[j].length != codes[j + 1].length) nRunLength++;
				else break;
			}
			if (nRunLength > 0x80) nRunLength = 0x80;
		}

		//run length >= 2: write run length block
		if (repeatedRun) {
			CxiBitStreamWrite(stream, 1);
			CxiBitStreamWriteBits(stream, nRunLength - 2, 7);
			CxiBitStreamWriteBits(stream, codes[i].length == 0 ? 0 : (codes[i].length - 1), 5);
		} else {
			CxiBitStreamWrite(stream, 0);
			CxiBitStreamWriteBits(stream, nRunLength - 1, 7);
			for (int j = 0; j < nRunLength; j++) {
				unsigned int length = codes[i + j].length;
				CxiBitStreamWriteBits(stream, length == 0 ? 0 : (length - 1), 5);
			}
		}
		i += nRunLength;
	}
}

static unsigned char *CxiCompressMvdkDeflateChunk(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, unsigned int *nOutBits) {
	//first, tokenize the input string.
	int nTokens;
	CxiLzToken *tokens = CxiMvdkTokenizeDeflate(buffer, size, &nTokens);

	//next, compute statistics on the data.
	unsigned int *symbolFrequencies = (unsigned int *) calloc(0x100 + 29, sizeof(unsigned int));
	unsigned int *offsetBinFrequencies = (unsigned int *) calloc(30, sizeof(unsigned int));
	for (int i = 0; i < nTokens; i++) {
		if (!tokens[i].isReference) {
			//byte reference
			symbolFrequencies[tokens[i].symbol]++;
		} else {
			//LZ reference
			unsigned int distance = tokens[i].distance;
			unsigned int length = tokens[i].length;
			
			//if length is 0x102, special case
			if (length == 0x102) {
				symbolFrequencies[0x100 + 28]++;
			} else {
				symbolFrequencies[0x100 + CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3)]++;
			}

			offsetBinFrequencies[CxiMvdkLookupDeflateTableEntry(sDeflateOffsetTable, 30, distance - 1)]++;
		}
	}

	//next: create Huffman tree.
	CxiHuffNode *symbolTree = (CxiHuffNode *) calloc((0x100 + 29) * 2, sizeof(CxiHuffNode));
	CxiHuffNode *offsetTree = (CxiHuffNode *) calloc(30 * 2, sizeof(CxiHuffNode));

	int symbolTreeSize = 0, lengthTreeSize = 0;
	for (int i = 0; i < 0x100 + 29; i++) {
		if (symbolFrequencies[i] == 0) continue;

		CxiHuffNode *node = symbolTree + symbolTreeSize;
		node->sym = node->symMin = node->symMax = i;
		node->freq = symbolFrequencies[i];
		node->nRepresent = 1;
		symbolTreeSize++;
	}
	for (int i = 0; i < 30; i++) {
		if (offsetBinFrequencies[i] == 0) continue;

		CxiHuffNode *node = offsetTree + lengthTreeSize;
		node->sym = node->symMin = node->symMax = i;
		node->freq = offsetBinFrequencies[i];
		node->nRepresent = 1;
		lengthTreeSize++;
	}

	//if we have one node of a tree, insert a dummy node of low frequency. The decompressor
	//won't accept a 0-depth tree (nodes not added to the tree). So we need to ensure that
	//all leaf nodes have a depth of 1 or higher.
	if (symbolTreeSize == 1) {
		CxiMvdkInsertDummyNode(symbolTree, 0x100 + 29);
		symbolTreeSize++;
	}
	if (lengthTreeSize == 1) {
		CxiMvdkInsertDummyNode(offsetTree, 30);
		lengthTreeSize++;
	}

	//construct tree structure
	CxiHuffmanConstructTree(symbolTree, symbolTreeSize);
	CxiHuffmanConstructTree(offsetTree, lengthTreeSize);
	free(symbolFrequencies);
	free(offsetBinFrequencies);

	//convert Huffman tree to canonical form
	CxiHuffmanCode *lengthEncodings = (CxiHuffmanCode *) calloc(0x100 + 29, sizeof(CxiHuffmanCode));
	CxiHuffmanCode *offsetEncodings = (CxiHuffmanCode *) calloc(30, sizeof(CxiHuffmanCode));
	CxiMvdkMakeCanonicalTree(symbolTree, lengthEncodings, 0x100 + 29);
	CxiMvdkMakeCanonicalTree(offsetTree, offsetEncodings, 30);
	free(symbolTree);
	free(offsetTree);

	//write huffman tree
	unsigned char *treeData = NULL;
	unsigned int treeSize = 0;
	{
		BITSTREAM symbolStream, offsetStream;
		CxiBitStreamCreate(&symbolStream);
		CxiBitStreamCreate(&offsetStream);
		CxiMvdkWriteHuffmanTree(&symbolStream, lengthEncodings, 0x100 + 29);
		CxiMvdkWriteHuffmanTree(&offsetStream, offsetEncodings, 30);
		
		unsigned int nBytesSymbolTree = (symbolStream.length + 7) / 8;
		unsigned int nBytesOffsetTree = (offsetStream.length + 7) / 8;
		void *symbolTree = CxiBitStreamGetBytes(&symbolStream, 0, 1, 0, &nBytesSymbolTree);
		void *offsetTree = CxiBitStreamGetBytes(&offsetStream, 0, 1, 0, &nBytesOffsetTree);

		treeSize = 2 + nBytesSymbolTree + 2 + nBytesOffsetTree;
		treeData = (unsigned char *) malloc(treeSize);
		*(uint16_t *) (treeData + 0) = symbolStream.length;
		*(uint16_t *) (treeData + 2 + nBytesSymbolTree) = offsetStream.length;
		
		memcpy(treeData + 2, symbolTree, nBytesSymbolTree);
		memcpy(treeData + 2 + nBytesSymbolTree + 2, offsetTree, nBytesOffsetTree);
		
		CxiBitStreamFree(&symbolStream);
		CxiBitStreamFree(&offsetStream);
		free(symbolTree);
		free(offsetTree);
	}

	//TEST: write out bit stream
	BITSTREAM bitStream;
	CxiBitStreamCreate(&bitStream);
	for (int i = 0; i < nTokens; i++) {

		if (!tokens[i].isReference) {
			//CxiHuffmanWriteSymbol(&bitStream, tokens[i].symbol, symbolTree);
			CxiBitStreamWriteBitsBE(&bitStream, lengthEncodings[tokens[i].symbol].encoding, lengthEncodings[tokens[i].symbol].length - 1);
		} else {
			int lensym = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, tokens[i].length - 3);
			int offsym = CxiMvdkLookupDeflateTableEntry(sDeflateOffsetTable, 30, tokens[i].distance - 1);

			unsigned int lengthMinor = tokens[i].length - 3 - sDeflateLengthTable[lensym].majorPart;
			unsigned int offsetMinor = tokens[i].distance - 1 - sDeflateOffsetTable[offsym].majorPart;
			int nLengthMinor = sDeflateLengthTable[lensym].nMinorBits;
			int nOffsetMinor = sDeflateOffsetTable[offsym].nMinorBits;
			
			CxiBitStreamWriteBitsBE(&bitStream, lengthEncodings[0x100 + lensym].encoding, lengthEncodings[0x100 + lensym].length - 1);
			CxiBitStreamWriteBits(&bitStream, lengthMinor, nLengthMinor);

			CxiBitStreamWriteBitsBE(&bitStream, offsetEncodings[offsym].encoding, offsetEncodings[offsym].length - 1);
			CxiBitStreamWriteBits(&bitStream, offsetMinor, nOffsetMinor);
		}
	}
	free(tokens);

	free(lengthEncodings);
	free(offsetEncodings);

	//extract bytes of bit sequence
	unsigned int nBitsComp = bitStream.length;
	unsigned int compDataSize;
	unsigned char *bytes = CxiBitStreamGetBytes(&bitStream, 0, 1, 0, &compDataSize);
	CxiBitStreamFree(&bitStream);

	unsigned int totalSizeBytes = treeSize + compDataSize;
	unsigned char *outbuf = (unsigned char *) malloc(totalSizeBytes);
	memcpy(outbuf, treeData, treeSize);
	memcpy(outbuf + treeSize, bytes, compDataSize);
	free(bytes);

	*compressedSize = totalSizeBytes;
	*nOutBits = treeSize * 8 + nBitsComp;
	return outbuf;
}

static unsigned char *CxiCompressMvdkDeflate(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);

	//32-bit reserved space (for header)
	uint32_t dummyHeader = 0;
	bstreamWrite(&stream, &dummyHeader, sizeof(dummyHeader));

	unsigned int srcpos = 0;
	while (srcpos < size) {
		//TODO: some heuristic for splitting?
		unsigned int chunkSize = size - srcpos;

		unsigned int chunkCompressedSize = 0, chunkCompressedBits;
		unsigned char *compressedChunk = CxiCompressMvdkDeflateChunk(buffer + srcpos, size, &chunkCompressedSize, &chunkCompressedBits);
		if (chunkCompressedSize < chunkSize) {
			//write compressed block
			uint32_t head = ((chunkCompressedBits + 32) << 1) | 1;
			bstreamWrite(&stream, &head, sizeof(head));
			bstreamWrite(&stream, compressedChunk, chunkCompressedSize);
		} else {
			//write uncompressed block
			uint32_t head = chunkSize << 1;
			bstreamWrite(&stream, &head, sizeof(head));
			bstreamWrite(&stream, (void *) (buffer + srcpos), chunkSize);
		}
		free(compressedChunk);

		srcpos += chunkSize;
	}

	//align
	bstreamAlign(&stream, 4);
	*compressedSize = stream.size;

	//header
	unsigned char *outbuf = stream.buffer;
	*(uint32_t *) (outbuf + 0) = (size << 2) | MVDK_DEFLATE;
	return outbuf;
}




static int CxiMvdkGetCompressionType(const unsigned char *buffer, unsigned int size) {
	return (*(uint32_t *) buffer) & 3;
}

int CxIsCompressedMvDK(const unsigned char *buffer, unsigned int size) {
	if (size < 4) return MVDK_INVALID;

	uint32_t uncompSize = (*(uint32_t *) buffer) >> 2;
	int type = CxiMvdkGetCompressionType(buffer, size);
	switch (type) {
		case MVDK_DUMMY:
			//check size
			return (((size - 4 + 3) & ~3) == ((uncompSize + 3) & ~3)) && ((size - 4) >= uncompSize);
		case MVDK_LZ:
			return CxiMvdkIsValidLZ(buffer, size);
		case MVDK_RLE:
			return CxiMvdkIsValidRL(buffer, size);
		case MVDK_DEFLATE:
			return CxiMvdkIsValidDeflate(buffer, size);
	}
	return 0;
}

static unsigned char *CxiMvdkDecompressDummy(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;
	unsigned char *out = (unsigned char *) malloc(outlen);
	*uncompressedSize = outlen;

	memcpy(out, buffer + 4, outlen);
	return out;
}

static unsigned char *CxiMvdkDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;

	unsigned char *copy = (unsigned char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (outlen << 8);
	unsigned char *out = CxDecompressLZ(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;

	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (outlen << 8);
	char *out = CxDecompressRL(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressDeflate(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outlen = (*(uint32_t *) buffer) >> 2;
	*uncompressedSize = outlen;
	char *dest = malloc(outlen);

	void *aux = calloc(1, sizeof(DEFLATE_WORK_BUFFER));
	CxDecompressDeflate(buffer, dest, aux, size);
	free(aux);
	return dest;
}

unsigned char *CxDecompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	int type = (*(uint32_t *) buffer) & 3;
	switch (type) {
		case MVDK_DUMMY:
			return CxiMvdkDecompressDummy(buffer, size, uncompressedSize);
		case MVDK_LZ:
			return CxiMvdkDecompressLZ(buffer, size, uncompressedSize);
		case MVDK_RLE:
			return CxiMvdkDecompressRL(buffer, size, uncompressedSize);
		case MVDK_DEFLATE:
			return CxiMvdkDecompressDeflate(buffer, size, uncompressedSize);
	}
	*uncompressedSize = 0;
	return NULL;
}

unsigned char *CxCompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	unsigned int dummySize = size + 4;
	unsigned int lzSize, rlSize, dfSize;
	unsigned char *lz = CxCompressLZ(buffer, size, &lzSize);
	unsigned char *rl = CxCompressRL(buffer, size, &rlSize);
	unsigned char *df = CxiCompressMvdkDeflate(buffer, size, &dfSize);

	*(uint32_t *) lz = MVDK_LZ | (size << 2);
	*(uint32_t *) rl = MVDK_RLE | (size << 2);

	if (lzSize <= rlSize && lzSize <= dummySize && lzSize <= dfSize) {
		free(rl);
		free(df);
		*compressedSize = lzSize;
		return lz;
	} else if (rlSize <= lzSize && rlSize <= dummySize && rlSize <= dfSize) {
		free(lz);
		free(df);
		*compressedSize = rlSize;
		return rl;
	} else if (dfSize <= lzSize && dfSize <= rlSize && dfSize <= dummySize) {
		free(lz);
		free(rl);
		*compressedSize = dfSize;
		return df;
	}

	//else
	unsigned char *dummy = (unsigned char *) malloc(size + 4);
	*(uint32_t *) dummy = MVDK_DUMMY | (size << 2);
	memcpy(dummy + 4, buffer, size);

	free(rl);
	free(lz);
	free(df);
	*compressedSize = dummySize;
	return dummy;
}


typedef struct st1_ {
	uint16_t value;
	uint16_t maskBits;
	uint32_t encoding;
	uint32_t mask;
} CxiVlxTreeNode;

typedef struct WordBuffer_ {
	uint32_t bits;
	unsigned int available;
	const unsigned char *src;
	unsigned int srcpos;
	unsigned int size;
	int error;                 // set when we try to read bits off the end of the file
	int nEofBits;              // set when low level buffer reaches end of file
} WordBuffer;

static unsigned char CxiVlxWordBufferFetchByte(WordBuffer *buffer) {
	if (buffer->srcpos < buffer->size) {
		return buffer->src[buffer->srcpos++];
	}

	//update EOF status
	if (buffer->nEofBits <= 24) buffer->nEofBits += 8;
	else buffer->nEofBits = 32;
	return 0;
}

static void CxiVlxWordBufferInit(WordBuffer *buffer, const unsigned char *src, unsigned int srcpos, unsigned int size) {
	buffer->bits = 0;
	buffer->available = 0;
	buffer->src = src;
	buffer->srcpos = srcpos;
	buffer->size = size;
	buffer->error = 0;
	buffer->nEofBits = 0;

	for (int i = 0; i < 4; i++) {
		buffer->bits = CxiVlxWordBufferFetchByte(buffer) | (buffer->bits << 8);
	}
}

static void CxiVlxWordBufferFill(WordBuffer *buffer) {
	while (buffer->available >= 8) {
		buffer->bits |= CxiVlxWordBufferFetchByte(buffer) << (buffer->available - 8);
		buffer->available -= 8;
	}
}

static unsigned char CxiVlxWordBufferReadByte(WordBuffer *buffer) {
	//check if we're reading bits that would be out of bounds
	if (buffer->nEofBits > (32 - 8)) buffer->error = 1;

	unsigned char u = buffer->bits >> 24;
	buffer->bits <<= 8;
	buffer->available += 8;
	CxiVlxWordBufferFill(buffer);

	return u;
}

static uint32_t CxiVlxWordBufferReadBits(WordBuffer *buffer, int nBits) {
	//check that we're reading bits that would be out of bounds
	if (buffer->nEofBits > (32 - nBits)) buffer->error = 1;

	uint32_t bits = buffer->bits >> (32 - nBits);
	buffer->bits <<= nBits;
	buffer->available += nBits;

	CxiVlxWordBufferFill(buffer);
	return bits;
}

static uint32_t CxiVlxReadNextValue(WordBuffer *buffer, CxiVlxTreeNode *tree, int nTreeElements) {
	CxiVlxTreeNode *entry = tree;
	for (int i = 0; i < nTreeElements; i++) {
		if (entry->encoding == (buffer->bits & entry->mask)) {
			//consume bits
			CxiVlxWordBufferReadBits(buffer, entry->maskBits);
			return entry->value;
		}
		entry++;
	}
	
	//not found
	return (uint32_t) -1;
}

static unsigned int CxiVlxGetUncompressedSize(const unsigned char *b) {
	switch (*b & 0xF) {
		case 1:
			return b[1];
		case 2:
			return b[1] | (b[2] << 8);
		case 4:
			return b[1] | (b[2] << 8) | (b[3] << 16) | (b[4] << 16); //yes 16 twice
	}
	return -1;
}

static int CxiTryDecompressVlx(const unsigned char *src, unsigned int size, unsigned char *dest) {
	CxiVlxTreeNode lengthEntries[12] = { 0 };
	CxiVlxTreeNode distEntries[12] = { 0 };
	if (size < 1) return 0; //must be big enough for header byte

	if (*src & 0xF0) return 0; //high 4 bits not used, but always 0

	//ensure the buffer is big enough for the head byte, length, and tree header
	unsigned char lenlen = *src & 0xF;
	switch (lenlen) {
		case 1:
			if (size < 3) return 0;
			break;
		case 2:
			if (size < 4) return 0;
			break;
		case 4:
			if (size < 6) return 0;
			if (src[4] != 0) return 0; //bug in the game's decoder shifts this byte incorrectly
			break;
		default:
			//invalid
			return 0;
	}

	uint32_t outlen = 0, srcpos = 0;
	outlen = CxiVlxGetUncompressedSize(src);
	srcpos += lenlen + 1;

	unsigned char byte1 = src[srcpos++];
	unsigned char hi4 = (byte1 >> 4) & 0xF;
	unsigned char lo4 = (byte1 >> 0) & 0xF;
	if (hi4 > 12 || lo4 > 12) return 0; //too large tree size

	unsigned int treeSize = (hi4 + lo4) * sizeof(uint16_t);
	if (srcpos + treeSize > size) return 0; //not enough space for tree

	for (int i = 0; i < hi4; i++) {
		uint16_t hw = src[srcpos] | (src[srcpos + 1] << 8);
		srcpos += 2;

		lengthEntries[i].value = hw >> 12;
		lengthEntries[i].maskBits = 11;
		if ((hw & 0xFFF) == 0) return 0; //will lock the decoder

		while (!((hw & 0xFFF) & (1 << lengthEntries[i].maskBits))) {
			lengthEntries[i].maskBits--;
		}

		uint32_t shift = 32 - lengthEntries[i].maskBits;
		lengthEntries[i].encoding = (hw & 0xFFF) << shift;
		lengthEntries[i].mask     = 0xFFFFFFFF   << shift;
	}

	for (int i = 0; i < lo4; i++) {
		uint16_t hw = src[srcpos] | (src[srcpos + 1] << 8);
		srcpos += 2;

		distEntries[i].value = hw >> 12;
		distEntries[i].maskBits = 11;
		if ((hw & 0xFFF) == 0) return 0; //will lock the decoder

		while (!((hw & 0xFFF) & (1 << distEntries[i].maskBits))) {
			distEntries[i].maskBits--;
		}

		uint32_t shift = 32 - distEntries[i].maskBits;
		distEntries[i].encoding = (hw & 0xFFF) << shift;
		distEntries[i].mask     = 0xFFFFFFFF   << shift;
	}

	//a proper encoder won't produce duplicate tree entries. 
	uint16_t lengthBitmap = 0, distBitmap = 0;
	for (int i = 0; i < hi4; i++) {
		if (lengthBitmap & (1 << lengthEntries[i].value)) return 0;
		lengthBitmap |= (1 << lengthEntries[i].value);
	}
	for (int i = 0; i < lo4; i++) {
		if (distBitmap & (1 << distEntries[i].value)) return 0;
		distBitmap |= (1 << distEntries[i].value);
	}

	//a proper encoder will ensure that the tree can be interpreted umabiguously.
	for (int i = 0; i < hi4; i++) {
		for (int j = 0; j < hi4; j++) {
			if (j == i) continue;

			//check that [i] is not a prefix of [j]
			if (lengthEntries[i].maskBits > lengthEntries[j].maskBits) continue;
			if (lengthEntries[i].encoding == (lengthEntries[j].encoding & lengthEntries[i].mask)) return 0;
		}
	}
	for (int i = 0; i < lo4; i++) {
		for (int j = 0; j < lo4; j++) {
			if (j == i) continue;

			//check that [i] is not a prefix of [j]
			if (distEntries[i].maskBits > distEntries[j].maskBits) continue;
			if (distEntries[i].encoding == (distEntries[j].encoding & distEntries[i].mask)) return 0;
		}
	}

	uint32_t outpos = 0;
	WordBuffer buffer32;
	CxiVlxWordBufferInit(&buffer32, src, srcpos, size);

	while (outpos < outlen) {
		uint32_t nLengthBits = CxiVlxReadNextValue(&buffer32, lengthEntries, hi4);
		if (nLengthBits == (uint32_t) -1) return 0; //bit pattern not found

		if (nLengthBits == 0) {
			unsigned char bval = CxiVlxWordBufferReadByte(&buffer32);
			if (dest != NULL) dest[outpos] = bval;
			outpos++;

			if (buffer32.error) return 0;
		} else {
			uint32_t copylen = (1 << nLengthBits) + CxiVlxWordBufferReadBits(&buffer32, nLengthBits);
			if (copylen == (uint32_t) -1) return 0; //bit pattern not found
			
			uint32_t nDistBits = CxiVlxReadNextValue(&buffer32, distEntries, lo4);
			uint32_t dist = (1 << nDistBits) + CxiVlxWordBufferReadBits(&buffer32, nDistBits) - 1;
			if (buffer32.error) return 0;

			if (dist > outpos || dist == 0 || copylen > (outlen - outpos)) return 0; //invalid copy

			if (dest != NULL) {
				unsigned char *cpyDest = dest + outpos;
				unsigned char *cpySrc = dest + outpos - dist;
				for (uint32_t i = 0; i < copylen; i++) {
					*(cpyDest++) = *(cpySrc++);
				}
			}
			outpos += copylen;
		}
	}

	//check buffer used, allow up to 3 bytes trailing for 4-byte padding
	if ((buffer32.srcpos + 3) < size) return 0;

	return 1;
}

unsigned char *CxDecompressVlx(const unsigned char *src, unsigned int size, unsigned int *decompressedSize) {
	unsigned int outlen = CxiVlxGetUncompressedSize(src);
	unsigned char *dest = (unsigned char *) malloc(outlen);

	int success = CxiTryDecompressVlx(src, size, dest);
	if (!success) {
		free(dest);
		dest = NULL;
		outlen = 0;
	}

	*decompressedSize = outlen;
	return dest;
}

int CxIsCompressedVlx(const unsigned char *src, unsigned int size) {
	return CxiTryDecompressVlx(src, size, NULL);
}

static CxiLzToken *CxiVlxComputeLzStatistics(const unsigned char *buffer, unsigned int size, unsigned int *lengthCounts, unsigned int *distCounts, int *pnTokens) {
	int tokenBufferSize = 16;
	int nTokens = 0;
	CxiLzToken *tokenBuffer = (CxiLzToken *) calloc(tokenBufferSize, sizeof(CxiLzToken));

	unsigned int nProcessedBytes = 0;
	while (nProcessedBytes < size) {
		unsigned int biggestRun = 0, biggestRunIndex = 0;
		biggestRun = CxiSearchLZ(buffer, size, nProcessedBytes, 1, 0xFFE, 0xFFF, &biggestRunIndex);

		//ensure token buffer capacity
		if ((nTokens + 1) > tokenBufferSize) {
			tokenBufferSize = (tokenBufferSize + 1) * 3 / 2;
			tokenBuffer = (CxiLzToken *) realloc(tokenBuffer, tokenBufferSize * sizeof(CxiLzToken));
		}

		//minimum run length is 2 bytes
		if (biggestRun >= 2) {
			//advance the buffer
			buffer += biggestRun;
			nProcessedBytes += biggestRun;

			//increment copy length bin
			lengthCounts[CxiIlog2(biggestRun)]++;

			//increment copy distance bin
			distCounts[CxiIlog2(biggestRunIndex + 1)]++;

			//append token
			tokenBuffer[nTokens].isReference = 1;
			tokenBuffer[nTokens].length = biggestRun;
			tokenBuffer[nTokens].distance = biggestRunIndex;
		} else {
			nProcessedBytes++;

			//no copy found: increment 0-length bin
			lengthCounts[0]++;

			tokenBuffer[nTokens].isReference = 0;
			tokenBuffer[nTokens].symbol = *(buffer++);
		}
		nTokens++;
	}

	*pnTokens = nTokens;
	return tokenBuffer;
}

static int CxiVlxWriteHuffmanTree(CxiHuffNode *tree, uint16_t *dest, int pos, uint16_t *reps, int *lengths, uint16_t curval, int nBitsVal) {
	if (tree->nRepresent == 1) {
		dest[pos] = (tree->sym << 12) | (curval | (1 << nBitsVal));
		lengths[tree->sym] = nBitsVal;
		reps[tree->sym] = curval;
		return pos + 1;
	} else {
		pos = CxiVlxWriteHuffmanTree(tree->left, dest, pos, reps, lengths, (curval << 1) | 0, nBitsVal + 1);
		pos = CxiVlxWriteHuffmanTree(tree->right, dest, pos, reps, lengths, (curval << 1) | 1, nBitsVal + 1);
		return pos;
	}
}

static void CxiVlxWriteSymbol(BITSTREAM *stream, uint32_t string, int length) {
	for (int i = 0; i < length; i++) {
		CxiBitStreamWrite(stream, (string >> (length - 1 - i)) & 1);
	}
}

static unsigned char *CxiVlxWriteTokenString(CxiLzToken *tokens, int nTokens, unsigned int *lengthCounts, unsigned int *distCounts, unsigned int uncompSize, unsigned int *compressedSize) {
	//write header
	unsigned char header[5] = { 1, 0, 0, 0, 0 };
	header[1] = (uncompSize >> 0) & 0xFF;
	if (uncompSize > 0xFFFF) {
		header[0] = 4;
		header[2] = (uncompSize >> 8) & 0xFF;
		header[3] = (uncompSize >> 16) & 0xFF;
		//decompressor is bugged: only 24-bit size allowed
	} else if (uncompSize > 0xFF) {
		header[0] = 2;
		header[2] = (uncompSize >> 8) & 0xFF;
	}

	//arrange tree
	CxiHuffNode lengthNodes[24] = { 0 }, distNodes[24] = { 0 };
	int nLengthNodes = 0, nDistNodes = 0;
	for (int i = 0; i < 12; i++) {
		if (lengthCounts[i] == 0) continue;

		lengthNodes[nLengthNodes].symMin = lengthNodes[nLengthNodes].symMax = lengthNodes[nLengthNodes].sym = i;
		lengthNodes[nLengthNodes].nRepresent = 1;
		lengthNodes[nLengthNodes].freq = lengthCounts[i];
		nLengthNodes++;
	}
	for (int i = 0; i < 12; i++) {
		if (distCounts[i] == 0) continue;

		distNodes[nDistNodes].symMin = distNodes[nDistNodes].symMax = distNodes[nDistNodes].sym = i;
		distNodes[nDistNodes].nRepresent = 1;
		distNodes[nDistNodes].freq = distCounts[i];
		nDistNodes++;
	}

	//if we would create a tree of 0 size, add a dummy node. The decompressor is not written to
	//handle this case.
	for (int i = 0; i < 12 && nLengthNodes < 2; i++) {
		if (lengthCounts[i] == 0) {
			lengthCounts[i] = 1;
			lengthNodes[nLengthNodes].symMin = lengthNodes[nLengthNodes].symMax = lengthNodes[nLengthNodes].sym = i;
			lengthNodes[nLengthNodes].nRepresent = 1;
			lengthNodes[nLengthNodes].freq = lengthCounts[i];
			nLengthNodes++;
		}
	}
	for (int i = 0; i < 12 && nDistNodes < 2; i++) {
		if (distCounts[i] == 0) {
			distCounts[i] = 1;
			distNodes[nDistNodes].symMin = distNodes[nDistNodes].symMax = distNodes[nDistNodes].sym = i;
			distNodes[nDistNodes].nRepresent = 1;
			distNodes[nDistNodes].freq = distCounts[i];
			nDistNodes++;
		}
	}

	CxiHuffmanConstructTree(lengthNodes, nLengthNodes);
	CxiHuffmanConstructTree(distNodes, nDistNodes);

	//write tree
	uint16_t treeData[24] = { 0 };
	uint8_t treeHeader = (nLengthNodes << 4) | nDistNodes;

	uint16_t lengthReps[12] = { 0 }, distReps[12] = { 0 };
	int lengthLengths[12] = { 0 }, distLengths[12] = { 0 };
	int treeDataSize = CxiVlxWriteHuffmanTree(lengthNodes, treeData, 0, lengthReps, lengthLengths, 0, 0);
	treeDataSize = CxiVlxWriteHuffmanTree(distNodes, treeData, treeDataSize, distReps, distLengths, 0, 0);

	//write tokens
	BITSTREAM stream;
	CxiBitStreamCreate(&stream);
	for (int i = 0; i < nTokens; i++) {
		CxiLzToken *token = tokens + i;
		if (!token->isReference) {
			//write 0-length token
			CxiVlxWriteSymbol(&stream, lengthReps[0], lengthLengths[0]);

			//write byte
			CxiVlxWriteSymbol(&stream, token->symbol, 8);
		} else {
			//else write reference
			int nBitsLength = CxiIlog2(token->length);
			CxiVlxWriteSymbol(&stream, lengthReps[nBitsLength], lengthLengths[nBitsLength]);
			CxiVlxWriteSymbol(&stream, token->length - (1 << nBitsLength), nBitsLength);

			int nBitsDistance = CxiIlog2(token->distance + 1);
			CxiVlxWriteSymbol(&stream, distReps[nBitsDistance], distLengths[nBitsDistance]);
			CxiVlxWriteSymbol(&stream, (token->distance + 1) - (1 << nBitsDistance), nBitsDistance);
		}
	}

	//write ending 24-bit dummy string
	CxiVlxWriteSymbol(&stream, 0, 24);

	//last: switch endianness of stream (uses big endian)
	for (int i = 0; i < stream.nWords; i++) {
		uint32_t w = stream.bits[i];
		w = ((w & 0xFF) << 24) | ((w & 0xFF00) << 8) | ((w & 0xFF0000) >> 8) | ((w & 0xFF000000) >> 24);
		stream.bits[i] = w;
	}

	unsigned int outsize = 1 + header[0] + treeDataSize * sizeof(uint16_t) + stream.nWords * 4;
	outsize = (outsize + 3) & ~3;

	unsigned char *outbuf = (unsigned char *) calloc(outsize, 1);
	unsigned char *pos = outbuf;
	memcpy(pos, header, header[0] + 1);
	pos += header[0] + 1;
	memcpy(pos, &treeHeader, sizeof(treeHeader));
	pos += sizeof(treeHeader);
	memcpy(pos, treeData, treeDataSize * sizeof(uint16_t));
	pos += treeDataSize * sizeof(uint16_t);
	memcpy(pos, stream.bits, stream.nWords * 4);
	CxiBitStreamFree(&stream);

	*compressedSize = outsize;
	return outbuf;
}

unsigned char *CxCompressVlx(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//compute histogram of LZ lengths and distances
	unsigned int lengthCounts[12] = { 0 }, distCounts[12] = { 0 };
	int nTokens;
	CxiLzToken *tokens = CxiVlxComputeLzStatistics(buffer, size, lengthCounts, distCounts, &nTokens);
	
	unsigned char *out = CxiVlxWriteTokenString(tokens, nTokens, lengthCounts, distCounts, size, compressedSize);
	free(tokens);
	return out;
}




int CxGetCompressionType(const unsigned char *buffer, unsigned int size) {
	if (CxIsFilteredLZHeader(buffer, size)) return COMPRESSION_LZ77_HEADER;
	if (CxIsCompressedLZ(buffer, size)) return COMPRESSION_LZ77;
	if (CxIsCompressedLZX(buffer, size)) return COMPRESSION_LZ11;
	if (CxIsCompressedLZXComp(buffer, size)) return COMPRESSION_LZ11_COMP_HEADER;
	if (CxIsCompressedRL(buffer, size)) return COMPRESSION_RLE;
	if (CxIsCompressedHuffman4(buffer, size)) return COMPRESSION_HUFFMAN_4;
	if (CxIsCompressedHuffman8(buffer, size)) return COMPRESSION_HUFFMAN_8;
	if (CxIsFilteredDiff8(buffer, size)) return COMPRESSION_DIFF8;
	if (CxIsFilteredDiff16(buffer, size)) return COMPRESSION_DIFF16;
	if (CxIsCompressedMvDK(buffer, size)) return COMPRESSION_MVDK;
	if (CxIsCompressedVlx(buffer, size)) return COMPRESSION_VLX;

	return COMPRESSION_NONE;
}

unsigned char *CxDecompress(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	int type = CxGetCompressionType(buffer, size);
	switch (type) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*uncompressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return CxDecompressLZ(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11:
			return CxDecompressLZX(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return CxDecompressLZXComp(buffer, size, uncompressedSize);
		case COMPRESSION_HUFFMAN_4:
		case COMPRESSION_HUFFMAN_8:
			return CxDecompressHuffman(buffer, size, uncompressedSize);
		case COMPRESSION_LZ77_HEADER:
			return CxDecompressLZHeader(buffer, size, uncompressedSize);
		case COMPRESSION_RLE:
			return CxDecompressRL(buffer, size, uncompressedSize);
		case COMPRESSION_DIFF8:
			return CxUnfilterDiff8(buffer, size, uncompressedSize);
		case COMPRESSION_DIFF16:
			return CxUnfilterDiff16(buffer, size, uncompressedSize);
		case COMPRESSION_MVDK:
			return CxDecompressMvDK(buffer, size, uncompressedSize);
		case COMPRESSION_VLX:
			return CxDecompressVlx(buffer, size, uncompressedSize);
	}
	return NULL;
}

unsigned char *CxCompress(const unsigned char *buffer, unsigned int size, int compression, unsigned int *compressedSize) {
	switch (compression) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*compressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return CxCompressLZ(buffer, size, compressedSize);
		case COMPRESSION_LZ11:
			return CxCompressLZX(buffer, size, compressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return CxCompressLZXComp(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_4:
			return CxCompressHuffman4(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_8:
			return CxCompressHuffman8(buffer, size, compressedSize);
		case COMPRESSION_LZ77_HEADER:
			return CxCompressLZHeader(buffer, size, compressedSize);
		case COMPRESSION_RLE:
			return CxCompressRL(buffer, size, compressedSize);
		case COMPRESSION_DIFF8:
			return CxFilterDiff8(buffer, size, compressedSize);
		case COMPRESSION_DIFF16:
			return CxFilterDiff16(buffer, size, compressedSize);
		case COMPRESSION_MVDK:
			return CxCompressMvDK(buffer, size, compressedSize);
		case COMPRESSION_VLX:
			return CxCompressVlx(buffer, size, compressedSize);
	}
	return NULL;
}