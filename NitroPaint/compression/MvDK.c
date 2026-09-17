#include <string.h>

#include "LZCore.h"
#include "HuffCore.h"
#include "bstream.h"
#include "compression.h"


#define MVDK_DUMMY        0  // "Dummy" compression (uncompressed with header)
#define MVDK_LZ           1  // LZ compression (same as BIOS with different header)
#define MVDK_DEFLATE      2  // simplified DEFLATE compression
#define MVDK_RLE          3  // RLE compression (same as BIOS with different header)
#define MVDK_INVALID     -1  // Indicate invalid compression


// ----- Common routines

static int CxiMvdkGetCompressionType(const unsigned char *buffer, unsigned int size) {
	return buffer[0] & 3;
}

static unsigned int CxiMvdkGetUncompressedSize(const unsigned char *buffer, unsigned int size) {
	uint32_t x = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
	return x >> 2;
}



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

static const DEFLATE_TABLE_ENTRY sDeflateLengthTable[] = {
	{ 0, 0x00 }, { 0, 0x01 }, { 0, 0x02 }, { 0, 0x03 }, { 0, 0x04 }, { 0, 0x05 }, { 0, 0x06 }, { 0, 0x07 },
	{ 1, 0x08 }, { 1, 0x0A }, { 1, 0x0C }, { 1, 0x0E }, { 2, 0x10 }, { 2, 0x14 }, { 2, 0x18 }, { 2, 0x1C },
	{ 3, 0x20 }, { 3, 0x28 }, { 3, 0x30 }, { 3, 0x38 }, { 4, 0x40 }, { 4, 0x50 }, { 4, 0x60 }, { 4, 0x70 },
	{ 5, 0x80 }, { 5, 0xA0 }, { 5, 0xC0 }, { 5, 0xE0 }, { 0, 0xFF }
};

static const DEFLATE_TABLE_ENTRY sDeflateOffsetTable[] = {
	{  0, 0x0000 }, {  0, 0x0001 }, {  0, 0x0002 }, {  0, 0x0003 },
	{  1, 0x0004 }, {  1, 0x0006 }, {  2, 0x0008 }, {  2, 0x000C },
	{  3, 0x0010 }, {  3, 0x0018 }, {  4, 0x0020 }, {  4, 0x0030 },
	{  5, 0x0040 }, {  5, 0x0060 }, {  6, 0x0080 }, {  6, 0x00C0 },
	{  7, 0x0100 }, {  7, 0x0180 }, {  8, 0x0200 }, {  8, 0x0300 },
	{  9, 0x0400 }, {  9, 0x0600 }, { 10, 0x0800 }, { 10, 0x0C00 },
	{ 11, 0x1000 }, { 11, 0x1800 }, { 12, 0x2000 }, { 12, 0x3000 },
	{ 13, 0x4000 }, { 13, 0x6000 }
};



// ----- Compression routines

static CxiLzToken *CxiMvdkTokenizeDeflate(const unsigned char *buffer, unsigned int size, unsigned int *pnTokens) {
	//tokenize greedily
	return CxiLzTokenizeGreedy(buffer, size, 3, 0x102, 1, 0x8000, pnTokens);
}

static int CxiMvdkLookupDeflateTableEntry(const DEFLATE_TABLE_ENTRY *table, int tableSize, unsigned int n) {
	for (int i = tableSize - 1; i >= 0; i--) {
		if (n >= table[i].majorPart) return i;
	}
	return 0;
}

static unsigned int CxiMvdkGetLengthCost(unsigned int length, CxiHuffCode *symCodes) {
	//get length node from table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3);

	//compute cost
	unsigned int cost = 0;
	cost += sDeflateLengthTable[idx].nMinorBits; // number of directly stored least significant bits
	cost += symCodes[idx + 0x100].length;        // number of bits to store the Huffman code for most significant bits
	return cost;
}

