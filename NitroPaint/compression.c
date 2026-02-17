#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "compression.h"
#include "bstream.h"
#include "struct.h"

#ifdef _MSC_VER
#define inline __inline
#endif

static void *CxiShrink(void *block, unsigned int to) {
	void *newblock = realloc(block, to);
	if (newblock == NULL) {
		//alloc fail, return old block
		return block;
	}
	return newblock;
}

// ----- Common LZ subroutines

//struct for mapping an LZ graph
typedef struct CxiLzNode_ {
	uint32_t distance : 15;    // distance of node if reference
	uint32_t length   : 17;    // length of node
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

//struct for keeping track of LZ sliding window
typedef struct CxiLzState_ {
	const unsigned char *buffer;
	unsigned int size;
	unsigned int pos;
	unsigned int minLength;
	unsigned int maxLength;
	unsigned int minDistance;
	unsigned int maxDistance;
	unsigned int symLookup[512];
	unsigned int *chain;
} CxiLzState;

static unsigned int CxiLzHash3(const unsigned char *p) {
	unsigned char c0 = p[0];         // A
	unsigned char c1 = p[0] ^ p[1];  // A ^ B
	unsigned char c2 = p[0] ^ p[2];  // (A ^ B) ^ (B ^ C)
	return (c0 ^ (c1 << 1) ^ (c2 << 2) ^ (c2 >> 7)) & 0x1FF;
}

static void CxiLzStateInit(CxiLzState *state, const unsigned char *buffer, unsigned int size, unsigned int minLength, unsigned int maxLength, unsigned int minDistance, unsigned int maxDistance) {
	state->buffer = buffer;
	state->size = size;
	state->pos = 0;
	state->minLength = minLength;
	state->maxLength = maxLength;
	state->minDistance = minDistance;
	state->maxDistance = maxDistance;

	for (unsigned int i = 0; i < 512; i++) {
		//init symbol lookup to empty
		state->symLookup[i] = UINT_MAX;
	}

	state->chain = (unsigned int *) calloc(state->maxDistance, sizeof(unsigned int));
	for (unsigned int i = 0; i < state->maxDistance; i++) {
		state->chain[i] = UINT_MAX;
	}
}

static void CxiLzStateFree(CxiLzState *state) {
	free(state->chain);
}

static unsigned int CxiLzStateGetChainIndex(CxiLzState *state, unsigned int index) {
	return (state->pos - index) % state->maxDistance;
}

static unsigned int CxiLzStateGetChain(CxiLzState *state, int index) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	return state->chain[chainIndex];
}

static void CxiLzStatePutChain(CxiLzState *state, unsigned int index, unsigned int data) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	state->chain[chainIndex] = data;
}

static void CxiLzStateSlideByte(CxiLzState *state) {
	if (state->pos >= state->size) return; // cannot slide

	//only update search structures when we have enough space left to necessitate searching.
	if ((state->size - state->pos) >= 3) {
		//fetch next 3 bytes' hash
		unsigned int next = CxiLzHash3(state->buffer + state->pos);

		//get the distance back to the next byte before sliding. If it exists in the window,
		//we'll have nextDelta less than UINT_MAX. We'll take this first occurrence and it 
		//becomes the offset from the current byte. Bear in mind the chain is 0-indexed starting
		//at a distance of 1. 
		unsigned int nextDelta = state->symLookup[next];
		if (nextDelta != UINT_MAX) {
			nextDelta++;
			if (nextDelta >= state->maxDistance) {
				nextDelta = UINT_MAX;
			}
		}
		CxiLzStatePutChain(state, 0, nextDelta);

		//increment symbol lookups
		for (int i = 0; i < 512; i++) {
			if (state->symLookup[i] != UINT_MAX) {
				state->symLookup[i]++;
				if (state->symLookup[i] > state->maxDistance) state->symLookup[i] = UINT_MAX;
			}
		}
		state->symLookup[next] = 0; // update entry for the current byte to the start of the chain
	}

	state->pos++;
}

static void CxiLzStateSlide(CxiLzState *state, unsigned int nSlide) {
	while (nSlide--) CxiLzStateSlideByte(state);
}

static unsigned int CxiCompareMemory(const unsigned char *b1, const unsigned char *b2, unsigned int nMax) {
	//compare nAbsoluteMax bytes, do not perform any looping.
	unsigned int nSame = 0;
	while (nMax > 0) {
		if (*(b1++) != *(b2++)) break;
		nMax--;
		nSame++;
	}
	return nSame;
}

static int CxiLzConfirmMatch(const unsigned char *buffer, unsigned int size, unsigned int pos, unsigned int distance, unsigned int length) {
	(void) size;

	//compare string match
	return memcmp(buffer + pos, buffer + pos - distance, length) == 0;
}

static unsigned int CxiLzSearch(CxiLzState *state, unsigned int *pDistance) {
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < 3 || nBytesLeft < state->minLength) {
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check only if distance is at least minDistance
		if (distance >= state->minDistance) {
			unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);

			if (matchLen > bestLength) {
				bestLength = matchLen;
				bestDistance = distance;
				if (bestLength == nMaxCompare) break;
			}
		}

		if (distance == state->maxDistance) break;
		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;
		distance += next;
	}

	if (bestLength < state->minLength) {
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}

