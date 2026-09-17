#include <string.h>

#include "LZCore.h"
#include "HuffCore.h"

// ----- Word buffer routines

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



// ----- Compression routines

static CxiLzToken *CxiVlxComputeLzStatistics(const unsigned char *buffer, unsigned int size, unsigned int *lengthCounts, unsigned int *distCounts, int *pnTokens) {
	unsigned int nTokens;
	CxiLzToken *tokens = CxiLzTokenizeGreedy(buffer, size, 2, 0xFFF, 1, 0xFFE, &nTokens);

	for (unsigned int i = 0; i < nTokens; i++) {
		CxiLzToken *tok = &tokens[i];

		if (tok->isReference) {
			//increment copy length bin
			lengthCounts[CxiIlog2(tok->length)]++;

			//increment copy distance bin
			distCounts[CxiIlog2(tok->distance + 1)]++;
		} else {
			//increment 0-length bin
			lengthCounts[0]++;
		}
	}

	*pnTokens = nTokens;
	return tokens;
}

static int CxiVlxWriteHuffmanTree(CxiHuffNode *tree, uint16_t *dest, int pos, uint16_t *reps, int *lengths, uint16_t curval, int nBitsVal) {
	if (ISLEAF(tree)) {
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

static void CxiVlxWriteSymbol(CxiBitWriter *stream, uint32_t string, int length) {
	for (int i = 0; i < length; i++) {
		CxiBitWriterWriteBit(stream, (string >> (length - 1 - i)) & 1);
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

		lengthNodes[nLengthNodes].sym = i;
		lengthNodes[nLengthNodes].freq = lengthCounts[i];
		nLengthNodes++;
	}
	for (int i = 0; i < 12; i++) {
		if (distCounts[i] == 0) continue;

		distNodes[nDistNodes].sym = i;
		distNodes[nDistNodes].freq = distCounts[i];
		nDistNodes++;
	}

	//if we would create a tree of 0 size, add a dummy node. The decompressor is not written to
	//handle this case.
	for (int i = 0; i < 12 && nLengthNodes < 2; i++) {
		if (lengthCounts[i] == 0) {
			lengthCounts[i] = 1;
			lengthNodes[nLengthNodes].sym = i;
			lengthNodes[nLengthNodes].freq = lengthCounts[i];
			nLengthNodes++;
		}
	}
	for (int i = 0; i < 12 && nDistNodes < 2; i++) {
		if (distCounts[i] == 0) {
			distCounts[i] = 1;
			distNodes[nDistNodes].sym = i;
			distNodes[nDistNodes].freq = distCounts[i];
			nDistNodes++;
		}
	}

	CxiHuffmanConstructTree(lengthNodes, nLengthNodes, 2);
	CxiHuffmanConstructTree(distNodes, nDistNodes, 2);

	//write tree
	uint16_t treeData[24] = { 0 };
	uint8_t treeHeader = (nLengthNodes << 4) | nDistNodes;

	uint16_t lengthReps[12] = { 0 }, distReps[12] = { 0 };
	int lengthLengths[12] = { 0 }, distLengths[12] = { 0 };
	int treeDataSize = CxiVlxWriteHuffmanTree(lengthNodes, treeData, 0, lengthReps, lengthLengths, 0, 0);
	treeDataSize = CxiVlxWriteHuffmanTree(distNodes, treeData, treeDataSize, distReps, distLengths, 0, 0);

	//write tokens
	CxiBitWriter stream;
	CxiBitWriterInit(&stream);
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
	for (unsigned int i = 0; i < stream.nWords; i++) {
		stream.bits[i] = CxiByteSwap(stream.bits[i]);
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
	CxiBitWriterFree(&stream);

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



// ----- Decompression routines

typedef struct st1_ {
	uint16_t value;
	uint16_t maskBits;
	uint32_t encoding;
	uint32_t mask;
} CxiVlxTreeNode;

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
			if (src[4] != 0) return 0; // bug in the game's decoder shifts this byte incorrectly
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
		lengthEntries[i].mask = 0xFFFFFFFF << shift;
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
		distEntries[i].mask = 0xFFFFFFFF << shift;
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



// ----- Validation routines

int CxIsCompressedVlx(const unsigned char *src, unsigned int size) {
	return CxiTryDecompressVlx(src, size, NULL);
}
