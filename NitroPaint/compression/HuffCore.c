#include "HuffCore.h"

static int CxiHuffFrequencyComparator(const void *p1, const void *p2) {
	const CxiHuffNode *n1 = (const CxiHuffNode *) p1;
	const CxiHuffNode *n2 = (const CxiHuffNode *) p2;

	//sort first according to descending frequency
	if (n2->freq != n1->freq) return n2->freq - n1->freq;

	//sort secondarily by symbol value (low symbols first)
	if (n1->sym < n2->sym) return -1;
	if (n1->sym > n2->sym) return  1;
	return 0;
}

static int CxiHuffCanonicalComparator(const void *p1, const void *p2) {
	const CxiHuffCode *c1 = (const CxiHuffCode *) p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *) p2;

	//force 0-length (excluded) symbols to the end
	if (c1->length == 0) return  1;
	if (c2->length == 0) return -1;

	if (c1->length < c2->length) return -1;
	if (c1->length > c2->length) return  1;
	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return  1;
	return 0;
}

static int CxiHuffSymbolComparator(const void *p1, const void *p2) {
	const CxiHuffCode *c1 = (const CxiHuffCode *) p1;
	const CxiHuffCode *c2 = (const CxiHuffCode *) p2;

	if (c1->value < c2->value) return -1;
	if (c1->value > c2->value) return  1;
	return 0;
}

int CxiHuffmanHasSymbol(CxiHuffNode *node, uint16_t sym) {
	if (ISLEAF(node)) return node->sym == sym;
	if (sym < node->symMin || sym > node->symMax) return 0;

	return CxiHuffmanHasSymbol(node->left, sym) || CxiHuffmanHasSymbol(node->right, sym);
}

unsigned int CxiHuffmanConstructTree(CxiHuffNode *nodes, unsigned int nNodes, unsigned int nNodeMin) {
	//initialize symMin, symMax
	for (unsigned int i = 0; i < nNodes; i++) {
		nodes[i].symMin = nodes[i].symMax = nodes[i].sym;
	}

	//sort by frequency, then cut off the remainder (freq=0).
	qsort(nodes, nNodes, sizeof(CxiHuffNode), CxiHuffFrequencyComparator);
	for (unsigned int i = 0; i < nNodes; i++) {
		if (nodes[i].freq == 0) {
			nNodes = i;
			break;
		}
	}
	if (nNodes < nNodeMin) nNodes = nNodeMin;

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

		nRoots--;
		nTotalNodes++;
		qsort(nodes, nRoots, sizeof(CxiHuffNode), CxiHuffFrequencyComparator);
	}

	return nNodes;
}

static int CxiHuffAppendCanonicalCode(CxiHuffNode *tree, CxiHuffCode *codes, uint32_t encoding, int depth) {
	if (ISLEAF(tree)) {
		codes[tree->sym].length = depth;
		return 1;
	}

	//recurse
	int nl = CxiHuffAppendCanonicalCode(tree->left, codes, (encoding << 1) | 0, depth + 1);
	int nr = CxiHuffAppendCanonicalCode(tree->right, codes, (encoding << 1) | 1, depth + 1);
	return nl + nr;
}

void CxiHuffMakeCanonicalCodes(CxiHuffNode *tree, CxiHuffCode *codes, int nMaxNodes) {
	//first, recursively append to the list.
	int nNodes = CxiHuffAppendCanonicalCode(tree, codes, 0, 1);
	for (int i = 0; i < nMaxNodes; i++) {
		codes[i].value = i;
	}

	//next, apply sort. Unassigned codes are pushed to the end of the list.
	qsort(codes, nMaxNodes, sizeof(CxiHuffCode), CxiHuffCanonicalComparator);

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
	qsort(codes, nMaxNodes, sizeof(CxiHuffCode), CxiHuffSymbolComparator);
}

void CxiHuffmanWriteSymbol(CxiBitWriter *bits, uint16_t sym, const CxiHuffNode *tree) {
	if (ISLEAF(tree)) return;

	if (CxiHuffmanHasSymbol(tree->left, sym)) {
		CxiBitWriterWriteBit(bits, 0);
		CxiHuffmanWriteSymbol(bits, sym, tree->left);
	} else {
		CxiBitWriterWriteBit(bits, 1);
		CxiHuffmanWriteSymbol(bits, sym, tree->right);
	}
}
