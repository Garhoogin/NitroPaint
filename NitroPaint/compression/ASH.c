#include <string.h>

#include "LZCore.h"
#include "HuffCore.h"


// ----- Format constants

#define ASH_SYM_BITS      9  // number of bits in main symbol/length table
#define ASH_DST_BITS     11  // number of bits in distance table
#define ASH_MIN_LENGTH    3  // minimum length copy


// ----- Compression routines

static uint32_t BigToLittle32(uint32_t x) {
	return CxiByteSwap(x);
}

static void CxiAshWriteTree(CxiBitWriter *stream, CxiHuffNode *nodes, int nBits) {
	if (nodes->left != NULL) {
		//
		CxiBitWriterWriteBit(stream, 1);
		CxiAshWriteTree(stream, nodes->left, nBits);
		CxiAshWriteTree(stream, nodes->right, nBits);
	} else {
		//write value
		CxiBitWriterWriteBit(stream, 0);
		CxiBitWriterWriteBitsBE(stream, nodes->sym, nBits);
	}
}

static CxiLzToken *CxiAshTokenize(const unsigned char *buffer, unsigned int size, int nSymBits, int nDstBits, unsigned int *pnTokens) {
	return CxiLzTokenizeGreedy(buffer, size, ASH_MIN_LENGTH, (1 << nSymBits) - 1 - 0x100 + ASH_MIN_LENGTH, 1, (1 << nDstBits), pnTokens);
}

unsigned char *CxCompressAsh(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	int nSymBits = ASH_SYM_BITS, nDstBits = ASH_DST_BITS;

	int nSymNodes = (1 << nSymBits);
	int nDstNodes = (1 << nDstBits);
	CxiHuffNode *symNodes = (CxiHuffNode *) calloc(nSymNodes * 2, sizeof(CxiHuffNode));
	CxiHuffNode *dstNodes = (CxiHuffNode *) calloc(nDstNodes * 2, sizeof(CxiHuffNode));

	for (int i = 0; i < nSymNodes; i++) symNodes[i].sym = i;
	for (int i = 0; i < nDstNodes; i++) dstNodes[i].sym = i;

	//tokenize
	unsigned int nTokens = 0;
	CxiLzToken *tokens = CxiAshTokenize(buffer, size, nSymBits, nDstBits, &nTokens);

	//construct frequency distribution
	for (unsigned int i = 0; i < nTokens; i++) {
		CxiLzToken *token = &tokens[i];
		if (token->isReference) {
			symNodes[token->length - ASH_MIN_LENGTH + 0x100].freq++;
			dstNodes[token->distance - 1].freq++;
		} else {
			symNodes[token->symbol].freq++;
		}
	}

	//construct trees
	CxiHuffmanConstructTree(symNodes, nSymNodes, 2);
	CxiHuffmanConstructTree(dstNodes, nDstNodes, 2);

	//init streams
	CxiBitWriter symStream, dstStream;
	CxiBitWriterInit(&symStream);
	CxiBitWriterInit(&dstStream);

	//first, write huffman trees.
	CxiAshWriteTree(&symStream, symNodes, nSymBits);
	CxiAshWriteTree(&dstStream, dstNodes, nDstBits);

	//write data stream
	for (unsigned int i = 0; i < nTokens; i++) {
		CxiLzToken *token = &tokens[i];

		if (token->isReference) {
			CxiHuffmanWriteSymbol(&symStream, token->length - ASH_MIN_LENGTH + 0x100, symNodes);
			CxiHuffmanWriteSymbol(&dstStream, token->distance - 1, dstNodes);
		} else {
			CxiHuffmanWriteSymbol(&symStream, token->symbol, symNodes);
		}
	}
	free(tokens);
	free(symNodes);
	free(dstNodes);

	//encode data output
	unsigned int symStreamSize = 0, dstStreamSize = 0;
	void *symBytes = CxiBitWriterGetBytes(&symStream, 1, 1, 1, &symStreamSize);
	void *dstBytes = CxiBitWriterGetBytes(&dstStream, 1, 1, 1, &dstStreamSize);

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
	CxiBitWriterFree(&symStream);
	CxiBitWriterFree(&dstStream);

	*compressedSize = 0xC + symStreamSize + dstStreamSize;
	return out;
}



// ----- Decompression routines

#define TREE_RIGHT    0x80000000
#define TREE_LEFT     0x40000000
#define TREE_VAL_MASK 0x3FFFFFFF

