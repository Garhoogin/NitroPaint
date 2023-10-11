#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "nns.h"

int NnsG2dIsValid(const unsigned char *buffer, unsigned int size) {
	if (size < 0x10) return 0;
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	if (endianness != 0xFFFE && endianness != 0xFEFF && endianness != 0) return 0;

	int isOld = (endianness == 0);

	uint32_t fileSize = *(uint32_t *) (buffer + 8);
	if (fileSize != size && !(fileSize + 8 == size && isOld)) { //old G2D Runtime subtracts 8
		fileSize = (fileSize + 3) & ~3; 
		if (fileSize != size) return 0;
	}
	uint16_t headerSize = *(uint16_t *) (buffer + 0xC);
	if (headerSize < 0x10) return 0;
	int nSections = *(uint16_t *) (buffer + 0xE);
	

	//check sections
	unsigned int offset = headerSize;
	for (int i = 0; i < nSections; i++) {
		if (offset + 8 > size) return 0;

		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		offset += size;
		if (isOld) offset += 8;
	}

	return 1;
}

int NnsG2dIsOld(const unsigned char *buffer, unsigned int size) {
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	return endianness == 0;
}

int NnsG2dGetNumberOfSections(const unsigned char *buffer, unsigned int size) {
	return *(uint16_t *) (buffer + 0xE);
}

unsigned char *NnsG2dGetSectionByIndex(const unsigned char *buffer, unsigned int size, int index) {
	if (index >= NnsG2dGetNumberOfSections(buffer, size)) return NULL;

	uint16_t endianness = *(uint16_t *) (buffer + 4);
	int isOld = (endianness == 0);

	uint32_t offset = *(uint16_t *) (buffer + 0xC);
	for (int i = 0; i <= index; i++) {
		if (offset + 8 > size) return NULL;
		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		if (isOld) size += 8;

		if (i == index) return (unsigned char *) (buffer + offset);
		offset += size;
	}
	return NULL;
}

unsigned char *NnsG2dGetSectionByMagic(const unsigned char *buffer, unsigned int size, unsigned int sectionMagic) {
	int nSections = NnsG2dGetNumberOfSections(buffer, size);
	uint32_t offset = *(uint16_t *) (buffer + 0xC);
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	int isOld = (endianness == 0);

	for (int i = 0; i <= nSections; i++) {
		if (offset + 8 > size) return NULL;
		uint32_t magic = *(uint32_t *) (buffer + offset);
		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		if (isOld) size += 8;

		if (magic == sectionMagic) return (unsigned char *) (buffer + offset); //cast away const
		offset += size;
	}
	return NULL;
}


// ----- NNS G3D functions


typedef struct NnsG3dTreeNode_ {
	int leafIndex; //to control output index for a leaf node (set by user)
	char name[16]; //name (for leaf nodes, read by user)

	//internals
	int isLeaf;
	int refBit;    //for leaf nodes - equal to parent refBit
	int writtenTo; //for branch nodes - the index in the P-tree that this was written to
				   //for leaf nodes - the index in the P-tree that holds its leaf info
	struct NnsG3dTreeNode_ *parent;

	//for branch nodes
	struct NnsG3dTreeNode_ *left;
	struct NnsG3dTreeNode_ *right;
} NnsG3dTreeNode;

static int NnsiG3dGetResourceNameBit(const char *name, unsigned int bit) {
	return (name[bit / 8] >> (bit % 8)) & 1;
}

static int NnsiG3dFindBitDivergence(const char *const *names, int nNames) {
	if (nNames <= 1) return -1;

	//find a divergence. Return highest index of this.
	for (int i = 127; i >= 0; i--) {
		//check if any have a different value
		int diverged = 0;
		int b0 = NnsiG3dGetResourceNameBit(names[0], i);
		for (int j = 1; j < nNames; j++) {
			int bj = NnsiG3dGetResourceNameBit(names[j], i);
			if (bj != b0) {
				diverged = 1;
				break;
			}
		}

		//if we diverged here, return i.
		if (diverged) return i;
	}
	return -1;
}

static int g_bitCompareBit = 0;
static int NnsiG3dBitComparator(const void *p1, const void *p2) {
	const char *name1 = *(const char **) p1;
	const char *name2 = *(const char **) p2;
	int bit = g_bitCompareBit;

	//p1 - p2
	return NnsiG3dGetResourceNameBit(name1, bit) - NnsiG3dGetResourceNameBit(name2, bit);
}

