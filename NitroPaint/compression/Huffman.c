#include <string.h>

#include "HuffCore.h"


// ----- Compression routines

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

	//build Huffman tree
	int nLeaf = CxiHuffmanConstructTree(nodes, nSym, 2);

	//construct Huffman tree encoding
	CxiHuffTreeCode treeCode[512] = { 0 };
	treeCode[0].value = ((nLeaf + 1) & ~1) - 1;
	treeCode[0].lrbit = 0;
	treeCode[1].value = 0;
	treeCode[1].lrbit = CxiHuffmanGetFlagForNode(nodes);
	CxiHuffmanCreateTreeCode(nodes, treeCode + 2);
	CxiHuffmanCheckTree(treeCode, nLeaf * 2);

	//now write bits out.
	CxiBitWriter stream;
	CxiBitWriterInit(&stream);
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
	CxiBitWriterFree(&stream);

	*compressedSize = outSize;
	return finbuf;
}

unsigned char *CxCompressHuffman8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	return CxCompressHuffman(buffer, size, compressedSize, 8);
}

unsigned char *CxCompressHuffman4(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	return CxCompressHuffman(buffer, size, compressedSize, 4);
}



// ----- Decompression routines

unsigned char *CxDecompressHuffman(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t outSize = (*(const uint32_t *) buffer) >> 8;
	unsigned char *out = (unsigned char *) malloc((outSize + 3) & ~3);
	*uncompressedSize = outSize;

	const unsigned char *treeBase = buffer + 4;
	unsigned int symSize = *buffer & 0xF;
	unsigned int bufferFill = 0;
	unsigned int bufferSize = 32 / symSize;
	uint32_t outBuffer = 0;

	unsigned int offs = ((*treeBase + 1) << 1) + 4;
	unsigned int trOffs = 1;

	CxiBitReader reader;
	CxiBitReaderInit(&reader, buffer + offs, buffer + size, 1, 0);

	unsigned int nWritten = 0;
	while (nWritten < outSize) {
		unsigned int lr = CxiBitReaderReadBit(&reader);
		unsigned char thisNode = treeBase[trOffs];
		unsigned int thisNodeOffs = ((thisNode & 0x3F) + 1) << 1; //add to current offset rounded down to get next element offset

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
	}

	return out;
}



// ----- Validation routines

int CxIsCompressedHuffman(const unsigned char *buffer, unsigned int size) {
	if (size < 5 || (*buffer != 0x28 && *buffer != 0x24)) return 0;

	uint32_t outSize = (*(const uint32_t *) buffer) >> 8;
	unsigned int symSize = *buffer & 0xF;

	unsigned int bufferFill = 0;
	unsigned int bufferSize = 32 / symSize;
	unsigned int trOffs = 1;

	CxiBitReader reader;
	CxiBitReaderInit(&reader, buffer + 4 + ((buffer[4] + 1) << 1), buffer + size, 1, 0);

	unsigned int nWritten = 0;
	while (nWritten < outSize) {
		unsigned int lr = CxiBitReaderReadBit(&reader);
		unsigned char thisNode = buffer[4 + trOffs];
		if ((4u + thisNode) >= size) return 0;

		unsigned int thisNodeOffs = ((thisNode & 0x3F) + 1) << 1;
		trOffs = (trOffs & ~1) + thisNodeOffs + lr;

		if (thisNode & (0x80 >> lr)) {
			if ((4 + trOffs) >= size) return 0;
			trOffs = 1;
			bufferFill++;

			if (bufferFill >= bufferSize) {
				nWritten += 4;
				bufferFill = 0;
			}
		}
	}
	return !reader.error;
}

int CxIsCompressedHuffman4(const unsigned char *buffer, unsigned int size) {
	return size > 0 && *buffer == 0x24 && CxIsCompressedHuffman(buffer, size);
}

int CxIsCompressedHuffman8(const unsigned char *buffer, unsigned int size) {
	return size > 0 && *buffer == 0x28 && CxIsCompressedHuffman(buffer, size);
}