uint32_t CxAshReadTree(CxiBitReader *reader, int width, uint32_t *leftTree, uint32_t *rightTree) {
	uint32_t *workmem = (uint32_t *) calloc(2 * (1 << width), sizeof(uint32_t));
	uint32_t *work = workmem;

	uint32_t r23 = (1 << width);
	uint32_t symRoot;
	uint32_t nNodes = 0;
	do {
		int bit = CxiBitReaderReadBit(reader);
		if (reader->error) goto Error;

		if (bit) {
			if (r23 >= (2 * (1u << width)) || nNodes >= (2 * (1u << width))) goto Error;

			*(work++) = r23 | TREE_RIGHT;
			*(work++) = r23 | TREE_LEFT;
			nNodes += 2;
			r23++;
		} else {
			if (nNodes == 0) goto Error;

			symRoot = CxiBitReaderReadBits(reader, width);
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

unsigned char *CxDecompressAsh(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	int symBits = ASH_SYM_BITS, distBits = ASH_DST_BITS;
	uint32_t uncompSize = BigToLittle32(*(uint32_t *) (buffer + 4)) & 0x00FFFFFF;
	uint32_t outSize = uncompSize;

	uint8_t *outbuf = calloc(uncompSize, 1);
	uint8_t *destp = outbuf;

	CxiBitReader reader, reader2;
	CxiBitReaderInit(&reader, buffer + BigToLittle32(*(const uint32_t *) (buffer + 0x8)), buffer + size, 1, 1);
	CxiBitReaderInit(&reader2, buffer + 0xC, buffer + size, 1, 1);

	uint32_t symMax = (1 << symBits);
	uint32_t distMax = (1 << distBits);

	//HACK, pointer to RAM
	uint32_t *symLeftTree   = calloc(2 * symMax  - 1, sizeof(uint32_t));
	uint32_t *symRightTree  = calloc(2 * symMax  - 1, sizeof(uint32_t));
	uint32_t *distLeftTree  = calloc(2 * distMax - 1, sizeof(uint32_t));
	uint32_t *distRightTree = calloc(2 * distMax - 1, sizeof(uint32_t));

	uint32_t symRoot, distRoot;
	symRoot = CxAshReadTree(&reader2, symBits, symLeftTree, symRightTree);
	distRoot = CxAshReadTree(&reader, distBits, distLeftTree, distRightTree);

	//main uncompress loop
	do {
		uint32_t sym = symRoot;
		while (sym >= symMax) {
			if (!CxiBitReaderReadBit(&reader2)) {
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
				if (!CxiBitReaderReadBit(&reader)) {
					distsym = distLeftTree[distsym];
				} else {
					distsym = distRightTree[distsym];
				}
			}

			uint32_t copylen = (sym - 0x100) + ASH_MIN_LENGTH;
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


// ----- Validation routines

int CxIsCompressedAsh(const unsigned char *buffer, unsigned int size) {
	//for our purposes, constants (until we decide otherwise)
	int symBits = ASH_SYM_BITS, distBits = ASH_DST_BITS;
	int valid = 0;

	//check header
	if (size < 0xC || memcmp(buffer, "ASH", 3) != 0) return 0;

	uint32_t uncompSize = BigToLittle32(*(uint32_t *) (buffer + 4)) & 0x00FFFFFF;
	uint32_t outSize = uncompSize;

	CxiBitReader reader, reader2;
	uint32_t offsDist = BigToLittle32(*(const uint32_t *) (buffer + 0x8));
	if (offsDist < 0xC || offsDist >= size) return 0;

	CxiBitReaderInit(&reader,  buffer + offsDist, buffer + size, 1, 1);
	CxiBitReaderInit(&reader2, buffer + 0xC,      buffer + size, 1, 1);

	uint32_t symMax  = (1 << symBits);
	uint32_t distMax = (1 << distBits);

	//alloc trees
	uint32_t *symLeftTree   = calloc(2 * symMax  - 1, sizeof(uint32_t));
	uint32_t *symRightTree  = calloc(2 * symMax  - 1, sizeof(uint32_t));
	uint32_t *distLeftTree  = calloc(2 * distMax - 1, sizeof(uint32_t));
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
			int bit = CxiBitReaderReadBit(&reader2);
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
				int bit = CxiBitReaderReadBit(&reader);
				if (reader.error) goto Cleanup;

				if (!bit) {
					distsym = distLeftTree[distsym];
				} else {
					distsym = distRightTree[distsym];
				}
			}

			//assert valid source and length
			uint32_t copylen = (sym - 0x100) + ASH_MIN_LENGTH;
			uint32_t copydst = distsym + 1;
			if (copylen > uncompSize || copydst > outpos) goto Cleanup;

			outpos += copylen;
			uncompSize -= copylen;
		}
	} while (uncompSize > 0);
	valid = 1;

Cleanup:
	if (symLeftTree   != NULL) free(symLeftTree);
	if (symRightTree  != NULL) free(symRightTree);
	if (distLeftTree  != NULL) free(distLeftTree);
	if (distRightTree != NULL) free(distRightTree);
	return valid;
}