static NnsG3dTreeNode *NnsiG3dConstructTreeRecursive(const char **names, int nNames) {
	NnsG3dTreeNode *root = (NnsG3dTreeNode *) calloc(1, sizeof(NnsG3dTreeNode));
	root->parent = NULL;
	if (nNames == 1) {
		root->isLeaf = 1;
		root->refBit = 0x80;
		memcpy(root->name, *names, 16);
		return root;
	}
	root->isLeaf = 0;

	//find first bit in the set of names that diverges. (duplicate resources prohibited!!)
	int divergedBit = NnsiG3dFindBitDivergence(names, nNames);
	root->refBit = divergedBit;
	if (divergedBit == -1) {
		free(root);
		return NULL;
	}

	//trash the name list? Of course, for sorting!
	g_bitCompareBit = divergedBit;
	qsort((void *) names, nNames, sizeof(char *), NnsiG3dBitComparator);

	//create children
	int nLeftNodes = nNames;
	for (int i = 0; i < nNames; i++) {
		int b = NnsiG3dGetResourceNameBit(names[i], divergedBit);
		if (b) {
			nLeftNodes = i;
			break;
		}
	}

	//create left+right
	root->left = NnsiG3dConstructTreeRecursive(names, nLeftNodes);
	root->right = NnsiG3dConstructTreeRecursive(names + nLeftNodes, nNames - nLeftNodes);
	root->left->parent = root;
	root->right->parent = root;
	if (root->left->isLeaf) root->left->refBit = root->refBit;
	if (root->right->isLeaf) root->right->refBit = root->refBit;
	return root;
}

static NnsG3dTreeNode *NnsiG3dLookupResource(NnsG3dTreeNode *tree, const char *name) {
	//traverse tree
	while (!tree->isLeaf) {
		int refBit = tree->refBit;
		int bit = NnsiG3dGetResourceNameBit(name, refBit);

		tree = bit ? tree->right : tree->left;
	}
	if (memcmp(tree->name, name, 16) == 0) return tree;
	return NULL;
}

static NnsG3dTreeNode *NnsiG3dConstructTree(const char *const *names, int nNames) {
	//create buffer for sorting
	char **namesCopy = (char **) calloc(nNames, sizeof(char *));
	char *namesBlob = (char *) calloc(nNames, 16);
	for (int i = 0; i < nNames; i++) {
		char *base = namesBlob + 16 * i;
		namesCopy[i] = base;

		//copy name (NUL-pad if <16 chars)
		int pad = 0;
		for (int j = 0; j < 16; j++) {
			if (!pad) {
				base[j] = names[i][j];
				if (base[j] == '\0') pad = 0;
			} else {
				base[j] = '\0';
			}
		}
	}

	NnsG3dTreeNode *constructed = NnsiG3dConstructTreeRecursive(namesCopy, nNames);
	free(namesCopy);
	free(namesBlob);

	//set out indices
	for (int i = 0; i < nNames; i++) {
		NnsG3dTreeNode *thisLeaf = NnsiG3dLookupResource(constructed, names[i]);
		thisLeaf->leafIndex = i;
	}
	return constructed;
}

static void NnsiG3dFreeTree(NnsG3dTreeNode *root) {
	if (root->left != NULL) {
		NnsiG3dFreeTree(root->left);
		free(root->left);
	}
	if (root->right != NULL) {
		NnsiG3dFreeTree(root->right);
		free(root->right);
	}
	memset(root, 0, sizeof(NnsG3dTreeNode));
}

static void NnsiG3dWriteNode(BSTREAM *stream, NnsG3dTreeNode *node, int *baseIndex) {
	//if this node is a leaf, don't write anything
	node->writtenTo = -1; //not written
	if (node->isLeaf) return;

	//set node index
	node->leafIndex = -1; //not assigned to any leaf nodes (yet)
	node->writtenTo = *baseIndex; //was actually written

	//count child leaf nodes
	int nChildLeaves = node->left->isLeaf + node->right->isLeaf;

	//prepare node output
	uint8_t data[4] = { 0 };
	int thisOffset = stream->pos;
	data[0] = node->refBit;
	bstreamWrite(stream, data, sizeof(data));
	(*baseIndex)++;

	//write children
	NnsiG3dWriteNode(stream, node->left, baseIndex);
	NnsiG3dWriteNode(stream, node->right, baseIndex);
	int afterOffset = stream->pos;

	//if our children aren't leaf nodes, build references
	if (!node->left->isLeaf) {
		data[1] = node->left->writtenTo;
	}
	if (!node->right->isLeaf) {
		data[2] = node->right->writtenTo;
	}

	//if we have a child leaf, point it here. Point right first, then left (g3dcvtr does this?)
	if (node->right->isLeaf) {
		node->right->writtenTo = node->writtenTo;
		node->leafIndex = node->right->leafIndex;
		data[2] = node->writtenTo;
		data[3] = node->leafIndex;
	} else if (node->left->isLeaf) {
		node->left->writtenTo = node->writtenTo;
		node->leafIndex = node->left->leafIndex;
		data[1] = node->writtenTo;
		data[3] = node->leafIndex;
	}

	//if both children are leaf nodes, only the right node will have been set. node->right->writtenTo
	//will still be -1 by now. This will be fixed up later.

	bstreamSeek(stream, thisOffset, 0);
	bstreamWrite(stream, data, sizeof(data));

	//restore to end
	bstreamSeek(stream, afterOffset, 0);
}