static unsigned int CxiSearchLZ(const unsigned char *buffer, unsigned int size, unsigned int curpos, unsigned int minDistance, unsigned int maxDistance, unsigned int maxLength, unsigned int *pDistance) {
	//nProcessedBytes = curpos
	unsigned int nBytesLeft = size - curpos;

	//the maximum distance we can search backwards is limited by how far into the buffer we are. It won't
	//make sense to a decoder to copy bytes from before we've started.
	if (maxDistance > curpos) maxDistance = curpos;

	//the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//begin searching backwards.
	unsigned int bestLength = 0, bestDistance = 0;
	for (unsigned int i = minDistance; i <= maxDistance; i++) {
		unsigned int nMatched = CxiCompareMemory(buffer + curpos - i, buffer + curpos, nMaxCompare);
		if (nMatched > bestLength) {
			bestLength = nMatched;
			bestDistance = i;
			if (bestLength == nMaxCompare) break;
		}
	}

	*pDistance = bestDistance;
	return bestLength;
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
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, LZ_MIN_LENGTH, LZ_MAX_LENGTH, LZ_MIN_SAFE_DISTANCE, LZ_MAX_DISTANCE);

	//create node list and fill in the maximum string reference sizes
	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
	unsigned int pos = 0;
	while (pos < size) {
		unsigned int dst;
		unsigned int len = CxiLzSearch(&state, &dst);

		//store longest found match
		nodes[pos].length = len;
		nodes[pos].distance = dst;

		pos++;
		CxiLzStateSlide(&state, 1);
	}
	CxiLzStateFree(&state);

	//work backwards from the end of file
	pos = size;
	while (pos--) {
		//get node at pos
		CxiLzNode *node = nodes + pos;

		//search for largest LZ string match
		unsigned int len = nodes[pos].length;
		unsigned int dist = nodes[pos].distance;

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
	return CxiShrink(buf, outSize); //reduce buffer size
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
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, LZX_MIN_LENGTH, LZX_MAX_LENGTH_3, LZX_MIN_SAFE_DISTANCE, LZX_MAX_DISTANCE);

	//create node list and fill in the maximum string reference sizes
	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
	unsigned int pos = 0;
	while (pos < size) {
		unsigned int dst;
		unsigned int len = CxiLzSearch(&state, &dst);

		//store longest found match
		nodes[pos].length = len;
		nodes[pos].distance = dst;

		pos++;
		CxiLzStateSlide(&state, 1);
	}
	CxiLzStateFree(&state);

	//work backwards from the end of file
	pos = size;
	while (pos--) {
		//get node at pos
		CxiLzNode *node = nodes + pos;

		//read out longest match
		unsigned int len = node->length;
		unsigned int dist = node->distance;

		//if node takes us to the end of file, set weight to cost of this node.
		if ((pos + len) == size) {
			//token takes us to the end of the file, its weight equals this token cost.
			node->length = len;
			node->distance = dist;
			node->weight = CxiLzxTokenCost(len);
		} else {
			//else, search LZ matches from here down.
			unsigned int weightBest = UINT_MAX;
			unsigned int lenBest = 1;
			while (len) {
				//measure cost
				unsigned int weightNext = nodes[pos + len].weight;
				unsigned int weight = CxiLzxTokenCost(len) + weightNext;
				if (weight < weightBest) {
					lenBest = len;
					weightBest = weight;
				}

				//decrement length w.r.t. length discontinuity
				len--;
				if (len != 0 && len < LZX_MIN_LENGTH) len = 1;
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
	*(uint32_t *) (bufpos) = (size << 8) | 0x11;
	bufpos += 4;

	CxiLzNode *curnode = &nodes[0];

	unsigned int srcpos = 0;
	while (srcpos < size) {
		uint8_t head = 0;
		unsigned char *headpos = bufpos++;

		for (unsigned int i = 0; i < 8 && srcpos < size; i++) {
			unsigned int length = curnode->length;
			unsigned int distance = curnode->distance;

			if (CxiLzxNodeIsReference(curnode)) {
				//node is reference
				head |= 1 << (7 - i);

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

typedef struct CxiBitStream_ {
	uint32_t *bits;
	int nWords;
	int nBitsInLastWord;
	int nWordsAlloc;
	int length;
} CxiBitStream;

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

typedef struct CxiRlNode_ {
	uint32_t weight : 31; // weight of this node
	uint32_t isRun  :  1; // is node compressed run
	uint8_t length  :  8; // length of node in bytes
} CxiRlNode;

static unsigned int CxiFindRlRun(const unsigned char *buffer, unsigned int size, unsigned int maxSize) {
	if (maxSize > size) maxSize = size;
	if (maxSize == 0) return 0;

	unsigned char first = buffer[0];
	for (unsigned int i = 1; i < maxSize; i++) {
		if (buffer[i] != first) return i;
	}
	return maxSize;
}

unsigned char *CxCompressRL(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//construct a graph
	CxiRlNode *nodes = (CxiRlNode *) calloc(size, sizeof(CxiRlNode));

	unsigned int pos = size;
	while (pos--) {
		CxiRlNode *node = nodes + pos;

		//find longest run up to 130 bytes
		unsigned int runLength = CxiFindRlRun(buffer + pos, size - pos, 130);
		
		unsigned int bestLength = 1, bestCost = UINT_MAX, bestRun = 0;
		if (runLength >= 3) {
			//meets threshold, explore run lengths.
			unsigned int tmpLength = runLength;
			bestRun = 1;
			while (tmpLength >= 3) {
				unsigned int cost = 2;
				if ((pos + tmpLength) < size) cost += nodes[pos + tmpLength].weight;

				if (cost < bestCost) {
					bestCost = cost;
					bestLength = tmpLength;
				}

				tmpLength--;
			}
		}

		//explore cost of storing a byte run
		unsigned int tmpLength = 0x80;
		if ((pos + tmpLength) > size) tmpLength = size - pos;
		while (tmpLength >= 1) {
			unsigned int cost = (1 + tmpLength);
			if ((pos + tmpLength) < size) cost += nodes[pos + tmpLength].weight;

			if (cost < bestCost) {
				bestCost = cost;
				bestLength = tmpLength;
				bestRun = 0; // best is not a run
			}
			tmpLength--;
		}

		//put best
		node->weight = bestCost;
		node->length = bestLength;
		node->isRun = bestRun;
	}

	//produce RL encoding
	pos = 0;
	unsigned int outLength = 4;
	while (pos < size) {
		CxiRlNode *node = nodes + pos;

		if (node->isRun) outLength += 2;
		else             outLength += 1 + node->length;
		pos += node->length;
	}

	unsigned char *out = (unsigned char *) calloc(outLength, 1);
	*(uint32_t *) out = 0x30 | (size << 8);

	pos = 0;
	unsigned int outpos = 4;
	while (pos < size) {
		CxiRlNode *node = nodes + pos;

		if (node->isRun) {
			out[outpos++] = 0x80 | (node->length - 3);
			out[outpos++] = buffer[pos];
		} else {
			out[outpos++] = 0x00 | (node->length - 1);
			memcpy(out + outpos, buffer + pos, node->length);
			outpos += node->length;
		}
		pos += node->length;
	}

	free(nodes);

	*compressedSize = outLength;
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



static void CxiBitStreamCreate(CxiBitStream *stream) {
	stream->nWords = 0;
	stream->length = 0;
	stream->nBitsInLastWord = 32;
	stream->nWordsAlloc = 16;
	stream->bits = (uint32_t *) calloc(stream->nWordsAlloc, 4);
}

static void CxiBitStreamFree(CxiBitStream *stream) {
	free(stream->bits);
}

static void CxiBitStreamWrite(CxiBitStream *stream, int bit) {
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

static void *CxiBitStreamGetBytes(CxiBitStream *stream, int wordAlign, int beBytes, int beBits, unsigned int *size) {
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

static void CxiBitStreamWriteBits(CxiBitStream *stream, uint32_t bits, int nBits) {
	for (int i = 0; i < nBits; i++) CxiBitStreamWrite(stream, (bits >> i) & 1);
}

static void CxiBitStreamWriteBitsBE(CxiBitStream *stream, uint32_t bits, int nBits) {
	for (int i = 0; i < nBits; i++) CxiBitStreamWrite(stream, (bits >> (nBits - 1 - i)) & 1);
}

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)

static int CxiHuffmanNodeComparator(const void *p1, const void *p2) {
	return ((CxiHuffNode *) p2)->freq - ((CxiHuffNode *) p1)->freq;
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

static void CxiHuffmanWriteSymbol(CxiBitStream *bits, uint16_t sym, CxiHuffNode *tree) {
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


// ----- Standard Huffman routines

typedef struct CxiHuffTreeCode_ {
	uint8_t value;
	uint8_t leaf;
	uint8_t lrbit : 7;
} CxiHuffTreeCode;

static uint8_t CxiHuffmanGetFlagForNode(CxiHuffNode *root) {
	CxiHuffNode *left = root->left;
	CxiHuffNode *right = root->right;

	return (ISLEAF(left) << 1) | (ISLEAF(right) << 0);
}

static CxiHuffTreeCode *CxiHuffmanCreateTreeCode(CxiHuffNode *root, CxiHuffTreeCode *treeCode) {
	CxiHuffNode *left = root->left;
	CxiHuffNode *right = root->right;

	CxiHuffTreeCode *base = treeCode;

	//left node
	{
		if (ISLEAF(left)) {
			base[0].value = (uint8_t) left->sym;
			base[0].leaf = 1;
		} else {
			uint8_t flag = CxiHuffmanGetFlagForNode(left);

			CxiHuffTreeCode *wr = treeCode + 2;
			treeCode = CxiHuffmanCreateTreeCode(left, wr);

			base[0].value = ((wr - base - 2) / 2);
			base[0].lrbit = flag;
			base[0].leaf = 0;
		}
	}

	//right node
	{
		if (ISLEAF(right)) {
			base[1].value = (uint8_t) right->sym;
			base[1].leaf = 1;
		} else {
			uint8_t flag = CxiHuffmanGetFlagForNode(right);

			CxiHuffTreeCode *wr = treeCode + 2;
			treeCode = CxiHuffmanCreateTreeCode(right, wr);

			base[1].value = ((wr - base - 2) / 2);
			base[1].lrbit = flag;
			base[1].leaf = 0;
		}
	}
	return treeCode;
}

static void CxiHuffmanCheckTree(CxiHuffTreeCode *treeCode, int nNode) {
	for (int i = 2; i < nNode; i++) {
		if (treeCode[i].leaf) continue;

		//check node distance out of range
		if (treeCode[i].value <= 0x3F) continue;

		int slideDst = 1;
		if (treeCode[i ^ 1].value == 0x3F) {
			//other node in pair is at maximum distance
			i ^= 1;
		} else {
			//required slide distnace to bring node in range
			slideDst = treeCode[i].value - 0x3F;
		}

		int slideMax = (i >> 1) + treeCode[i].value + 1;
		int slideMin = slideMax - slideDst;

		//move node back and rotate node pair range forward one position
		CxiHuffTreeCode cpy[2];
		memcpy(cpy, &treeCode[(slideMax << 1)], sizeof(cpy));
		memmove(&treeCode[(slideMin + 1) << 1], &treeCode[(slideMin + 0) << 1], 2 * slideDst * sizeof(CxiHuffTreeCode));
		memcpy(&treeCode[(slideMin << 1)], cpy, sizeof(cpy));

		//update node references to rotated range
		treeCode[i].value -= slideDst;

		//if the moved node pair is branch nodes, adjust outgoing references
		if (!treeCode[(slideMin << 1) + 0].leaf) treeCode[(slideMin << 1) + 0].value += slideDst;
		if (!treeCode[(slideMin << 1) + 1].leaf) treeCode[(slideMin << 1) + 1].value += slideDst;

		for (int j = i + 1; j < (slideMin << 1); j++) {
			if (treeCode[j].leaf) continue;

			//increment node values referring to slid nodes
			int refb = (j >> 1) + treeCode[j].value + 1;
			if ((refb >= slideMin) && (refb < slideMax)) treeCode[j].value++;
		}

		for (int j = (slideMin + 1) << 1; j < ((slideMax + 1) << 1); j++) {
			if (treeCode[j].leaf) continue;

			//adjust outgoing references from slid nodes
			int refb = (j >> 1) + treeCode[j].value + 1;
			if (refb > slideMax) treeCode[j].value--;
		}

		//continue again from start of this node pair
		i &= ~1;
		i--;
	}
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
			nodes[(buffer[i] >> 0) & 0xF].freq++;
			nodes[(buffer[i] >> 4) & 0xF].freq++;
		}
	}

	//count nodes
	int nLeaf = 0;
	for (int i = 0; i < nSym; i++) {
		if (nodes[i].freq) nLeaf++;
	}
	if (nLeaf < 2) {
		//insert dummy nodes
		for (int i = 0; i < nSym && nLeaf < 2; i++) {
			if (nodes[i].freq == 0) nodes[i].freq = 1;
		}
	}

	//build Huffman tree
	CxiHuffmanConstructTree(nodes, nSym);

	//construct Huffman tree encoding
	CxiHuffTreeCode treeCode[512] = { 0 };
	treeCode[0].value = ((nLeaf + 1) & ~1) - 1;
	treeCode[0].lrbit = 0;
	treeCode[1].value = 0;
	treeCode[1].lrbit = CxiHuffmanGetFlagForNode(nodes);
	CxiHuffmanCreateTreeCode(nodes, treeCode + 2);
	CxiHuffmanCheckTree(treeCode, nLeaf * 2);

	//now write bits out.
	CxiBitStream stream;
	CxiBitStreamCreate(&stream);
	if (nBits == 8) {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, buffer[i], nodes);
		}
	} else {
		for (unsigned int i = 0; i < size; i++) {
			CxiHuffmanWriteSymbol(&stream, (buffer[i] >> 0) & 0xF, nodes);
			CxiHuffmanWriteSymbol(&stream, (buffer[i] >> 4) & 0xF, nodes);
		}
	}

	//create output bytes
	unsigned int treeSize = (nLeaf * 2 + 3) & ~3;
	unsigned int outSize = 4 + treeSize + stream.nWords * 4;
	unsigned char *finbuf = (unsigned char *) malloc(outSize);
	*(uint32_t *) finbuf = 0x20 | nBits | (size << 8);

	for (int i = 0; i < nLeaf * 2; i++) {
		finbuf[4 + i] = treeCode[i].value | (treeCode[i].leaf ? 0 : (treeCode[i].lrbit << 6));
	}

	memcpy(finbuf + 4 + treeSize, stream.bits, stream.nWords * 4);
	free(nodes);
	CxiBitStreamFree(&stream);

	*compressedSize = outSize;
	return finbuf;
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


// ----- LZX COMP Routines

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



// ----- MvDK Routines

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
	uint8_t beBits;
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


unsigned char CxiReverseByte(unsigned char x) {
	unsigned char out = 0;
	for (int i = 0; i < 8; i++) out |= ((x >> i) & 1) << (7 - i);
	return out;
}

void CxiInitBitReader(BIT_READER_8 *reader, const unsigned char *pos, const unsigned char *end, int beBits) {
	reader->pos = pos;
	reader->end = end;
	reader->start = pos;
	reader->beBits = beBits;
	reader->nBitsBuffered = 8;
	reader->nBitsRead = 0;
	reader->current = 0;
	reader->error = 0;
	if (pos < end) reader->current = *pos;
	if (reader->beBits) reader->current = CxiReverseByte(reader->current);
}

uint32_t CxiConsumeBit(BIT_READER_8 *reader) {
	if (reader->pos >= reader->end) {
		//error
		reader->error = 1;
		return 0;
	}

	unsigned char byteVal = reader->current;
	reader->nBitsBuffered--;
	reader->nBitsRead++;

	if (reader->nBitsBuffered > 0) {
		reader->current >>= 1;
	} else {
		reader->pos++;
		if (reader->pos < reader->end) {
			reader->nBitsBuffered = 8;
			reader->current = *reader->pos;
			if (reader->beBits) reader->current = CxiReverseByte(reader->current);
		}
	}

	return byteVal & 1;
}

uint32_t CxiConsumeBits(BIT_READER_8 *bitReader, unsigned int nBits) {
	uint32_t string = 0, i = 0;
	for (i = 0; i < nBits; i++) {
		if (bitReader->pos >= bitReader->end) {
			//error
			bitReader->error = 1;
			return string;
		}

		bitReader->nBitsBuffered--;
		bitReader->nBitsRead++;
		if (bitReader->beBits) {
			string <<= 1;
			string |= (bitReader->current & 1);
		} else {
			string |= (bitReader->current & 1) << i;
		}

		if (bitReader->nBitsBuffered > 0) {
			bitReader->current >>= 1;
		} else {
			bitReader->pos++;
			if (bitReader->pos < bitReader->end) {
				bitReader->nBitsBuffered = 8;
				bitReader->current = *bitReader->pos;
				if (bitReader->beBits) bitReader->current = CxiReverseByte(bitReader->current);
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
	CxiInitBitReader(&reader, pos, srcEnd, 0);

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
		if (huffRoot1 == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL; // Validate tree size

		//Reposition stream after the Huffman tree. Read out the LZ distance tree next.
		//Its size in bits is given by the following 16 bits from the stream.
		CxiInitBitReader(&reader, postTree, srcEnd, 0);
		reader.nBitsRead = (postTree - pos) * 8;
		lzLen2 = CxiConsumeBits(&reader, 16);
		uint32_t table2SizeBytes = (lzLen2 + 7) >> 3;

		postTree = reader.pos + table2SizeBytes;
		DEFLATE_TREE_NODE *huffDistancesRoot = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->lengthNodeBuffer, 0x1E);
		if (huffDistancesRoot == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL;         // Validate tree size

		//Reposition stream after this tree to prepare for reading the compressed sequence.
		CxiInitBitReader(&reader, postTree, srcEnd, 0);
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

	//check bytes unconsumed (Nintendo's encoder sometimes adds 4 bytes? uncompressed block indicator?)
	if ((nConsumed + 4) < ((size + 3) & ~3)) return 0;

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
	StList tokenBuffer;
	StStatus s = StListCreateInline(&tokenBuffer, CxiLzToken, NULL);
	if (!ST_SUCCEEDED(s)) return NULL;

	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 3, 0x102, 1, 0x8000);

	unsigned int curpos = 0;
	while (curpos < size) {
		//search backwards
		unsigned int distance;
		unsigned int length = CxiLzSearch(&state, &distance);

		CxiLzToken token;
		if (length >= 3) {
			//write LZ reference
			token.isReference = 1;
			token.distance = distance;
			token.length = length;

			curpos += length;
		} else {
			//write byte literal
			token.isReference = 0;
			token.symbol = buffer[curpos++];
		}

		s = StListAdd(&tokenBuffer, &token);
		if (!ST_SUCCEEDED(s)) goto Error;

		CxiLzStateSlide(&state, length);
	}
	CxiLzStateFree(&state);

	*pnTokens = tokenBuffer.length;
	return (CxiLzToken *) tokenBuffer.buffer;

Error:
	CxiLzStateFree(&state);
	StListFree(&tokenBuffer);
	return NULL;
}


typedef struct CxiHuffmanCode_ {
	uint32_t encoding;
	uint16_t value;
	uint16_t length;
} CxiHuffmanCode;

static int CxiMvdkLookupDeflateTableEntry(const DEFLATE_TABLE_ENTRY *table, int tableSize, unsigned int n) {
	for (int i = tableSize - 1; i >= 0; i--) {
		if (n >= table[i].majorPart) return i;
	}
	return 0;
}

static unsigned int CxiMvdkGetLengthCost(unsigned int length, CxiHuffmanCode *symCodes) {
	//get length node from table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3);

	//compute cost
	unsigned int cost = 0;
	cost += sDeflateLengthTable[idx].nMinorBits; // number of directly stored least significant bits
	cost += symCodes[idx + 0x100].length;        // number of bits to store the Huffman code for most significant bits
	return cost;
}

static unsigned int CxiMvdkGetDistanceCost(unsigned int distance, CxiHuffmanCode *distCodes) {
	//get distance node from table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateOffsetTable, 30, distance - 1);
	
	unsigned int cost = 0;
	cost += sDeflateOffsetTable[idx].nMinorBits; // number of directly stored least significant bits
	cost += distCodes[idx].length;               // number of bits to store the Huffman code for most significant bits
	return cost;
}

static unsigned int CxiMvdkGetByteCost(unsigned int symbol, CxiHuffmanCode *symCodes) {
	//return cost as direct stored symbol
	return symCodes[symbol].length;
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

static void CxiMvdkWriteHuffmanTree(CxiBitStream *stream, CxiHuffmanCode *codes, int nCodes) {
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

static void CxiMvdkCreateHuffmanTree(CxiLzToken *tokens, int nTokens, CxiHuffNode **pSymbolTree, CxiHuffNode **pOffsetTree) {
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

	*pSymbolTree = symbolTree;
	*pOffsetTree = offsetTree;
}

static int CxiMvdkIsLengthAvailable(unsigned int length, CxiHuffmanCode *encLengths) {
	if (length == 1) return 1; // 1: direct byte (always available)
	if (length == 2) return 0; // 2: never available

	//get index of table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3);

	//nonzero length indicates it is within our Huffman code table
	return encLengths[0x100 + idx].length != 0;
}

static unsigned int CxiMvdkRoundDownLength(unsigned int length, CxiHuffmanCode *encLengths) {
	if (length == 1 || length == 2) return 1;
	if (length < 3) return 0;

	//get index of table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3);
	if (encLengths[0x100 + idx].length != 0) return length; // in encoding table

	//else
	while (1) {
		idx--;
		if (idx < 0) return 0;

		if (encLengths[0x100 + idx].length != 0) {
			return (sDeflateLengthTable[idx].majorPart + (1 << sDeflateLengthTable[idx].nMinorBits) - 1) + 3;
		}
	}
}

static unsigned int CxiMvdkGetDistanceTableMax(int idx) {
	const DEFLATE_TABLE_ENTRY *entry = &sDeflateOffsetTable[idx];
	return (entry->majorPart + ((1 << entry->nMinorBits) - 1)) + 1;
}

static unsigned int CxiMvdkSearchLZzestrictedFast(CxiLzState *state, CxiHuffmanCode *distanceCodes, unsigned int *pDistance) {
	//nProcessedBytes = curpos
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < state->minLength || nBytesLeft < 3) {
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[CxiLzHash3(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	//the longest string we can match, including repetition by overwriting the source.
	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//begin searching backwards.
	int curDeflateIndex = 29;
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check only if distance is at least minDistance
		if (distance >= state->minDistance) {
			//run down index into deflate table
			while (curDeflateIndex >= 0 && distance > CxiMvdkGetDistanceTableMax(curDeflateIndex)) curDeflateIndex--;
			if (curDeflateIndex == -1) break;

			if (distanceCodes[curDeflateIndex].length > 0) {
				unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);

				if (matchLen > bestLength) {
					bestLength = matchLen;
					bestDistance = distance;
					if (bestLength == nMaxCompare) break;
				}
			}
		}

		if (distance == state->maxDistance) break;
		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;
		distance += next;
	}

	if (bestLength < state->minLength) {
		bestLength = 1;
		bestDistance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}

static CxiLzToken *CxiMvdkRetokenize(const unsigned char *buffer, unsigned int size, int *pnTokens, CxiHuffNode *symbolTree, CxiHuffNode *offsetTree) {
	//create canonical tree and get encodings
	CxiHuffmanCode *lengthEncodings = (CxiHuffmanCode *) calloc(0x100 + 29, sizeof(CxiHuffmanCode));
	CxiHuffmanCode *offsetEncodings = (CxiHuffmanCode *) calloc(30, sizeof(CxiHuffmanCode));
	CxiMvdkMakeCanonicalTree(symbolTree, lengthEncodings, 0x100 + 29);
	CxiMvdkMakeCanonicalTree(offsetTree, offsetEncodings, 30);

	//count available length nodes
	int nLenNodesAvailable = 0;
	for (int i = 0x100; i < 0x100 + 29; i++) {
		if (lengthEncodings[i].length == 0) continue;
		nLenNodesAvailable++;
	}
	
	//calculate minimal cost of a distance token.
	unsigned int minDstCost = UINT_MAX;
	for (int i = 0; i < 30; i++) {
		if (offsetEncodings[i].length) {
			unsigned int cost = offsetEncodings[i].length;
			cost += sDeflateOffsetTable[offsetEncodings[i].value].nMinorBits;

			if (cost < minDstCost) {
				minDstCost = cost;
			}
		}
	}

	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 3, 0x102, 1, 0x8000);

	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
	unsigned int pos = 0;
	while (pos < size) {
		unsigned int distance;
		unsigned int length = CxiMvdkSearchLZzestrictedFast(&state, offsetEncodings, &distance);

		nodes[pos].length = length;
		nodes[pos].distance = distance;

		pos++;
		CxiLzStateSlide(&state, 1);
	}
	CxiLzStateFree(&state);

	pos = size;
	while (pos-- > 0) {
		//search backwards
		unsigned int length = nodes[pos].length;
		unsigned int distance = nodes[pos].distance;

		//check: length must be in the allowed lengths list.
		int lengthIndex = -1;
		if (length >= 3) {
			//round down length to an encodable length
			length = CxiMvdkRoundDownLength(length, lengthEncodings);
		}

		//NOTE: all byte values that appear in the file will have a symbol associated since they must appear at least once.
		//thus we do not need to check that any byte value exists.

		//check length (should store reference?)
		unsigned int weight = 0;
		if (length < 3) {
			//byte literal (can't go lower)
			length = 1;

			//compute cost of byte literal
			weight = CxiMvdkGetByteCost(buffer[pos], lengthEncodings);
			if ((pos + 1) < size) {
				//add next weight
				weight += nodes[pos + 1].weight;
			}
		} else {
			//get cost of selected distance
			unsigned int dstCost = CxiMvdkGetDistanceCost(distance, offsetEncodings);

			//scan size down
			unsigned int weightBest = UINT_MAX, lengthBest = length;
			while (length) {
				//skip unavailable lengths
				if (length != 1 && !CxiMvdkIsLengthAvailable(length, lengthEncodings)) {
					length--;
					continue;
				}

				unsigned int thisWeight;

				//compute weight of this length value
				unsigned int thisLengthWeight;
				if (length > 1) {
					//length > 1: symbol (use cost of length symbol)
					thisLengthWeight = CxiMvdkGetLengthCost(length, lengthEncodings);
				} else {
					//length = 1: byte literal (use cost of byte literal)
					thisLengthWeight = CxiMvdkGetByteCost(buffer[pos], lengthEncodings);
				}

				//takes us to end of file? 
				if ((pos + length) == size) {
					//cost is just this node's weight
					thisWeight = thisLengthWeight;
				} else {
					//cost is this node's weight plus the weight of the next node
					CxiLzNode *next = nodes + pos + length;
					thisWeight = thisLengthWeight + next->weight;
				}
				if (thisWeight <= weightBest || (length == 1 && thisWeight <= (weightBest + minDstCost))) {
					weightBest = thisWeight;
					lengthBest = length;
				}

				//decrement length
				length--;
			}

			length = lengthBest;
			if (length < 3) {
				//byte literal (distance cost is thus now zero since we have no distance component)
				length = 1;
				dstCost = 0;
			} else {
				//we ended up selecting an LZ copy-able length. but did we select the most optimal distance
				//encoding?
				//search possible distances where we can match the string at. We'll take the lowest-cost one.
				for (int i = 0; i < 30; i++) {
					if (offsetEncodings[i].length == 0) continue;

					unsigned int distanceBinCost = offsetEncodings[i].length + sDeflateOffsetTable[i].nMinorBits;
					for (int j = 0; j < (1 << sDeflateOffsetTable[i].nMinorBits); j++) {
						unsigned int dst = (sDeflateOffsetTable[i].majorPart + j) + 1;
						if (dst > pos) break;

						//matching distance, check the cost
						if (distanceBinCost < dstCost) {
							//check matching LZ string...
							if (CxiLzConfirmMatch(buffer, size, pos, dst, length)) {
								dstCost = distanceBinCost;
								distance = dst;

								//further iteration in this distance bin is unnecessary: low bit cost is fixed.
								break;
							}
						}
					}
				}
			}
			weight = weightBest + dstCost;
		}

		//write node
		if (length >= 3) {
			nodes[pos].distance = distance;
			nodes[pos].length = length;
		} else {
			nodes[pos].length = 1;
			nodes[pos].distance = 0;
		}
		nodes[pos].weight = weight;
	}

	free(lengthEncodings);
	free(offsetEncodings);

	//convert graph into node array
	unsigned int nTokens = 0;
	pos = 0;
	while (pos < size) {
		CxiLzNode *node = nodes + pos;
		nTokens++;

		if (node->length >= 3) pos += node->length;
		else pos++;
	}

	CxiLzToken *tokens = (CxiLzToken *) calloc(nTokens, sizeof(CxiLzToken));
	{
		pos = 0;
		unsigned int i = 0;
		while (pos < size) {
			CxiLzNode *node = nodes + pos;

			tokens[i].isReference = node->length >= 3;
			if (tokens[i].isReference) {
				tokens[i].length = node->length;
				tokens[i].distance = node->distance;
			} else {
				tokens[i].symbol = buffer[pos];
			}
			i++;

			if (node->length >= 3) pos += node->length;
			else pos++;
		}
		free(nodes);
	}

	*pnTokens = nTokens;
	return tokens;
}

static unsigned char *CxiCompressMvdkDeflateChunk(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, unsigned int *nOutBits) {
	//first, tokenize the input string.
	int nTokens;
	CxiLzToken *tokens = CxiMvdkTokenizeDeflate(buffer, size, &nTokens);

	//create Huffman tree
	CxiHuffNode *symbolTree, *offsetTree;
	CxiMvdkCreateHuffmanTree(tokens, nTokens, &symbolTree, &offsetTree);

	//re-tokenize
	//NOTE: this code is only theoretical. In reality it is very slow and provides little to no material benefit. 
	//left here for theory's sake. (change the loop count to 1 or 2 to see it in effect)
	for (int i = 0; i < 1; i++) {
		//create new tokenization
		free(tokens);
		tokens = CxiMvdkRetokenize(buffer, size, &nTokens, symbolTree, offsetTree);

		//create new Huffman tree
		free(symbolTree);
		free(offsetTree);
		CxiMvdkCreateHuffmanTree(tokens, nTokens, &symbolTree, &offsetTree);
	}

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
		CxiBitStream symbolStream, offsetStream;
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
	CxiBitStream bitStream;
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
	if (size < 4) return 0;

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


// ----- VLX Routines

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
	StList tokenBuffer;
	StStatus s = StListCreateInline(&tokenBuffer, CxiLzToken, NULL);
	if (!ST_SUCCEEDED(s)) return NULL;

	unsigned int nProcessedBytes = 0;
	while (nProcessedBytes < size) {
		unsigned int biggestRun = 0, biggestRunIndex = 0;
		biggestRun = CxiSearchLZ(buffer, size, nProcessedBytes, 1, 0xFFE, 0xFFF, &biggestRunIndex);

		//minimum run length is 2 bytes
		CxiLzToken token;
		if (biggestRun >= 2) {
			//increment copy length bin
			lengthCounts[CxiIlog2(biggestRun)]++;

			//increment copy distance bin
			distCounts[CxiIlog2(biggestRunIndex + 1)]++;

			//append token
			token.isReference = 1;
			token.length = biggestRun;
			token.distance = biggestRunIndex;

			//advance the buffer
			nProcessedBytes += biggestRun;
		} else {
			//no copy found: increment 0-length bin
			lengthCounts[0]++;

			token.isReference = 0;
			token.symbol = buffer[nProcessedBytes++];
		}

		s = StListAdd(&tokenBuffer, &token);
		if (!ST_SUCCEEDED(s)) {
			StListFree(&tokenBuffer);
			return NULL;
		}
	}

	*pnTokens = tokenBuffer.length;
	return tokenBuffer.buffer;
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

static void CxiVlxWriteSymbol(CxiBitStream *stream, uint32_t string, int length) {
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
	CxiBitStream stream;
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



// ----- ASH Routines

#define TREE_RIGHT    0x80000000
#define TREE_LEFT     0x40000000
#define TREE_VAL_MASK 0x3FFFFFFF

static uint32_t BigToLittle32(uint32_t x) {
	return (x >> 24) | (x << 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8);
}

uint32_t CxAshReadTree(BIT_READER_8 *reader, int width, uint32_t *leftTree, uint32_t *rightTree) {
	uint32_t *workmem = (uint32_t *) calloc(2 * (1 << width), sizeof(uint32_t));
	uint32_t *work = workmem;

	uint32_t r23 = (1 << width);
	uint32_t symRoot;
	uint32_t nNodes = 0;
	do {
		int bit = CxiConsumeBit(reader);
		if (reader->error) goto Error;

		if (bit) {
			if (r23 >= (2 * (1u << width)) || nNodes >= (2 * (1u << width))) goto Error;

			*(work++) = r23 | TREE_RIGHT;
			*(work++) = r23 | TREE_LEFT;
			nNodes += 2;
			r23++;
		} else {
			if (nNodes == 0) goto Error;

			symRoot = CxiConsumeBits(reader, width);
			if (reader->error) goto Error;
			do {
				uint32_t nodeval = *--work;
				uint32_t idx = nodeval & TREE_VAL_MASK;
				nNodes--;
				if (nodeval & TREE_RIGHT) {
					rightTree[idx] = symRoot;
					symRoot = idx;
				} else {
					leftTree[idx] = symRoot;
					break;
				}
			} while (nNodes > 0);
		}
	} while (nNodes > 0);

	free(workmem);
	return symRoot;

Error:
	free(workmem);
	return UINT32_MAX;
}


static void CxiAshEnsureTreeElements(CxiHuffNode *nodes, int nNodes, int nMinNodes) {
	//count nodes
	int nPresent = 0;
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq) nPresent++;
	}

	//have sufficient nodes?
	if (nPresent >= nMinNodes) return;

	//add dummy nodes
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nodes[i].freq = 1;
			nPresent++;
			if (nPresent >= nMinNodes) return;
		}
	}
}

static void CxiAshWriteTree(CxiBitStream *stream, CxiHuffNode *nodes, int nBits) {
	if (nodes->left != NULL) {
		//
		CxiBitStreamWrite(stream, 1);
		CxiAshWriteTree(stream, nodes->left, nBits);
		CxiAshWriteTree(stream, nodes->right, nBits);
	} else {
		//write value
		CxiBitStreamWrite(stream, 0);
		CxiBitStreamWriteBitsBE(stream, nodes->sym, nBits);
	}
}

static CxiLzToken *CxiAshTokenize(const unsigned char *buffer, unsigned int size, int nSymBits, int nDstBits, unsigned int *pnTokens) {
	StList tokenBuffer;
	StStatus s = StListCreateInline(&tokenBuffer, CxiLzToken, NULL);
	if (!ST_SUCCEEDED(s)) return NULL;

	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 3, (1 << nSymBits) - 1 - 0x100 + 3, 1, (1 << nDstBits));

	//
	unsigned int curpos = 0;
	while (curpos < size) {
		//search backwards
		unsigned int length, distance;
		length = CxiLzSearch(&state, &distance);

		CxiLzToken token;
		if (length >= 3) {
			token.isReference = 1;
			token.length = length;
			token.distance = distance;

			curpos += length;
		} else {
			token.isReference = 0;
			token.symbol = buffer[curpos++];
		}

		s = StListAdd(&tokenBuffer, &token);
		if (!ST_SUCCEEDED(s)) goto Error;

		CxiLzStateSlide(&state, length);
	}

	CxiLzStateFree(&state);
	*pnTokens = tokenBuffer.length;
	return (CxiLzToken *) tokenBuffer.buffer;

Error:
	CxiLzStateFree(&state);
	StListFree(&tokenBuffer);
	return NULL;
}

unsigned char *CxCompressAsh(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	int nSymBits = 9, nDstBits = 11;

	int nSymNodes = (1 << nSymBits);
	int nDstNodes = (1 << nDstBits);
	CxiHuffNode *symNodes = (CxiHuffNode *) calloc(nSymNodes * 2, sizeof(CxiHuffNode));
	CxiHuffNode *dstNodes = (CxiHuffNode *) calloc(nDstNodes * 2, sizeof(CxiHuffNode));

	for (int i = 0; i < nSymNodes; i++) {
		symNodes[i].sym = symNodes[i].symMin = symNodes[i].symMax = i;
		symNodes[i].nRepresent = 1;
	}
	for (int i = 0; i < nDstNodes; i++) {
		dstNodes[i].sym = dstNodes[i].symMin = dstNodes[i].symMax = i;
		dstNodes[i].nRepresent = 1;
	}

	//tokenize
	unsigned int nTokens = 0;
	CxiLzToken *tokens = CxiAshTokenize(buffer, size, nSymBits, nDstBits, &nTokens);

	//construct frequency distribution
	for (unsigned int i = 0; i < nTokens; i++) {
		CxiLzToken *token = &tokens[i];
		if (token->isReference) {
			symNodes[token->length - 3 + 0x100].freq++;
			dstNodes[token->distance - 1].freq++;
		} else {
			symNodes[token->symbol].freq++;
		}
	}

	//pre-tree construction: ensure at least two nodes are used
	CxiAshEnsureTreeElements(symNodes, nSymNodes, 2);
	CxiAshEnsureTreeElements(dstNodes, nDstNodes, 2);

	//construct trees
	CxiHuffmanConstructTree(symNodes, nSymNodes);
	CxiHuffmanConstructTree(dstNodes, nDstNodes);

	//init streams
	CxiBitStream symStream, dstStream;
	CxiBitStreamCreate(&symStream);
	CxiBitStreamCreate(&dstStream);

	//first, write huffman trees.
	CxiAshWriteTree(&symStream, symNodes, nSymBits);
	CxiAshWriteTree(&dstStream, dstNodes, nDstBits);

	//write data stream
	for (unsigned int i = 0; i < nTokens; i++) {
		CxiLzToken *token = &tokens[i];

		if (token->isReference) {
			CxiHuffmanWriteSymbol(&symStream, token->length - 3 + 0x100, symNodes);
			CxiHuffmanWriteSymbol(&dstStream, token->distance - 1, dstNodes);
		} else {
			CxiHuffmanWriteSymbol(&symStream, token->symbol, symNodes);
		}
	}
	free(tokens);
	free(symNodes);
	free(dstNodes);

	//encode data output
	unsigned int symStreamSize = 0;
	unsigned int dstStreamSize = 0;
	void *symBytes = CxiBitStreamGetBytes(&symStream, 1, 1, 1, &symStreamSize);
	void *dstBytes = CxiBitStreamGetBytes(&dstStream, 1, 1, 1, &dstStreamSize);

	//write data out
	unsigned char *out = (unsigned char *) calloc(0xC + symStreamSize + dstStreamSize, 1);
	{
		//write header
		uint32_t header[3];
		header[0] = 0x30485341;
		header[1] = BigToLittle32(size);
		header[2] = BigToLittle32(0xC + symStreamSize);
		memcpy(out, header, sizeof(header));

		//write streams
		memcpy(out + sizeof(header), symBytes, symStreamSize);
		memcpy(out + sizeof(header) + symStreamSize, dstBytes, dstStreamSize);
		free(symBytes);
		free(dstBytes);
	}

	//free stuff
	CxiBitStreamFree(&symStream);
	CxiBitStreamFree(&dstStream);

	*compressedSize = 0xC + symStreamSize + dstStreamSize;
	return out;
}

unsigned char *CxDecompressAsh(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	int symBits = 9, distBits = 11;
	uint32_t uncompSize = BigToLittle32(*(uint32_t *) (buffer + 4)) & 0x00FFFFFF;
	uint32_t outSize = uncompSize;

	uint8_t *outbuf = calloc(uncompSize, 1);
	uint8_t *destp = outbuf;

	BIT_READER_8 reader, reader2;
	CxiInitBitReader(&reader, buffer + BigToLittle32(*(const uint32_t *) (buffer + 0x8)), buffer + size, 1);
	CxiInitBitReader(&reader2, buffer + 0xC, buffer + size, 1);

	uint32_t symMax = (1 << symBits);
	uint32_t distMax = (1 << distBits);

	//HACK, pointer to RAM
	uint32_t *symLeftTree = calloc(2 * symMax - 1, sizeof(uint32_t));
	uint32_t *symRightTree = calloc(2 * symMax - 1, sizeof(uint32_t));
	uint32_t *distLeftTree = calloc(2 * distMax - 1, sizeof(uint32_t));
	uint32_t *distRightTree = calloc(2 * distMax - 1, sizeof(uint32_t));

	uint32_t symRoot, distRoot;
	symRoot = CxAshReadTree(&reader2, symBits, symLeftTree, symRightTree);
	distRoot = CxAshReadTree(&reader, distBits, distLeftTree, distRightTree);

	//main uncompress loop
	do {
		uint32_t sym = symRoot;
		while (sym >= symMax) {
			if (!CxiConsumeBit(&reader2)) {
				sym = symLeftTree[sym];
			} else {
				sym = symRightTree[sym];
			}
		}

		if (sym < 0x100) {
			*(destp++) = sym;
			uncompSize--;
		} else {
			uint32_t distsym = distRoot;
			while (distsym >= distMax) {
				if (!CxiConsumeBit(&reader)) {
					distsym = distLeftTree[distsym];
				} else {
					distsym = distRightTree[distsym];
				}
			}

			uint32_t copylen = (sym - 0x100) + 3;
			const uint8_t *srcp = destp - distsym - 1;

			uncompSize -= copylen;
			while (copylen--) {
				*(destp++) = *(srcp++);
			}
		}
	} while (uncompSize > 0);

	free(symLeftTree);
	free(symRightTree);
	free(distLeftTree);
	free(distRightTree);

	*uncompressedSize = outSize;
	return outbuf;
}

int CxIsCompressedAsh(const unsigned char *buffer, unsigned int size) {
	int symBits = 9, distBits = 11;
	int valid = 0;

	//check header
	if (size < 0xC) return 0;
	if (memcmp(buffer, "ASH", 3) != 0) return 0;

	uint32_t uncompSize = BigToLittle32(*(uint32_t *) (buffer + 4)) & 0x00FFFFFF;
	uint32_t outSize = uncompSize;

	BIT_READER_8 reader, reader2;
	uint32_t offsDist = BigToLittle32(*(const uint32_t *) (buffer + 0x8));
	if (offsDist < 0xC || offsDist >= size) return 0;

	CxiInitBitReader(&reader, buffer + offsDist, buffer + size, 1);
	CxiInitBitReader(&reader2, buffer + 0xC, buffer + size, 1);

	uint32_t symMax = (1 << symBits);
	uint32_t distMax = (1 << distBits);

	//alloc trees
	uint32_t *symLeftTree = calloc(2 * symMax - 1, sizeof(uint32_t));
	uint32_t *symRightTree = calloc(2 * symMax - 1, sizeof(uint32_t));
	uint32_t *distLeftTree = calloc(2 * distMax - 1, sizeof(uint32_t));
	uint32_t *distRightTree = calloc(2 * distMax - 1, sizeof(uint32_t));

	uint32_t symRoot, distRoot;
	symRoot = CxAshReadTree(&reader2, symBits, symLeftTree, symRightTree);
	distRoot = CxAshReadTree(&reader, distBits, distLeftTree, distRightTree);
	if (symRoot == UINT32_MAX || distRoot == UINT32_MAX) goto Cleanup;

	//main uncompress loop
	unsigned int outpos = 0;
	do {
		uint32_t sym = symRoot;
		while (sym >= symMax) {
			int bit = CxiConsumeBit(&reader2);
			if (reader2.error) goto Cleanup;

			if (!bit) {
				sym = symLeftTree[sym];
			} else {
				sym = symRightTree[sym];
			}
		}

		if (sym < 0x100) {
			outpos++;
			uncompSize--;
		} else {
			uint32_t distsym = distRoot;
			while (distsym >= distMax) {
				int bit = CxiConsumeBit(&reader);
				if (reader.error) goto Cleanup;

				if (!bit) {
					distsym = distLeftTree[distsym];
				} else {
					distsym = distRightTree[distsym];
				}
			}

			//assert valid source and length
			uint32_t copylen = (sym - 0x100) + 3;
			uint32_t copydst = distsym + 1;
			if (copylen > uncompSize || copydst > outpos) goto Cleanup;

			outpos += copylen;
			uncompSize -= copylen;
		}
	} while (uncompSize > 0);
	valid = 1;

Cleanup:
	if (symLeftTree != NULL) free(symLeftTree);
	if (symRightTree != NULL) free(symRightTree);
	if (distLeftTree != NULL) free(distLeftTree);
	if (distRightTree != NULL) free(distRightTree);
	return valid;
}



// ----- Generic Routines

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
	if (CxIsCompressedAsh(buffer, size)) return COMPRESSION_ASH;

	return COMPRESSION_NONE;
}

int CxIsCompressed(const unsigned char *buffer, unsigned int size, int type) {
	switch (type) {
		case COMPRESSION_NONE             : return 1;
		case COMPRESSION_LZ77             : return CxIsCompressedLZ(buffer, size);
		case COMPRESSION_LZ77_HEADER      : return CxIsFilteredLZHeader(buffer, size);
		case COMPRESSION_LZ11             : return CxIsCompressedLZX(buffer, size);
		case COMPRESSION_RLE              : return CxIsCompressedRL(buffer, size);
		case COMPRESSION_HUFFMAN_4        : return CxIsCompressedHuffman4(buffer, size);
		case COMPRESSION_HUFFMAN_8        : return CxIsCompressedHuffman8(buffer, size);
		case COMPRESSION_DIFF8            : return CxIsFilteredDiff8(buffer, size);
		case COMPRESSION_DIFF16           : return CxIsFilteredDiff16(buffer, size);
		case COMPRESSION_ASH              : return CxIsCompressedAsh(buffer, size);
		case COMPRESSION_MVDK             : return CxIsCompressedMvDK(buffer, size);
		case COMPRESSION_VLX              : return CxIsCompressedVlx(buffer, size);
		case COMPRESSION_LZ11_COMP_HEADER : return CxIsCompressedLZXComp(buffer, size);
	}
	return 0;
}

unsigned char *CxDecompress(const unsigned char *buffer, unsigned int size, int type, unsigned int *uncompressedSize) {
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
		case COMPRESSION_ASH:
			return CxDecompressAsh(buffer, size, uncompressedSize);
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
		case COMPRESSION_ASH:
			return CxCompressAsh(buffer, size, compressedSize);
	}
	return NULL;
}
