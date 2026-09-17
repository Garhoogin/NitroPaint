#include "CxPrivate.h"



// ----- Compression routines

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



// ----- Decompression routines

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



// ----- Validation routines

int CxIsCompressedRL(const unsigned char *buffer, unsigned int size) {
	if (size < 4 || *buffer != 0x30) return 0;
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;

	unsigned int dstOfs = 0, srcOfs = 4;
	while (dstOfs < uncompSize) {
		if (srcOfs >= size) return 0;

		unsigned char head = buffer[srcOfs++];
		unsigned int chunkLen = head & 0x7F;

		if (head & 0x80) {
			//RL run
			chunkLen += 3;
			if ((size - srcOfs) < 1) return 0;

			srcOfs++;
		} else {
			//stored run
			chunkLen++;
			if ((size - srcOfs) < chunkLen) return 0;

			srcOfs += chunkLen;
		}
		dstOfs += chunkLen;

		if (dstOfs > uncompSize) return 0;
	}

	//allow up to 3 bytes padding
	if (size - srcOfs > 3) return 0;

	return 1;
}

