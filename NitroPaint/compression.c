#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "compression.h"
#include "bstream.h"

char *lz77decompress(char *buffer, int size, int *uncompressedSize){
	//decompress the input buffer. 
	//input is invalid if the size is less than 4.
	if(size < 4) return NULL;
	//find the length of the decompressed buffer.
	int length = *(int *) (buffer + 1) & 0xFFFFFF;
	//create a buffer for the decompressed buffer
	char *result = (char *) malloc(length);
	if(!result) return NULL;
	*uncompressedSize = length;
	//initialize variables
	int offset = 4;
	int dstOffset = 0;
	int x = 0;
	while(1){
		unsigned char head = buffer[offset];
		offset++;
		//loop 8 times
		for(int i = 0; i < 8; i++){
			int flag = head >> 7;
			head <<= 1;
			if(!flag){
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if(dstOffset == length) return result;
			} else {
				unsigned char high = buffer[offset++];
				unsigned char low = buffer[offset++];

				//length of uncompressed chunk and offset
				int offs = (((high & 0xF) << 8) | low) + 1;
				int len = (high >> 4) + 3;
				x = 1;
				for(int j = 0; j < len; j++){
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if(dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}

char *lz11decompress(char *buffer, int size, int *uncompressedSize){
	//decompress the input buffer. 
	if(size < 4) return NULL;

	//find the length of the decompressed buffer.
	int length = *(int *) (buffer) >> 8;

	//create a buffer for the decompressed buffer
	char *result = (char *) malloc(length);
	if(result == NULL) return NULL;
	*uncompressedSize = length;

	//initialize variables
	int offset = 4;
	int dstOffset = 0;
	int x = 0;
	while(1){
		unsigned char head = buffer[offset];
		offset++;

		//loop 8 times
		for(int i = 0; i < 8; i++){
			int flag = head >> 7;
			head <<= 1;
			if(!flag){
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if(dstOffset == length) return result;
			} else {
				unsigned char high = buffer[offset++];
				unsigned char low = buffer[offset++];
				unsigned char low2, low3;
				int mode = high >> 4;

				int len = 0, offs = 0;
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
				for(int j = 0; j < len; j++){
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if(dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}

char *huffmanDecompress(unsigned char *buffer, int size, int *uncompressedSize) {
	if (size < 5) return NULL;

	int outSize = (*(unsigned *) buffer) >> 8;
	char *out = (char *) malloc((outSize + 3) & ~3);
	*uncompressedSize = outSize;

	unsigned char *treeBase = buffer + 4;
	int symSize = *buffer & 0xF;
	int bufferFill = 0;
	int bufferSize = 32 / symSize;
	unsigned int outBuffer = 0;

	int offs = ((*treeBase + 1) << 1) + 4;
	int trOffs = 1;

	int nWritten = 0;
	while (nWritten < outSize) {

		unsigned bits = *(unsigned *) (buffer + offs);
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
					*(unsigned *) (out + nWritten) = outBuffer;
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

int compareMemory(char *b1, char *b2, int nMax, int nAbsoluteMax) {
	int nSame = 0;
	if (nMax > nAbsoluteMax) nMax = nAbsoluteMax;
	//count up to nMax. If all match, count 0x12-nMax bytes. The b1 just starts over.
	for (int i = 0; i < nMax; i++) {
		if (*(b1++) == *(b2++)) nSame++;
		else break;
	}
	if (nSame == nMax) {
		b1 -= nMax;
		for (int i = 0; i < nAbsoluteMax - nMax; i++) {
			if (*(b1++) == *(b2++)) nSame++;
			else break;
		}
	}
	return nSame;
}

char *lz77compress(char *buffer, int size, int *compressedSize){
	int compressedMaxSize = 4 + 9 * ((size + 7) >> 3);
	char *compressed = (char *) malloc(compressedMaxSize);
	char *compressedBase = compressed;
	*(unsigned *) compressed = size << 8;
	*compressed = 0x10;
	int nProcessedBytes = 0;
	int nSize = 4;
	compressed += 4;
	while (1) {
		//make note of where to store the head for later.
		char *headLocation = compressed;
		compressed++;
		nSize++;
		//initialize the head.
		char head = 0;

		//set when the end of the file is reached, and the result needs to be zero-padded.
		int isDone = 0;

		//repeat 8x (8 bits per byte)
		for (int i = 0; i < 8; i++) {
			head <<= 1;

			if (isDone) {
				*(compressed++) = 0;
				nSize++;
				continue;
			}

			//search backwards up to 0xFFF bytes.
			int maxSearch = 0x1000;
			if (maxSearch > nProcessedBytes) maxSearch = nProcessedBytes;

			//the biggest match, and where it was
			int biggestRun = 0, biggestRunIndex = 0;

			//begin searching backwards.
			for (int j = 2; j < maxSearch; j++) {
				//compare up to 0xF bytes, at most j bytes.
				int nCompare = 0x12;
				if (nCompare > j) nCompare = j;
				int nBytesLeft = size - nProcessedBytes;
				int nAbsoluteMaxCompare = 0x12;
				if (nAbsoluteMaxCompare > nBytesLeft) nAbsoluteMaxCompare = nBytesLeft;
				int nMatched = compareMemory(buffer - j, buffer, nCompare, nAbsoluteMaxCompare);
				if (nMatched > biggestRun) {
					if (biggestRun == 0x12) break;
					biggestRun = nMatched;
					biggestRunIndex = j;
				}
			}

			//if the biggest run is at least 3, then we use it.
			if (biggestRun >= 3) {
				head |= 1;
				nProcessedBytes += biggestRun;
				//encode the match. First byte has high nybble as length minus 3, low nybble as the high byte of the offset.
				*(compressed++) = ((biggestRun - 3) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
				*(compressed++) = (biggestRunIndex - 1) & 0xFF;
				//advance the buffer
				buffer += biggestRun;
				nSize += 2;
				if (nProcessedBytes >= size) isDone = 1;
			} else {
				*(compressed++) = *(buffer++);
				nProcessedBytes++;
				nSize++;
				if (nProcessedBytes >= size) isDone = 1;
			}
		}
		*headLocation = head;
		if (nProcessedBytes >= size) break;
	}
	*compressedSize = nSize;
	return realloc(compressedBase, nSize);

}//22999

char *lz11compress(char *buffer, int size, int *compressedSize) {
	int compressedMaxSize = 7 + 9 * ((size + 7) >> 3);
	char *compressed = (char *) malloc(compressedMaxSize);
	char *compressedBase = compressed;
	*(unsigned *) compressed = size << 8;
	*compressed = 0x11;
	int nProcessedBytes = 0;
	int nSize = 4;
	compressed += 4;
	while (1) {
		//make note of where to store the head for later.
		char *headLocation = compressed;
		compressed++;
		nSize++;
		//initialize the head.
		char head = 0;

		//set when the end of the file is reached, and the result needs to be zero-padded.
		int isDone = 0;

		//repeat 8x (8 bits per byte)
		for (int i = 0; i < 8; i++) {
			head <<= 1;

			if (isDone) {
				*(compressed++) = 0;
				nSize++;
				continue;
			}

			//search backwards up to 0xFFF bytes.
			int maxSearch = 0x1000;
			if (maxSearch > nProcessedBytes) maxSearch = nProcessedBytes;

			//the biggest match, and where it was
			int biggestRun = 0, biggestRunIndex = 0;

			//begin searching backwards.
			for (int j = 2; j < maxSearch; j++) {
				//compare up to 0xF bytes, at most j bytes.
				int nCompare = 0xFFFF + 0x111; //max run length
				if (nCompare > j) nCompare = j;
				int nBytesLeft = size - nProcessedBytes;
				int nAbsoluteMaxCompare = 0xFFFF + 0x111; //max run length
				if (nAbsoluteMaxCompare > nBytesLeft) nAbsoluteMaxCompare = nBytesLeft;
				int nMatched = compareMemory(buffer - j, buffer, nCompare, nAbsoluteMaxCompare);
				if (nMatched > biggestRun) {
					if (biggestRun == 0x12) break;
					biggestRun = nMatched;
					biggestRunIndex = j;
				}
			}

			//if the biggest run is at least 3, then we use it.
			if (biggestRun >= 3) {
				head |= 1;
				nProcessedBytes += biggestRun;
				//encode the match.

				if (biggestRun <= 0x10) {
					//First byte has high nybble as length minus 1, low nybble as the high byte of the offset.
					*(compressed++) = ((biggestRun - 1) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 2;
				} else if (biggestRun <= 0xFF + 0x11) {
					//First byte has the high 4 bits of run length minus 0x11
					//Second byte has the low 4 bits of the run length minus 0x11 in the high nybble
					*(compressed++) = (biggestRun - 0x11) >> 4;
					*(compressed++) = (((biggestRun - 0x11) & 0xF) << 4) | ((biggestRunIndex - 1) >> 8);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 3;
				} else if (biggestRun <= 0xFFFF + 0x111) {
					//First byte is 0x10 ORed with the high 4 bits of run length minus 0x111
					*(compressed++) = 0x10 | (((biggestRun - 0x111) >> 12) & 0xF);
					*(compressed++) = ((biggestRun - 0x111) >> 4) & 0xFF;
					*(compressed++) = (((biggestRun - 0x111) & 0xF) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;

					nSize += 4;
				}
				//advance the buffer
				buffer += biggestRun;
				if (nProcessedBytes >= size) isDone = 1;
			} else {
				*(compressed++) = *(buffer++);
				nProcessedBytes++;
				nSize++;
				if (nProcessedBytes >= size) isDone = 1;
			}
		}
		*headLocation = head;
		if (nProcessedBytes >= size) break;
	}

done:
	while (nSize & 3) {
		*(compressed++) = 0;
		nSize++;
	}
	*compressedSize = nSize;
	return realloc(compressedBase, nSize);
}

typedef struct HUFFNODE_ {
	unsigned char sym;
	unsigned char symMin; //had space to spare, maybe make searches a little simpler
	unsigned char symMax;
	unsigned char nRepresent;
	int freq;
	struct HUFFNODE_ *left;
	struct HUFFNODE_ *right;
} HUFFNODE;

typedef struct BITSTREAM_ {
	unsigned *bits;
	int nWords;
	int nBitsInLastWord;
	int nWordsAlloc;
} BITSTREAM;

void bitStreamCreate(BITSTREAM *stream) {
	stream->nWords = 1;
	stream->nBitsInLastWord = 0;
	stream->nWordsAlloc = 16;
	stream->bits = (unsigned *) calloc(16, 4);
}

void bitStreamFree(BITSTREAM *stream) {
	free(stream->bits);
}

void bitStreamWrite(BITSTREAM *stream, int bit) {
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
}

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)

int huffNodeComparator(const void *p1, const void *p2) {
	return ((HUFFNODE *) p2)->freq - ((HUFFNODE *) p1)->freq;
}

unsigned int huffmanWriteNode(unsigned char *tree, unsigned int pos, HUFFNODE *node) {
	HUFFNODE *left = node->left;
	HUFFNODE *right = node->right;

	//we will write two bytes. 
	unsigned int afterPos = pos + 2;
	if (ISLEAF(left)) {
		tree[pos] = left->sym;
	} else {
		HUFFNODE *leftLeft = left->left;
		HUFFNODE *leftRight = left->right;
		unsigned char flag = (ISLEAF(leftLeft) << 7) | (ISLEAF(leftRight) << 6);
		unsigned lastAfterPos = afterPos;
		afterPos = huffmanWriteNode(tree, afterPos, left);
		tree[pos] = flag | ((((lastAfterPos - pos) >> 1) - 1) & 0x3F);
		if (((lastAfterPos - pos) >> 1) - 1 > 0x3F) _asm int 3;
	}

	if (ISLEAF(right)) {
		tree[pos + 1] = right->sym;
	} else {
		HUFFNODE *rightLeft = right->left;
		HUFFNODE *rightRight = right->right;
		unsigned char flag = (ISLEAF(rightLeft) << 7) | (ISLEAF(rightRight) << 6);
		unsigned lastAfterPos = afterPos;
		afterPos = huffmanWriteNode(tree, afterPos, right);
		tree[pos + 1] = flag | ((((lastAfterPos - pos) >> 1) - 1) & 0x3F);
		if (((lastAfterPos - pos) >> 1) - 1 > 0x3F) _asm int 3;
	}
	return afterPos;
}

void makeShallowNodeFirst(HUFFNODE *node) {
	if (ISLEAF(node)) return;
	if (node->left->nRepresent > node->right->nRepresent) {
		HUFFNODE *left = node->left;
		node->left = node->right;
		node->right = left;
	}
	makeShallowNodeFirst(node->left);
	makeShallowNodeFirst(node->right);
}

int huffmanNodeHasSymbol(HUFFNODE *node, unsigned char sym) {
	if (ISLEAF(node)) return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax) return 0;
	HUFFNODE *left = node->left;
	HUFFNODE *right = node->right;
	return huffmanNodeHasSymbol(left, sym) || huffmanNodeHasSymbol(right, sym);
}

void huffmanWriteSymbol(BITSTREAM *bits, unsigned char sym, HUFFNODE *tree) {
	if (ISLEAF(tree)) return;
	HUFFNODE *left = tree->left;
	HUFFNODE *right = tree->right;
	if (huffmanNodeHasSymbol(left, sym)) {
		bitStreamWrite(bits, 0);
		huffmanWriteSymbol(bits, sym, left);
	} else {
		bitStreamWrite(bits, 1);
		huffmanWriteSymbol(bits, sym, right);
	}
}

void huffmanConstructTree(HUFFNODE *nodes, int nNodes) {
	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(HUFFNODE), huffNodeComparator);
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
		HUFFNODE *srcA = nodes + nRoots - 2;
		HUFFNODE *destA = nodes + nTotalNodes;
		memcpy(destA, srcA, sizeof(HUFFNODE));

		HUFFNODE *left = destA;
		HUFFNODE *right = nodes + nRoots - 1;
		HUFFNODE *branch = srcA;

		branch->freq = left->freq + right->freq;
		branch->sym = 0;
		branch->left = left;
		branch->right = right;
		branch->symMin = min(left->symMin, right->symMin);
		branch->symMax = max(right->symMax, left->symMax);
		branch->nRepresent = left->nRepresent + right->nRepresent; //may overflow for root, but the root doesn't really matter for this

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(HUFFNODE), huffNodeComparator);
	}

	//just to be sure, make sure the shallow node always comes first
	makeShallowNodeFirst(nodes);
}

char *huffmanCompress(unsigned char *buffer, int size, int *compressedSize, int nBits) {
	//create a histogram of each byte in the file.
	HUFFNODE *nodes = (HUFFNODE *) calloc(512, sizeof(HUFFNODE));
	int nSym = 1 << nBits;
	for (int i = 0; i < nSym; i++) {
		nodes[i].sym = i;
		nodes[i].symMin = i;
		nodes[i].symMax = i;
		nodes[i].nRepresent = 1;
	}

	//construct histogram
	if (nBits == 8) {
		for (int i = 0; i < size; i++) {
			nodes[buffer[i]].freq++;
		}
	} else {
		for (int i = 0; i < size; i++) {
			nodes[buffer[i] & 0xF].freq++;
			nodes[buffer[i] >> 4].freq++;
		}
	}

	huffmanConstructTree(nodes, nSym);

	//now we've got a proper Huffman tree. Great! 
	unsigned char *tree = (unsigned char *) calloc(512, 1);
	unsigned int treeSize = huffmanWriteNode(tree, 2, nodes);
	treeSize = (treeSize + 3) & ~3; //round up
	tree[0] = (treeSize >> 1) - 1;
	tree[1] = 0;

	//now write bits out.
	BITSTREAM stream;
	bitStreamCreate(&stream);
	if (nBits == 8) {
		for (int i = 0; i < size; i++) {
			huffmanWriteSymbol(&stream, buffer[i], nodes);
		}
	} else {
		for (int i = 0; i < size; i++) {
			huffmanWriteSymbol(&stream, buffer[i] & 0xF, nodes);
			huffmanWriteSymbol(&stream, buffer[i] >> 4, nodes);
		}
	}

	//combine into one
	unsigned int outSize = 4 + treeSize + stream.nWords * 4;
	char *finBuf = (char *) malloc(outSize);
	*(unsigned *) finBuf = 0x20 | nBits | (size << 8);
	memcpy(finBuf + 4, tree, treeSize);
	memcpy(finBuf + 4 + treeSize, stream.bits, stream.nWords * 4);
	free(tree);
	free(nodes);
	bitStreamFree(&stream);

	*compressedSize = outSize;
	return finBuf;
}

char *huffman8Compress(unsigned char *buffer, int size, int *compressedSize) {
	return huffmanCompress(buffer, size, compressedSize, 8);
}

char *huffman4Compress(unsigned char *buffer, int size, int *compressedSize) {
	return huffmanCompress(buffer, size, compressedSize, 4);
}

int lz77IsCompressed(char *buffer, unsigned size) {
	if (size < 4) return 0;
	if (*buffer != 0x10) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	if ((length / 144) * 17 + 4 > size) return 0;

	//start a dummy decompression
	unsigned int offset = 4;
	unsigned int dstOffset = 0;
	while(1){
		unsigned char head = buffer[offset];
		offset++;
		//loop 8 times
		for(int i = 0; i < 8; i++){
			int flag = head >> 7;
			head <<= 1;
			if(!flag){
				if (dstOffset >= length || offset >= size) return 0;
				dstOffset++, offset++;
				if (dstOffset == length) return 1;
			} else {
				if (offset + 1 >= size) return 0;
				unsigned char high = buffer[offset++];
				unsigned char low = buffer[offset++];

				//length of uncompressed chunk and offset
				unsigned int offs = (((high & 0xF) << 8) | low) + 1;
				unsigned int len = (high >> 4) + 3;
				if (dstOffset < offs) return 0;
				for(unsigned int j = 0; j < len; j++){
					if (dstOffset >= length) return 0;
					dstOffset++;
					if (dstOffset == length) return 1;
				}
			}
		}
	}
	return 1;
}

int lz11IsCompressed(char *buffer, unsigned size) {
	if (size < 4) return 0;
	if (*buffer != 0x11) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	if (size > 7 + length * 9 / 8) return 0;

	//perform a test decompression.
	unsigned int offset = 4;
	unsigned int dstOffset = 0;
	while(1){
		if (offset >= size) return 0;
		unsigned char head = buffer[offset]; unsigned char origHead = head;
		offset++;

		//loop 8 times
		for(int i = 0; i < 8; i++){
			int flag = head >> 7;
			head <<= 1;
			if(!flag){
				if (offset >= size || dstOffset >= length) return 0;
				dstOffset++, offset++;
				if(dstOffset == length) return 1;
			} else {
				if (offset + 1 >= size) return 0;
				unsigned char high = buffer[offset++];
				unsigned char low = buffer[offset++];
				unsigned char low2, low3;
				int mode = high >> 4;

				int len = 0, offs = 0;
				switch (mode) {
					case 0:
						if (offset >= size) return 0;
						low2 = buffer[offset++];
						len = ((high << 4) | (low >> 4)) + 0x11; //8-bit length +0x11
						offs = (((low & 0xF) << 8) | low2) + 1; //12-bit offset
						break;
					case 1:
						if (offset + 1 >= size) return 0;
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
				if (dstOffset - offs < 0) return 0;
				for(int j = 0; j < len; j++){
					if (dstOffset >= length) return 0;
					dstOffset++;
					if(dstOffset == length) return 1;
				}
			}
		}
	}

	return 1;
}

int huffmanIsCompressed(unsigned char *buffer, unsigned size) {
	if (size < 5) return 0;
	if (*buffer != 0x24 && *buffer != 0x28) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	unsigned bitStreamOffset = ((buffer[5] + 1) << 1) + 4;
	if (bitStreamOffset > size) return 0;

	//process huffman tree
	unsigned dataOffset = ((buffer[4] + 1) << 1) + 4;
	if (dataOffset > size) return 0;

	//check if the uncompressed size makes sense
	unsigned bitStreamLength = size - dataOffset;
	if (bitStreamLength * 8 < length) return 0;
	return 1;
}

int huffman4IsCompressed(unsigned char *buffer, unsigned size) {
	return size > 0 && *buffer == 0x24 && huffmanIsCompressed(buffer, size);
}

int huffman8IsCompressed(unsigned char *buffer, unsigned size) {
	return size > 0 && *buffer == 0x28 && huffmanIsCompressed(buffer, size);
}

int lz11CompHeaderIsValid(char *buffer, unsigned size) {
	if (size < 0x14) return 0;
	unsigned magic = *(unsigned *) buffer;
	if (magic != 'COMP' && magic != 'PMOC') return 0;

	//validate headers
	uint32_t nSegments = *(uint32_t *) (buffer + 0x8);
	unsigned int headerSize = 0x10 + 4 * nSegments;
	unsigned int offset = headerSize;
	unsigned int uncompSize = 0;
	if (nSegments == 0) return 0;
	for (unsigned int i = 0; i < nSegments; i++) {

		//parse segment length & compression setting
		int32_t thisSegmentLength = *(int32_t *) (buffer + 0x10 + i * 4);
		int segCompressed = thisSegmentLength >= 0; //length >= 0 means segment is compressed
		if (thisSegmentLength < 0) {
			thisSegmentLength = -thisSegmentLength;
		}
		if (offset + thisSegmentLength > size) return 0;

		//decompression (if applicable)
		if (segCompressed) {
			if (!lz11IsCompressed(buffer + offset, thisSegmentLength)) return 0;
			uncompSize += *(unsigned *) (buffer + offset) >> 8;
		} else {
			uncompSize += thisSegmentLength;
		}
		offset += thisSegmentLength;
	}
	if(uncompSize != *(unsigned *) (buffer + 0x4)) return 0;

	return 1;
}

char *lz11CompHeaderDecompress(char *buffer, int size, int *uncompressedSize) {
	unsigned int totalSize = *(unsigned *) (buffer + 0x4);
	unsigned int nSegments = *(unsigned *) (buffer + 0x8);
	*uncompressedSize = totalSize;

	char *out = (char *) malloc(totalSize);
	unsigned int dstOffs = 0;
	unsigned int offset = 0x10 + 4 * nSegments;
	for (unsigned int i = 0; i < nSegments; i++) {

		//parse segment length & compression setting
		int segCompressed = 1;
		int32_t thisSegmentSize = *(int32_t *) (buffer + 0x10 + i * 4);
		if (thisSegmentSize < 0) {
			segCompressed = 0;
			thisSegmentSize = -thisSegmentSize;
		}

		//decompress (if applicable)
		unsigned int thisSegmentUncompressedSize;
		if (segCompressed) {
			char *thisSegment = lz11decompress(buffer + offset, thisSegmentSize, &thisSegmentUncompressedSize);
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

char *lz11CompHeaderCompress(char *buffer, int size, int *compressedSize) {
	unsigned int nSegments = (size + 0xFFF) / 0x1000;  //following LEGO Battles precedent
	unsigned int headerSize = 0x10 + 4 * nSegments;
	char *header = (char *) calloc(headerSize, 1);

	*(unsigned *) (header + 0) = 'COMP';
	*(unsigned *) (header + 4) = size;
	*(unsigned *) (header + 8) = nSegments;

	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);
	bstreamWrite(&stream, header, headerSize); //bstreamCreate bug workaround
	free(header);

	unsigned longestCompress = 0;
	unsigned bytesRemaining = size;

	int i = 0;
	unsigned int offs = 0;
	while (bytesRemaining > 0) {
		unsigned thisRunLength = 0x1000;
		if (thisRunLength > bytesRemaining) thisRunLength = bytesRemaining;

		unsigned int thisRunCompressedSize;
		char *thisRunCompressed = lz11compress(buffer + offs, thisRunLength, &thisRunCompressedSize);
		bstreamWrite(&stream, thisRunCompressed, thisRunCompressedSize);
		free(thisRunCompressed);

		if (thisRunCompressedSize > longestCompress) longestCompress = thisRunCompressedSize;
		bytesRemaining -= thisRunLength;

		*(unsigned *) (stream.buffer + 0x10 + i * 4) = thisRunCompressedSize;
		offs += thisRunLength;
		i++;
	}
	*(unsigned *) (stream.buffer + 0xC) = longestCompress;

	*compressedSize = stream.size;
	return stream.buffer;
}

int getCompressionType(char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) return COMPRESSION_LZ77;
	if (lz11IsCompressed(buffer, size)) return COMPRESSION_LZ11;
	if (lz11CompHeaderIsValid(buffer, size)) return COMPRESSION_LZ11_COMP_HEADER;
	if (huffman4IsCompressed(buffer, size)) return COMPRESSION_HUFFMAN_4;
	if (huffman8IsCompressed(buffer, size)) return COMPRESSION_HUFFMAN_8;

	return COMPRESSION_NONE;
}

char *decompress(char *buffer, int size, int *uncompressedSize) {
	int type = getCompressionType(buffer, size);
	switch (type) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*uncompressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return lz77decompress(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11:
			return lz11decompress(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return lz11CompHeaderDecompress(buffer, size, uncompressedSize);
		case COMPRESSION_HUFFMAN_4:
		case COMPRESSION_HUFFMAN_8:
			return huffmanDecompress(buffer, size, uncompressedSize);
	}
	return NULL;
}

char *compress(char *buffer, int size, int compression, int *compressedSize) {
	switch (compression) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*compressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return lz77compress(buffer, size, compressedSize);
		case COMPRESSION_LZ11:
			return lz11compress(buffer, size, compressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return lz11CompHeaderCompress(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_4:
			return huffman4Compress(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_8:
			return huffman8Compress(buffer, size, compressedSize);
	}
	return NULL;
}