static unsigned int CxiMvdkGetDistanceCost(unsigned int distance, CxiHuffCode *distCodes) {
	//get distance node from table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateOffsetTable, 30, distance - 1);
	
	unsigned int cost = 0;
	cost += sDeflateOffsetTable[idx].nMinorBits; // number of directly stored least significant bits
	cost += distCodes[idx].length;               // number of bits to store the Huffman code for most significant bits
	return cost;
}

static unsigned int CxiMvdkGetByteCost(unsigned int symbol, CxiHuffCode *symCodes) {
	//return cost as direct stored symbol
	return symCodes[symbol].length;
}

static void CxiMvdkInsertDummyNode(CxiHuffNode *nodes, int nNodes) {
	//find first node with a 0 frequency and give it a dummy frequency.
	for (int i = 0; i < nNodes; i++) {
		if (nodes[i].freq > 0) continue;

		nodes[i].freq = 1;
		nodes[i].sym = i;
		break;
	}
}

static void CxiMvdkWriteHuffmanTree(CxiBitWriter *stream, CxiHuffCode *codes, int nCodes) {
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
			CxiBitWriterWriteBit(stream, 1);
			CxiBitWriterWriteBits(stream, nRunLength - 2, 7);
			CxiBitWriterWriteBits(stream, codes[i].length == 0 ? 0 : (codes[i].length - 1), 5);
		} else {
			CxiBitWriterWriteBit(stream, 0);
			CxiBitWriterWriteBits(stream, nRunLength - 1, 7);
			for (int j = 0; j < nRunLength; j++) {
				unsigned int length = codes[i + j].length;
				CxiBitWriterWriteBits(stream, length == 0 ? 0 : (length - 1), 5);
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
		node->sym = i;
		node->freq = symbolFrequencies[i];
		symbolTreeSize++;
	}
	for (int i = 0; i < 30; i++) {
		if (offsetBinFrequencies[i] == 0) continue;

		CxiHuffNode *node = offsetTree + lengthTreeSize;
		node->sym = i;
		node->freq = offsetBinFrequencies[i];
		lengthTreeSize++;
	}

	//if we have one node of a tree, insert a dummy node of low frequency. The decompressor
	//won't accept a 0-depth tree (nodes not added to the tree). So we need to ensure that
	//all leaf nodes have a depth of 1 or higher.
	if (symbolTreeSize < 2) symbolTreeSize = 2;
	if (lengthTreeSize < 2) lengthTreeSize = 2;

	//construct tree structure
	CxiHuffmanConstructTree(symbolTree, symbolTreeSize, 2);
	CxiHuffmanConstructTree(offsetTree, lengthTreeSize, 2);
	free(symbolFrequencies);
	free(offsetBinFrequencies);

	*pSymbolTree = symbolTree;
	*pOffsetTree = offsetTree;
}

static int CxiMvdkIsLengthAvailable(unsigned int length, CxiHuffCode *encLengths) {
	if (length == 1) return 1; // 1: direct byte (always available)
	if (length == 2) return 0; // 2: never available

	//get index of table
	int idx = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, length - 3);

	//nonzero length indicates it is within our Huffman code table
	return encLengths[0x100 + idx].length != 0;
}