static NnsG3dTreeNode *NnsiG3dFindUnassignedLeaf(NnsG3dTreeNode *tree) {
	//find a node with writtenTo == -1. If multiple, find the one with higher refBit.
	if (tree->isLeaf) {
		return tree->writtenTo == -1 ? tree : NULL;
	}

	NnsG3dTreeNode *foundLeft = NnsiG3dFindUnassignedLeaf(tree->left);
	NnsG3dTreeNode *foundRight = NnsiG3dFindUnassignedLeaf(tree->right);
	if (foundLeft == NULL && foundRight == NULL) return NULL;
	if (foundLeft != NULL && foundRight == NULL) return foundLeft;
	if (foundLeft == NULL && foundRight != NULL) return foundRight;

	if (foundLeft->refBit > foundRight->refBit) return foundLeft;
	return foundRight;
}

static NnsG3dTreeNode *NnsiG3dFindUnassignedBranch(NnsG3dTreeNode *tree) {
	//consider all nodes, not just children. If this node is a leaf, return NULL
	if (tree->isLeaf) return NULL;
	if (tree->left->isLeaf && tree->right->isLeaf) {
		return tree->leafIndex == -1 ? tree : NULL;
	}

	//search children
	NnsG3dTreeNode *foundLeft = NnsiG3dFindUnassignedBranch(tree->left);
	NnsG3dTreeNode *foundRight = NnsiG3dFindUnassignedBranch(tree->right);

	NnsG3dTreeNode *bestNode = NULL;
	if (tree->leafIndex == -1) bestNode = tree;
	if (foundLeft != NULL) {
		if (bestNode == NULL || foundLeft->refBit > bestNode->refBit) bestNode = foundLeft;
	}
	if (foundRight != NULL) {
		if (bestNode == NULL || foundRight->refBit > bestNode->refBit) bestNode = foundRight;
	}

	return bestNode;
}

static void NnsiG3dSerializeTree(BSTREAM *stream, NnsG3dTreeNode *tree) {
	//write node 0 header
	int baseOffset = stream->pos;
	{
		uint8_t baseHeader[4] = { 0x7F, 1, 0, 0 };
		if (tree == NULL) baseHeader[1] = 0; //indicate empty tree to NNS
		bstreamWrite(stream, baseHeader, sizeof(baseHeader));
	}
	if (tree == NULL) return;

	//start writing each node
	int baseIndex = 1;
	NnsiG3dWriteNode(stream, tree, &baseIndex);

	//write final node. Since we've only written branches, we need one more for a leaf.
	int dummyNodePos = stream->pos;
	{
		uint8_t lastNode[4] = { 0, 0, 0, 0 };
		bstreamWrite(stream, lastNode, sizeof(lastNode));
	}
	int endPos = stream->pos;

	//next, fixup unassigned child nodes. Keep searching for the unassigned child node
	//of highest refBit, then assign it a branch.
	while (1) {
		NnsG3dTreeNode *unassigned = NnsiG3dFindUnassignedLeaf(tree);
		if (unassigned == NULL) break;

		//search for branch node not assigned to a leaf of highest refBit.
		NnsG3dTreeNode *toAssign = NnsiG3dFindUnassignedBranch(tree);
		uint8_t idxBuffer = 0;
		int destNodePos = dummyNodePos;
		if (toAssign != NULL) { //a node existed, write out info to it
			toAssign->leafIndex = unassigned->leafIndex;
			unassigned->writtenTo = toAssign->writtenTo;
			destNodePos = baseOffset + toAssign->writtenTo * 4;
		}

		//write back leaf index
		idxBuffer = unassigned->leafIndex;
		bstreamSeek(stream, destNodePos + 3, 0);
		bstreamWrite(stream, &idxBuffer, sizeof(idxBuffer));

		//if we're writing to the dummy node, set refBit to 0x7F to ensure it's interpreted as a leaf
		if (destNodePos == dummyNodePos) {
			uint8_t refBitBuffer = 0x7F;
			bstreamSeek(stream, dummyNodePos, 0);
			bstreamWrite(stream, &refBitBuffer, sizeof(refBitBuffer));
			unassigned->writtenTo = (dummyNodePos - baseOffset) / 4;
		}

		//update parent to point to the child location (after we're sure writtenTo is set!)
		NnsG3dTreeNode *leafParent = unassigned->parent;
		if (leafParent != NULL) {
			uint8_t childIndexBuffer = unassigned->writtenTo;
			int leftRight = leafParent->left == unassigned ? 0 : 1; //0 for left, 1 for right
			bstreamSeek(stream, baseOffset + leafParent->writtenTo * 4 + 1 + leftRight, 0);
			bstreamWrite(stream, &childIndexBuffer, sizeof(childIndexBuffer));
		}
	}
	bstreamSeek(stream, endPos, 0);
}

