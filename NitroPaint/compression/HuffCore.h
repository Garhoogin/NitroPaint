#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "CxPrivate.h"

typedef struct CxiHuffNode_ {
	uint16_t sym;
	uint16_t symMin;
	uint16_t symMax;
	int freq;
	struct CxiHuffNode_ *left;
	struct CxiHuffNode_ *right;
} CxiHuffNode;

typedef struct CxiHuffCode_ {
	uint16_t value;      // code value
	uint16_t length;     // code length
	uint32_t encoding;   // code representation
} CxiHuffCode;

#define ISLEAF(n) ((n)->left==NULL&&(n)->right==NULL)


int CxiHuffmanHasSymbol(
	CxiHuffNode *node,
	uint16_t     sym
);

unsigned int CxiHuffmanConstructTree(
	CxiHuffNode *nodes,
	unsigned int nNodes,
	unsigned int nNodeMin
);

void CxiHuffMakeCanonicalCodes(
	CxiHuffNode *tree,
	CxiHuffCode *codes,
	int          nMaxNodes
);

void CxiHuffmanWriteSymbol(
	CxiBitWriter      *bits,
	uint16_t           sym,
	const CxiHuffNode *tree
);