static unsigned int CxiMvdkRoundDownLength(unsigned int length, CxiHuffCode *encLengths) {
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

static CxiLzToken *CxiMvdkRetokenize(const unsigned char *buffer, unsigned int size, int *pnTokens, CxiHuffNode *symbolTree, CxiHuffNode *offsetTree) {
	//create canonical tree and get encodings
	CxiHuffCode *lengthEncodings = (CxiHuffCode *) calloc(0x100 + 29, sizeof(CxiHuffCode));
	CxiHuffCode *offsetEncodings = (CxiHuffCode *) calloc(30, sizeof(CxiHuffCode));
	CxiHuffMakeCanonicalCodes(symbolTree, lengthEncodings, 0x100 + 29);
	CxiHuffMakeCanonicalCodes(offsetTree, offsetEncodings, 30);

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

	CxiLzNode *nodes = CxiLzGraphExplore(buffer, size, 3, 0x102, 1, 0x8000);

	unsigned int pos = size;
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
	CxiLzToken *tokens = CxiLzGraphToTokens(buffer, nodes, size, &nTokens);
	free(nodes);

	*pnTokens = nTokens;
	return tokens;
}

static unsigned char *CxiCompressMvdkDeflateChunk(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, unsigned int *nOutBits) {
	//first, tokenize the input string.
	unsigned int nTokens;
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
	CxiHuffCode *lengthEncodings = (CxiHuffCode *) calloc(0x100 + 29, sizeof(CxiHuffCode));
	CxiHuffCode *offsetEncodings = (CxiHuffCode *) calloc(30, sizeof(CxiHuffCode));
	CxiHuffMakeCanonicalCodes(symbolTree, lengthEncodings, 0x100 + 29);
	CxiHuffMakeCanonicalCodes(offsetTree, offsetEncodings, 30);
	free(symbolTree);
	free(offsetTree);

	//write huffman tree
	unsigned char *treeData = NULL;
	unsigned int treeSize = 0;
	{
		CxiBitWriter symbolStream, offsetStream;
		CxiBitWriterInit(&symbolStream);
		CxiBitWriterInit(&offsetStream);
		CxiMvdkWriteHuffmanTree(&symbolStream, lengthEncodings, 0x100 + 29);
		CxiMvdkWriteHuffmanTree(&offsetStream, offsetEncodings, 30);
		
		unsigned int nBytesSymbolTree = (symbolStream.length + 7) / 8;
		unsigned int nBytesOffsetTree = (offsetStream.length + 7) / 8;
		void *symbolTree = CxiBitWriterGetBytes(&symbolStream, 0, 1, 0, &nBytesSymbolTree);
		void *offsetTree = CxiBitWriterGetBytes(&offsetStream, 0, 1, 0, &nBytesOffsetTree);

		treeSize = 2 + nBytesSymbolTree + 2 + nBytesOffsetTree;
		treeData = (unsigned char *) malloc(treeSize);
		*(uint16_t *) (treeData + 0) = symbolStream.length;
		*(uint16_t *) (treeData + 2 + nBytesSymbolTree) = offsetStream.length;
		
		memcpy(treeData + 2, symbolTree, nBytesSymbolTree);
		memcpy(treeData + 2 + nBytesSymbolTree + 2, offsetTree, nBytesOffsetTree);
		
		CxiBitWriterFree(&symbolStream);
		CxiBitWriterFree(&offsetStream);
		free(symbolTree);
		free(offsetTree);
	}

	//TEST: write out bit stream
	CxiBitWriter bitStream;
	CxiBitWriterInit(&bitStream);
	for (unsigned int i = 0; i < nTokens; i++) {

		if (!tokens[i].isReference) {
			//CxiHuffmanWriteSymbol(&bitStream, tokens[i].symbol, symbolTree);
			CxiBitWriterWriteBitsBE(&bitStream, lengthEncodings[tokens[i].symbol].encoding, lengthEncodings[tokens[i].symbol].length - 1);
		} else {
			int lensym = CxiMvdkLookupDeflateTableEntry(sDeflateLengthTable, 29, tokens[i].length - 3);
			int offsym = CxiMvdkLookupDeflateTableEntry(sDeflateOffsetTable, 30, tokens[i].distance - 1);

			unsigned int lengthMinor = tokens[i].length - 3 - sDeflateLengthTable[lensym].majorPart;
			unsigned int offsetMinor = tokens[i].distance - 1 - sDeflateOffsetTable[offsym].majorPart;
			int nLengthMinor = sDeflateLengthTable[lensym].nMinorBits;
			int nOffsetMinor = sDeflateOffsetTable[offsym].nMinorBits;
			
			CxiBitWriterWriteBitsBE(&bitStream, lengthEncodings[0x100 + lensym].encoding, lengthEncodings[0x100 + lensym].length - 1);
			CxiBitWriterWriteBits(&bitStream, lengthMinor, nLengthMinor);

			CxiBitWriterWriteBitsBE(&bitStream, offsetEncodings[offsym].encoding, offsetEncodings[offsym].length - 1);
			CxiBitWriterWriteBits(&bitStream, offsetMinor, nOffsetMinor);
		}
	}
	free(tokens);

	free(lengthEncodings);
	free(offsetEncodings);

	//extract bytes of bit sequence
	unsigned int nBitsComp = bitStream.length;
	unsigned int compDataSize;
	unsigned char *bytes = CxiBitWriterGetBytes(&bitStream, 0, 1, 0, &compDataSize);
	CxiBitWriterFree(&bitStream);

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

unsigned char *CxCompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	unsigned int dummySize = size + 4;
	unsigned int lzSize, rlSize, dfSize;
	unsigned char *lz = CxCompressLZ(buffer, size, &lzSize);
	unsigned char *rl = CxCompressRL(buffer, size, &rlSize);
	unsigned char *df = CxiCompressMvdkDeflate(buffer, size, &dfSize);

	*(uint32_t *) lz = MVDK_LZ  | (size << 2);
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



// ----- Decompression routines

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


DEFLATE_TREE_NODE *CxiHuffmanReadTree(DEFLATE_WORK_BUFFER *auxBuffer, CxiBitReader *reader, DEFLATE_TREE_NODE *nodeBuffer, unsigned int nNodes) {
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
		if (CxiBitReaderReadBit(reader)) {
			//read 7-bit number from 2 to 129 (number of loop iterations)
			unsigned int nNodesBlock = CxiBitReaderReadBits(reader, 7) + 2;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			//this 5-bit value gets put into the depth of all nodes written here
			unsigned int depth = CxiBitReaderReadBits(reader, 5);
			if (reader->error) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				nodeBuffer[i + j].depth = depth;
				depthCounts[depth]++;
			}
			i += nNodesBlock;
		} else {
			//read 7-bit number from 1 to 128. Number of loop iterations.
			unsigned int nNodesBlock = CxiBitReaderReadBits(reader, 7) + 1;
			if (reader->error) return NULL;
			if (i + nNodesBlock > nNodes) return NULL;

			for (j = 0; j < nNodesBlock; j++) {
				uint8_t depth = CxiBitReaderReadBits(reader, 5);
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

uint32_t CxiLookupTreeNode(DEFLATE_TREE_NODE *node, CxiBitReader *reader) {
	if (node == NULL) return (uint32_t) -1;

	while (!node->isLeaf) {
		if (CxiBitReaderReadBit(reader)) {
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
	CxiBitReader reader;
	const unsigned char *pos = *pPos;
	uint32_t nBytesConsumed = 0;
	CxiBitReaderInit(&reader, pos, srcEnd, 0, 0);

	int isCompressed = CxiBitReaderReadBit(&reader);
	if (reader.error) return NULL;
	uint32_t chunkLen = CxiBitReaderReadBits(&reader, 31);
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
		uint32_t lzLen2 = CxiBitReaderReadBits(&reader, 16);
		uint32_t table1SizeBytes = (lzLen2 + 7) >> 3;
		const unsigned char *postTree = reader.pos + table1SizeBytes;
		DEFLATE_TREE_NODE *huffRoot1 = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->symbolNodeBuffer, 0x11D);
		if (huffRoot1 == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL; // Validate tree size

		//Reposition stream after the Huffman tree. Read out the LZ distance tree next.
		//Its size in bits is given by the following 16 bits from the stream.
		CxiBitReaderInit(&reader, postTree, srcEnd, 0, 0);
		reader.nBitsRead = (postTree - pos) * 8;
		lzLen2 = CxiBitReaderReadBits(&reader, 16);
		uint32_t table2SizeBytes = (lzLen2 + 7) >> 3;

		postTree = reader.pos + table2SizeBytes;
		DEFLATE_TREE_NODE *huffDistancesRoot = CxiHuffmanReadTree(auxBuffer, &reader, auxBuffer->lengthNodeBuffer, 0x1E);
		if (huffDistancesRoot == NULL) return NULL; // Huffman tree error
		if (postTree > srcEnd) return NULL;         // Validate tree size

		//Reposition stream after this tree to prepare for reading the compressed sequence.
		CxiBitReaderInit(&reader, postTree, srcEnd, 0, 0);
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
				uint32_t lzLen2 = CxiBitReaderReadBits(&reader, nLengthMinorBits);
				uint32_t lzLen = lzLen1 + lzLen2 + 3;

				//read out offset
				uint32_t nodeVal2 = CxiLookupTreeNode(huffDistancesRoot, &reader);
				if (nodeVal2 == (uint32_t) -1) return NULL;

				uint32_t nOffsetMinorBits = sDeflateOffsetTable[nodeVal2].nMinorBits;
				uint32_t lzOffset1 = sDeflateOffsetTable[nodeVal2].majorPart;
				uint32_t lzOffset2 = CxiBitReaderReadBits(&reader, nOffsetMinorBits);
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



static unsigned char *CxiMvdkDecompressDummy(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int outlen = CxiMvdkGetUncompressedSize(buffer, size);
	unsigned char *out = (unsigned char *) malloc(outlen);

	memcpy(out, buffer + 4, outlen);
	*uncompressedSize = outlen;
	return out;
}

static unsigned char *CxiMvdkDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int outlen = CxiMvdkGetUncompressedSize(buffer, size);

	unsigned char *copy = (unsigned char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (outlen << 8);
	unsigned char *out = CxDecompressLZ(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int outlen = CxiMvdkGetUncompressedSize(buffer, size);

	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (outlen << 8);
	char *out = CxDecompressRL(copy, size, uncompressedSize);
	free(copy);

	return out;
}

static unsigned char *CxiMvdkDecompressDeflate(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	unsigned int outlen = CxiMvdkGetUncompressedSize(buffer, size);
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



// ----- Validation routines

static int CxiMvdkIsValidDeflate(const unsigned char *buffer, unsigned int size) {
	const unsigned char *pos = buffer + 4;
	unsigned char *dest = NULL; // won't be written to
	unsigned char *destBase = dest;
	unsigned char *end = dest + ((*(uint32_t *) buffer) >> 2); // for address comparison
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

static int CxiMvdkIsValidLZ(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = CxiMvdkGetUncompressedSize(buffer, size);
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x10 | (uncompSize << 8);
	int valid = CxIsCompressedLZ(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidRL(const unsigned char *buffer, unsigned int size) {
	//same format as standard LZ, with different header
	uint32_t uncompSize = CxiMvdkGetUncompressedSize(buffer, size);
	char *copy = (char *) malloc(size);
	memcpy(copy, buffer, size);
	*(uint32_t *) copy = 0x30 | (uncompSize << 8);
	int valid = CxIsCompressedRL(copy, size);
	free(copy);
	return valid;
}

static int CxiMvdkIsValidDummy(const unsigned char *buffer, unsigned int size) {
	//check the size
	uint32_t uncompSize = CxiMvdkGetUncompressedSize(buffer, size);
	return (((size - 4 + 3) & ~3) == ((uncompSize + 3) & ~3)) && ((size - 4) >= uncompSize);
}

int CxIsCompressedMvDK(const unsigned char *buffer, unsigned int size) {
	//all types must have space for the header
	if (size < 4) return 0;

	switch (CxiMvdkGetCompressionType(buffer, size)) {
		case MVDK_DUMMY:
			return CxiMvdkIsValidDummy(buffer, size);
		case MVDK_LZ:
			return CxiMvdkIsValidLZ(buffer, size);
		case MVDK_RLE:
			return CxiMvdkIsValidRL(buffer, size);
		case MVDK_DEFLATE:
			return CxiMvdkIsValidDeflate(buffer, size);
	}
	return 0;
}