static void NnsiG3dConstructTreeFromResources(BSTREAM *stream, void *items, int itemSize, int nItems, NnsGetResourceNameCallback getNamePtr) {
	char **namesBuf = (char **) calloc(nItems, sizeof(char *));
	char *namesBlob = (char *) calloc(nItems, 16);
	for (int i = 0; i < nItems; i++) {
		namesBuf[i] = namesBlob + i * 16;
		void *obj = (void *) (i * itemSize + (uintptr_t) items);
		memcpy(namesBuf[i], getNamePtr(obj), 16);

		int zeroFill = 0;
		for (int j = 0; j < 16; j++) {
			if (namesBuf[i][j] == '\0') zeroFill = 1;
			if (zeroFill) namesBuf[i][j] = '\0';
		}
	}

	//create tree
	NnsG3dTreeNode *tree = NnsiG3dConstructTree(namesBuf, nItems);
	NnsiG3dSerializeTree(stream, tree);
	NnsiG3dFreeTree(tree);
	free(namesBuf);
	free(namesBlob);
}

int NnsG3dWriteDictionary(BSTREAM *stream, void *resources, int itemSize, int nItems, NnsGetResourceNameCallback getNamePtr, int dictEntrySize) {
	int basePos = stream->pos;

	//write dummy dict header
	uint8_t dictHeader[8] = { 0 };
	bstreamWrite(stream, dictHeader, sizeof(dictHeader));

	//write P tree
	NnsiG3dConstructTreeFromResources(stream, resources, itemSize, nItems, getNamePtr);

	//write dict entries
	int dictEntryBase = stream->pos;
	uint8_t dictEntriesHeader[4] = { 0 };
	bstreamWrite(stream, dictEntriesHeader, sizeof(dictEntriesHeader));
	char *dummyDictEntry = (char *) calloc(dictEntrySize, 1);
	for (int i = 0; i < nItems; i++) {
		void *data = (void *) (i * itemSize + (uintptr_t) resources);
		bstreamWrite(stream, dummyDictEntry, dictEntrySize);
	}
	free(dummyDictEntry);

	int namesBase = stream->pos;
	bstreamSeek(stream, dictEntryBase, 0);
	*(uint16_t *) (dictEntriesHeader + 0) = dictEntrySize;
	*(uint16_t *) (dictEntriesHeader + 2) = namesBase - dictEntryBase;
	bstreamWrite(stream, dictEntriesHeader, sizeof(dictEntriesHeader));
	bstreamSeek(stream, namesBase, 0);

	//write names
	for (int i = 0; i < nItems; i++) {
		void *data = (void *) (i * itemSize + (uintptr_t) resources);
		bstreamWrite(stream, getNamePtr(data), 16);
	}

	int endPos = stream->pos;
	bstreamSeek(stream, basePos, 0);
	dictHeader[0] = 0;
	dictHeader[1] = nItems;
	*(uint16_t *) (dictHeader + 2) = endPos - basePos; //dict size
	*(uint16_t *) (dictHeader + 4) = 8; //padding?
	*(uint16_t *) (dictHeader + 6) = dictEntryBase - basePos;
	bstreamWrite(stream, dictHeader, sizeof(dictHeader));
	bstreamSeek(stream, endPos, 0);

	//return pointer to dictionary entries
	return dictEntryBase + 4;
}
