#include "nsbtx.h"
#include "texture.h"
#include "g2dfile.h"

#include <Windows.h>
#include <stdio.h>

//----- BEGIN Code for constructing an NSBTX dictionary

typedef struct PNODE_ {
	int leafIndex; //to control output index for a leaf node (set by user)
	char name[16]; //name (for leaf nodes, read by user)

	//internals
	int isLeaf;
	int refBit;    //for leaf nodes - equal to parent refBit
	int writtenTo; //for branch nodes - the index in the P-tree that this was written to
	               //for leaf nodes - the index in the P-tree that holds its leaf info
	struct PNODE_ *parent;

	//for branch nodes
	struct PNODE_ *left;
	struct PNODE_ *right;
} PNODE;

int nnsGetResourceBit(const char *name, unsigned int bit) {
	return (name[bit / 8] >> (bit % 8)) & 1;
}

int nnsFindBitDivergence(const char *const *names, int nNames) {
	if (nNames <= 1) return -1;

	//find a divergence. Return highest index of this.
	for (int i = 127; i >= 0; i--) {
		//check if any have a different value
		int diverged = 0;
		int b0 = nnsGetResourceBit(names[0], i);
		for (int j = 1; j < nNames; j++) {
			int bj = nnsGetResourceBit(names[j], i);
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
int nnsBitComparator(const void *p1, const void *p2) {
	const char *name1 = *(const char **) p1;
	const char *name2 = *(const char **) p2;
	int bit = g_bitCompareBit;

	//p1 - p2
	return nnsGetResourceBit(name1, bit) - nnsGetResourceBit(name2, bit);
}

PNODE *nnsConstructPTreeRecursive(const char **names, int nNames) {
	PNODE *root = (PNODE *) calloc(1, sizeof(PNODE));
	root->parent = NULL;
	if (nNames == 1) {
		root->isLeaf = 1;
		root->refBit = 0x80;
		memcpy(root->name, *names, 16);
		return root;
	}
	root->isLeaf = 0;

	//find first bit in the set of names that diverges. (duplicate resources prohibited!!)
	int divergedBit = nnsFindBitDivergence(names, nNames);
	root->refBit = divergedBit;
	if (divergedBit == -1) {
		free(root);
		return NULL;
	}

	//trash the name list? Of course, for sorting!
	g_bitCompareBit = divergedBit;
	qsort((void *) names, nNames, sizeof(char *), nnsBitComparator);

	//create children
	int nLeftNodes = nNames;
	for (int i = 0; i < nNames; i++) {
		int b = nnsGetResourceBit(names[i], divergedBit);
		if (b) {
			nLeftNodes = i;
			break;
		}
	}

	//create left+right
	root->left = nnsConstructPTreeRecursive(names, nLeftNodes);
	root->right = nnsConstructPTreeRecursive(names + nLeftNodes, nNames - nLeftNodes);
	root->left->parent = root;
	root->right->parent = root;
	if (root->left->isLeaf) root->left->refBit = root->refBit;
	if (root->right->isLeaf) root->right->refBit = root->refBit;
	return root;
}

PNODE *nnsLookupResource(PNODE *tree, const char *name) {
	//traverse tree
	while (!tree->isLeaf) {
		int refBit = tree->refBit;
		int bit = nnsGetResourceBit(name, refBit);

		tree = bit ? tree->right : tree->left;
	}
	if (memcmp(tree->name, name, 16) == 0) return tree;
	return NULL;
}

PNODE *nnsConstructPTree(const char *const *names, int nNames) {
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

	PNODE *constructed = nnsConstructPTreeRecursive(namesCopy, nNames);
	free(namesCopy);
	free(namesBlob);

	//set out indices
	for (int i = 0; i < nNames; i++) {
		PNODE *thisLeaf = nnsLookupResource(constructed, names[i]);
		thisLeaf->leafIndex = i;
	}
	return constructed;
}

void nnsFreePTree(PNODE *root) {
	if (root->left != NULL) {
		nnsFreePTree(root->left);
		free(root->left);
	}
	if (root->right != NULL) {
		nnsFreePTree(root->right);
		free(root->right);
	}
	memset(root, 0, sizeof(PNODE));
}

void nnsWritePNode(BSTREAM *stream, PNODE *node, int *baseIndex) {
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
	nnsWritePNode(stream, node->left, baseIndex);
	nnsWritePNode(stream, node->right, baseIndex);
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

PNODE *nnsFindUnassignedLeaf(PNODE *tree) {
	//find a node with writtenTo == -1. If multiple, find the one with higher refBit.
	if (tree->isLeaf) {
		return tree->writtenTo == -1 ? tree : NULL;
	}

	PNODE *foundLeft = nnsFindUnassignedLeaf(tree->left);
	PNODE *foundRight = nnsFindUnassignedLeaf(tree->right);
	if (foundLeft == NULL && foundRight == NULL) return NULL;
	if (foundLeft != NULL && foundRight == NULL) return foundLeft;
	if (foundLeft == NULL && foundRight != NULL) return foundRight;
	
	if (foundLeft->refBit > foundRight->refBit) return foundLeft;
	return foundRight;
}

PNODE *nnsFindUnassignedBranch(PNODE *tree) {
	//consider all nodes, not just children. If this node is a leaf, return NULL
	if (tree->isLeaf) return NULL;
	if (tree->left->isLeaf && tree->right->isLeaf) {
		return tree->leafIndex == -1 ? tree : NULL;
	}

	//search children
	PNODE *foundLeft = nnsFindUnassignedBranch(tree->left);
	PNODE *foundRight = nnsFindUnassignedBranch(tree->right);

	PNODE *bestNode = NULL;
	if (tree->leafIndex == -1) bestNode = tree;
	if (foundLeft != NULL) {
		if (bestNode == NULL || foundLeft->refBit > bestNode->refBit) bestNode = foundLeft;
	}
	if (foundRight != NULL) {
		if (bestNode == NULL || foundRight->refBit > bestNode->refBit) bestNode = foundRight;
	}

	return bestNode;
}

void nnsSerializePTree(BSTREAM *stream, PNODE *tree) {
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
	nnsWritePNode(stream, tree, &baseIndex);

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
		PNODE *unassigned = nnsFindUnassignedLeaf(tree);
		if (unassigned == NULL) break;

		//search for branch node not assigned to a leaf of highest refBit.
		PNODE *toAssign = nnsFindUnassignedBranch(tree);
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
		PNODE *leafParent = unassigned->parent;
		if (leafParent != NULL) {
			uint8_t childIndexBuffer = unassigned->writtenTo;
			int leftRight = leafParent->left == unassigned ? 0 : 1; //0 for left, 1 for right
			bstreamSeek(stream, baseOffset + leafParent->writtenTo * 4 + 1 + leftRight, 0);
			bstreamWrite(stream, &childIndexBuffer, sizeof(childIndexBuffer));
		}
	}
	bstreamSeek(stream, endPos, 0);
}

void nnsConstructPTreeFromResources(BSTREAM *stream, void *items, int itemSize, int nItems, char *(*getNamePtr) (void *obj)) {
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
	PNODE *tree = nnsConstructPTree(namesBuf, nItems);
	nnsSerializePTree(stream, tree);
	nnsFreePTree(tree);
	free(namesBuf);
	free(namesBlob);
}

int nnsWriteDictionary(BSTREAM *stream, void *items, int itemSize, int nItems, char *(*getNamePtr) (void *obj), int dictEntrySize) {
	int basePos = stream->pos;

	//write dummy dict header
	uint8_t dictHeader[8] = { 0 };
	bstreamWrite(stream, dictHeader, sizeof(dictHeader));

	//write P tree
	nnsConstructPTreeFromResources(stream, items, itemSize, nItems, getNamePtr);

	//write dict entries
	int dictEntryBase = stream->pos;
	uint8_t dictEntriesHeader[4] = { 0 };
	bstreamWrite(stream, dictEntriesHeader, sizeof(dictEntriesHeader));
	char *dummyDictEntry = (char *) calloc(dictEntrySize, 1);
	for (int i = 0; i < nItems; i++) {
		void *data = (void *) (i * itemSize + (uintptr_t) items);
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
		void *data = (void *) (i * itemSize + (uintptr_t) items);
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

//----- END Code for constructing an NSBTX dictionary

void freeDictionary(DICTIONARY *dictionary) {
	UNREFERENCED_PARAMETER(dictionary);
}

void nsbtxFree(OBJECT_HEADER *header) {
	NSBTX *nsbtx = (NSBTX *) header;
	if (nsbtx->textures != NULL) {
		for (int i = 0; i < nsbtx->nTextures; i++) {
			TEXELS *texture = nsbtx->textures + i;
			if (texture->texel != NULL) free(texture->texel);
			if (texture->cmp != NULL) free(texture->cmp);
		}
		free(nsbtx->textures);
		nsbtx->textures = NULL;
	}
	if (nsbtx->palettes != NULL) {
		for (int i = 0; i < nsbtx->nPalettes; i++) {
			PALETTE *palette = nsbtx->palettes + i;
			if (palette->pal != NULL) free(palette->pal);
		}
		free(nsbtx->palettes);
		nsbtx->palettes = NULL;
	}
	if (nsbtx->mdl0 != NULL) {
		free(nsbtx->mdl0);
		nsbtx->mdl0 = NULL;
		nsbtx->mdl0Size = 0;
	}
	if (nsbtx->bmdData != NULL) {
		BMD_DATA *bmd = nsbtx->bmdData;
		if (bmd->bones != NULL) free(bmd->bones);
		if (bmd->displaylists != NULL) free(bmd->displaylists);
		if (bmd->materials != NULL) free(bmd->materials);
		if (bmd->preTexture) free(bmd->preTexture);
		free(bmd);
		nsbtx->bmdData = NULL;
	}
	
}

void nsbtxInit(NSBTX *nsbtx, int format) {
	nsbtx->header.size = sizeof(NSBTX);
	fileInitCommon((OBJECT_HEADER *) nsbtx, FILE_TYPE_NSBTX, format);
	nsbtx->header.dispose = nsbtxFree;
}

//NSBTX code adapted from Gericom's code in Fvery File Explorer.

unsigned char *readDictionary(DICTIONARY *dict, unsigned char *base, int entrySize) {
	unsigned char *pos = base;
	int nEntries = *(uint8_t *) (pos + 1);
	int dictSize = *(uint16_t *) (pos + 4);
	int ofsEntry = *(uint16_t *) (pos + 6);
	dict->nEntries = nEntries;
	pos += ofsEntry; //skips the P tree

	dict->entry.sizeUnit = *(uint16_t *) (pos + 0);
	dict->entry.offsetName = *(uint16_t *) (pos + 2);
	pos += 4;

	dict->entry.data = pos;
	pos += entrySize * dict->nEntries;

	dict->namesPtr = pos;
	pos = base + dictSize; //end of dict
	return pos;
}

int nsbtxIsValidNsbtx(char *buffer, int size) {
	if (!g2dIsValid(buffer, size)) return 0;

	//check magic (only NSBTX or NSBMD)
	if ((buffer[0] != 'B' || buffer[1] != 'T' || buffer[2] != 'X' || buffer[3] != '0') &&
		(buffer[0] != 'B' || buffer[1] != 'M' || buffer[2] != 'D' || buffer[3] != '0')) return 0;

	return 1;
}

int nsbtxIsValidBmd(char *buffer, unsigned int size) {
	if (size < 0x3C || (size & 3)) return 0;

	int scale = *(int *) (buffer + 0);
	if (scale >= 32) return 0;
	uint32_t boneOffset = *(uint32_t *) (buffer + 0x08);
	uint32_t displaylistOffset = *(uint32_t *) (buffer + 0x10);
	uint32_t texturesOffset = *(uint32_t *) (buffer + 0x18);
	uint32_t palettesOffset = *(uint32_t *) (buffer + 0x20);
	uint32_t materialsOffset = *(uint32_t *) (buffer + 0x28);

	uint32_t nDisplaylists = *(uint32_t *) (buffer + 0x0C);
	uint32_t nTextures = *(uint32_t *) (buffer + 0x14);
	uint32_t nPalettes = *(uint32_t *) (buffer + 0x1C);
	uint32_t nMaterials = *(uint32_t *) (buffer + 0x24);

	//bounds+alignment
	if (boneOffset < 0x3C || displaylistOffset < 0x3C || texturesOffset < 0x3C
		|| palettesOffset < 0x3C || materialsOffset < 0x3C) return 0;
	if (boneOffset > size || displaylistOffset > size || texturesOffset > size
		|| palettesOffset > size || materialsOffset > size) return 0;
	if ((boneOffset & 3) || (displaylistOffset & 3) || (texturesOffset & 3)
		|| (palettesOffset & 3) || (materialsOffset & 3)) return 0;
	if (displaylistOffset + nDisplaylists * 8 > size) return 0;
	if (texturesOffset + nTextures * 0x14 > size) return 0;
	if (palettesOffset + nPalettes * 0x10 > size) return 0;
	if (materialsOffset + nMaterials * 0x30 > size) return 0;
	if (nDisplaylists == 0 && nMaterials == 0) return 0;

	unsigned char *textureSection = buffer + texturesOffset;
	for (unsigned int i = 0; i < nTextures; i++) {
		unsigned char *thisTex = textureSection + i * 0x14;

		uint32_t nameOffset = *(uint32_t *) (thisTex + 0x00);
		uint32_t textureOffset = *(uint32_t *) (thisTex + 0x04);
		uint32_t texelSize = *(uint32_t *) (thisTex + 0x08);
		uint32_t width = *(uint16_t *) (thisTex + 0x0C);
		uint32_t height = *(uint16_t *) (thisTex + 0x0E);
		uint32_t texImageParam = *(uint32_t *) (thisTex + 0x10);
		if (nameOffset < 0x3C || textureOffset < 0x3C || nameOffset >= size || textureOffset >= size)
			return 0;
		if (width != TEXW(texImageParam) || height != TEXH(texImageParam) || FORMAT(texImageParam) == 0)
			return 0;
		if (texelSize & (texelSize - 1))
			return 0;
	}

	return 1;
}

int nsbtxReadNsbtx(NSBTX *nsbtx, char *buffer, int size) {
	//is it valid?
	if (!nsbtxIsValidNsbtx(buffer, size)) return 1;

	nsbtxInit(nsbtx, NSBTX_TYPE_NNS);
	//iterate over each section
	int *sectionOffsets = (int *) (buffer + 0x10);
	int nSections = *(short *) (buffer + 0xE);
	//find the TEX0 section
	char *tex0 = NULL;
	int tex0Offset = 0;
	nsbtx->mdl0 = NULL;
	nsbtx->mdl0Size = 0;
	for (int i = 0; i < nSections; i++) {
		char *sect = buffer + sectionOffsets[i];
		if (sect[0] == 'T' && sect[1] == 'E' && sect[2] == 'X' && sect[3] == '0') {
			tex0 = sect;
			tex0Offset = sect - buffer;
			break;
		} else if (sect[0] == 'M' && sect[1] == 'D' && sect[2] == 'L' && sect[3] == '0') {
			nsbtx->mdl0Size = *(DWORD *) (sect + 4) - 8;
			nsbtx->mdl0 = malloc(nsbtx->mdl0Size);
			memcpy(nsbtx->mdl0, sect + 8, nsbtx->mdl0Size);
		}
	}
	if (tex0 == NULL) {
		if (nsbtx->mdl0 != NULL) {
			free(nsbtx->mdl0);
			nsbtx->mdl0 = NULL;
			nsbtx->mdl0Size = 0;
		}
		return 1;
	}

	//next, process the tex0 section.
	int blockSize = *(int *) (tex0 + 0x4);
	
	//texture header
	int textureDataSize = (*(uint16_t *) (tex0 + 0xC)) << 3;
	int textureInfoOffset = *(uint16_t *) (tex0 + 0xE); //dictionary
	int textureDataOffset = *(uint32_t *) (tex0 + 0x14); //ofsTex

	int compressedTextureDataSize = (*(uint16_t *) (tex0 + 0x1C)) << 3;
	int compressedTextureInfoOffset = *(uint16_t *) (tex0 + 0x1E); //dictionary
	int compressedTextureDataOffset = *(uint32_t *) (tex0 + 0x24); //ofsTex
	int compressedTextureInfoDataOffset = *(uint32_t *) (tex0 + 0x28); //ofsTexPlttIdx

	int paletteDataSize = (*(uint16_t *) (tex0 + 0x30)) << 3;
	int paletteInfoOffset = *(uint32_t *) (tex0 + 0x34); //dictionary
	int paletteDataOffset = *(uint32_t *) (tex0 + 0x38);

	char *texInfo = tex0 + textureInfoOffset;
	char *palInfo = tex0 + paletteInfoOffset;

	DICTIONARY dictTex;
	readDictionary(&dictTex, tex0 + textureInfoOffset, sizeof(DICTTEXDATA));
	DICTTEXDATA *dictTexData = (DICTTEXDATA *) dictTex.entry.data;
	TEXELS *texels = (TEXELS *) calloc(dictTex.nEntries, sizeof(TEXELS));
	
	DICTIONARY dictPal;
	char *pos = readDictionary(&dictPal, tex0 + paletteInfoOffset, sizeof(DICTPLTTDATA));
	DICTPLTTDATA *dictPalData = (DICTPLTTDATA *) dictPal.entry.data;
	PALETTE *palettes = (PALETTE *) calloc(dictPal.nEntries, sizeof(PALETTE));

	int baseOffsetTex = textureDataOffset;
	int baseOffsetTex4x4 = compressedTextureDataOffset;
	int baseOffsetTex4x4Info = compressedTextureInfoDataOffset;
	int texPlttSetOffset = tex0Offset;
	for (int i = 0; i < dictTex.nEntries; i++) {
		//read a texture from pos.
		DICTTEXDATA *texData = dictTexData + i;
		int offset = OFFSET(texData->texImageParam);

		int width = TEXW(texData->texImageParam);
		int height = TEXH(texData->texImageParam);
		int texelSize = TxGetTexelSize(width, height, texData->texImageParam);

		if (FORMAT(texData->texImageParam) == CT_4x4) {
			texels[i].texImageParam = texData->texImageParam;
			texels[i].texel = calloc(texelSize, 1);
			texels[i].cmp = calloc(texelSize >> 1, 1);
			memcpy(texels[i].texel, tex0 + offset + baseOffsetTex4x4, texelSize);
			memcpy(texels[i].cmp, tex0 + baseOffsetTex4x4Info + offset / 2, texelSize >> 1);
			
		} else {
			texels[i].texImageParam = texData->texImageParam;
			texels[i].cmp = NULL;
			texels[i].texel = calloc(texelSize, 1);
			memcpy(texels[i].texel, tex0 + offset + baseOffsetTex, texelSize);
		}
		memcpy(texels[i].name, dictTex.namesPtr + i * 16, 16);
	}

	for (int i = 0; i < dictPal.nEntries; i++) {
		DICTPLTTDATA *palData = dictPalData + i;
		
		//find the length of the palette, by finding the least offset greater than this one's palette
		int offset = size - paletteDataOffset - (palData->offset << 3) - tex0Offset + (palData->offset << 3);
		for (int j = 0; j < dictPal.nEntries; j++) {
			int offset2 = dictPalData[j].offset << 3;
			if (offset2 <= (palData->offset << 3)) continue;
			if (offset2 < offset || offset == 0) {
				offset = offset2;
			}
		}

		int nColors = (offset - (palData->offset << 3)) >> 1;
		if (palData->flag & 0x0001) nColors = 4; //4-color flag
		palettes[i].nColors = nColors;
		palettes[i].pal = (COLOR *) calloc(nColors, 2);
		memcpy(palettes[i].pal, tex0 + paletteDataOffset + (palData->offset << 3), nColors * 2);

		memcpy(palettes[i].name, dictPal.namesPtr + i * 16, 16);
	}

	//finally write out tex and pal info
	nsbtx->nTextures = dictTex.nEntries;
	nsbtx->nPalettes = dictPal.nEntries;
	nsbtx->textures = texels;
	nsbtx->palettes = palettes;
	return 0;
}

int nsbtxReadBmd(NSBTX *nsbtx, unsigned char *buffer, int size) {
	if (!nsbtxIsValidBmd(buffer, size)) return 1;

	nsbtxInit(nsbtx, NSBTX_TYPE_BMD);
	nsbtx->bmdData = (BMD_DATA *) calloc(1, sizeof(BMD_DATA));
	BMD_DATA *bmd = nsbtx->bmdData;

	bmd->scale = *(uint32_t *) (buffer + 0x00);
	bmd->nBones = *(uint32_t *) (buffer + 0x04);
	bmd->boneOffset = *(uint32_t *) (buffer + 0x08);
	bmd->nDisplaylists = *(uint32_t *) (buffer + 0x0C);
	bmd->displaylistOffset = *(uint32_t *) (buffer + 0x10);
	bmd->nMaterials = *(uint32_t *) (buffer + 0x24);
	bmd->transformOffset = *(uint32_t *) (buffer + 0x2C);
	bmd->field30 = *(uint32_t *) (buffer + 0x30);
	bmd->field34 = *(uint32_t *) (buffer + 0x34);

	nsbtx->nTextures = *(uint32_t *) (buffer + 0x14);
	nsbtx->nPalettes = *(uint32_t *) (buffer + 0x1C);
	nsbtx->textures = (TEXELS *) calloc(nsbtx->nTextures, sizeof(TEXELS));
	nsbtx->palettes = (PALETTE *) calloc(nsbtx->nPalettes, sizeof(PALETTE));

	//read textures and palettes
	unsigned char *texDescriptors = buffer + *(uint32_t *) (buffer + 0x18);
	unsigned char *palDescriptors = buffer + *(uint32_t *) (buffer + 0x20);
	for (int i = 0; i < nsbtx->nTextures; i++) {
		unsigned char *thisTex = texDescriptors + i * 0x14;
		TEXELS *texture = nsbtx->textures + i;

		uint32_t nameOffset = *(uint32_t *) (thisTex + 0x00);
		uint32_t texelOffset = *(uint32_t *) (thisTex + 0x04);
		uint32_t texelSize = *(uint32_t *) (thisTex + 0x08);
		uint32_t texImageParam = *(uint32_t *) (thisTex + 0x10);
		char *name = buffer + nameOffset;
		int format = FORMAT(texImageParam);

		texture->texImageParam = texImageParam;
		memcpy(texture->name, name, min(strlen(name), 16));
		texture->texel = (char *) calloc(texelSize, 1);
		memcpy(texture->texel, buffer + texelOffset, texelSize);
		if (format == CT_4x4) {
			texture->cmp = (short *) calloc(texelSize / 2, 1);
			memcpy(texture->cmp, buffer + texelOffset + texelSize, texelSize / 2);
		}
	}
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		unsigned char *thisPal = palDescriptors + i * 0x10;
		PALETTE *palette = nsbtx->palettes + i;

		uint32_t nameOffset = *(uint32_t *) (thisPal + 0x00);
		uint32_t paletteOffset = *(uint32_t *) (thisPal + 0x04);
		uint32_t paletteSize = *(uint32_t *) (thisPal + 0x08);
		char *name = buffer + nameOffset;

		palette->nColors = paletteSize / 2;
		palette->pal = (COLOR *) calloc(palette->nColors, sizeof(COLOR));
		memcpy(palette->pal, buffer + paletteOffset, paletteSize);
		memcpy(palette->name, name, min(strlen(name), 16));
	}

	//all the other stuff
	uint32_t materialsOffset = *(uint32_t *) (buffer + 0x28);
	uint32_t materialsEnd = materialsOffset + bmd->nMaterials * 0x30;
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = buffer + materialsOffset + i * 0x30;
		uint32_t nameOffset = *(uint32_t *) material;
		uint32_t nameEnd = nameOffset + 1 + strlen(buffer + nameOffset);
		if (nameEnd > materialsEnd) {
			materialsEnd = nameEnd;
		}
	}
	materialsEnd = (materialsEnd + 3) & ~3;
	bmd->materials = calloc(materialsEnd - materialsOffset, 1);
	bmd->materialsSize = materialsEnd - materialsOffset;
	memcpy(bmd->materials, buffer + materialsOffset, materialsEnd - materialsOffset);

	//make name offsets in the material section relative
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material -= materialsOffset;
	}

	uint32_t preTextureSize = *(uint32_t *) (buffer + 0x18) - 0x3C;
	bmd->preTextureSize = preTextureSize;
	bmd->preTexture = calloc(bmd->preTextureSize, 1);
	memcpy(bmd->preTexture, buffer + 0x3C, preTextureSize);

	return 0;
}

int nsbtxRead(NSBTX *nsbtx, char *buffer, int size) {
	if (nsbtxIsValidNsbtx(buffer, size)) return nsbtxReadNsbtx(nsbtx, buffer, size);
	if (nsbtxIsValidBmd(buffer, size)) return nsbtxReadBmd(nsbtx, buffer, size);
	return 1;
}

int nsbtxReadFile(NSBTX *nsbtx, LPCWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nsbtx, (OBJECT_READER) nsbtxRead);
}

typedef struct {
	BYTE *ptr;
	int bufferSize;
	int length;
} BYTEARRAY;

void initializeArray(BYTEARRAY *arr) {
	arr->ptr = calloc(1024, 1);
	arr->length = 0;
	arr->bufferSize = 1024;
}

void freeArray(BYTEARRAY *arr) {
	if (arr->ptr != NULL) free(arr->ptr);
	arr->ptr = NULL;
	arr->length = 0;
	arr->bufferSize = 0;
}

static char *getTextureName(void *texels) {
	return ((TEXELS *) texels)->name;
}

static char *getPaletteName(void *palette) {
	return ((PALETTE *) palette)->name;
}

void addBytes(BYTEARRAY *arr, BYTE *bytes, int length) {
	if (arr->length + length >= arr->bufferSize) {
		int newSize = arr->bufferSize;
		while (arr->length + length >= newSize) {
			newSize = newSize + (newSize >> 1);
		}
		arr->ptr = realloc(arr->ptr, newSize);
		arr->bufferSize = newSize;
	}
	memcpy(arr->ptr + arr->length, bytes, length);
	arr->length += length;
}

int nsbtxWriteNsbtx(NSBTX *nsbtx, BSTREAM *stream) {
	if (nsbtx->mdl0 != NULL) {
		BYTE fileHeader[] = { 'B', 'M', 'D', '0', 0xFF, 0xFE, 1, 0, 0, 0, 0, 0, 0x10, 0, 2, 0, 0x18, 0, 0, 0, 0, 0, 0, 0 };

		bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	} else {
		BYTE fileHeader[] = { 'B', 'T', 'X', '0', 0xFF, 0xFE, 1, 0, 0, 0, 0, 0, 0x10, 0, 1, 0, 0x14, 0, 0, 0 };

		bstreamWrite(stream, fileHeader, sizeof(fileHeader));
	}

	if (nsbtx->mdl0 != NULL) {
		BYTE mdl0Header[] = { 'M', 'D', 'L', '0', 0, 0, 0, 0 };
		*(DWORD *) (mdl0Header + 4) = nsbtx->mdl0Size + 8;
		bstreamWrite(stream, mdl0Header, sizeof(mdl0Header));
		bstreamWrite(stream, nsbtx->mdl0, nsbtx->mdl0Size);
	}
	
	DWORD tex0Offset = stream->pos;

	BYTE tex0Header[] = { 'T', 'E', 'X', '0', 0, 0, 0, 0 };
	bstreamWrite(stream, tex0Header, sizeof(tex0Header));

	BYTEARRAY texData, tex4x4Data, tex4x4PlttIdxData, paletteData;
	initializeArray(&texData);
	initializeArray(&tex4x4Data);
	initializeArray(&tex4x4PlttIdxData);
	initializeArray(&paletteData);
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		int width = TEXW(texture->texImageParam);
		int height = TEXH(texture->texImageParam);
		int texelSize = TxGetTexelSize(width, height, texture->texImageParam);
		if (FORMAT(texture->texImageParam) == CT_4x4) {
			//write the offset in the texImageParams
			int ofs = (tex4x4Data.length >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			addBytes(&tex4x4Data, texture->texel, texelSize);
			addBytes(&tex4x4PlttIdxData, (BYTE *) texture->cmp, texelSize / 2);
		} else {
			int ofs = (texData.length >> 3) & 0xFFFF;
			texture->texImageParam = (texture->texImageParam & 0xFFFF0000) | ofs;
			addBytes(&texData, texture->texel, texelSize);
		}
	}

	int *paletteOffsets = (int *) calloc(nsbtx->nTextures, sizeof(int));
	int has4Color = 0;
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		int offs = paletteData.length;
		PALETTE *palette = nsbtx->palettes + i;
		paletteOffsets[i] = paletteData.length;

		//add bytes, make sure to align to a multiple of 16 bytes if more than 4 colors! (or if it's the last palette)
		int nColors = palette->nColors;
		addBytes(&paletteData, (BYTE *) palette->pal, nColors * 2);
		if (nColors <= 4 && ((i == nsbtx->nPalettes - 1) || (nsbtx->palettes[i + 1].nColors > 4))) {
			BYTE padding[16] = { 0 };
			addBytes(&paletteData, padding, 16 - nColors * 2);
		}

		//do we have 4 color?
		if (nColors <= 4) has4Color = 1;
	}

	uint8_t texInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (texInfo + 6) = 60;
	*(uint16_t *) (texInfo + 4) = texData.length >> 3;
	*(uint32_t *) (texInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24;
	bstreamWrite(stream, texInfo, sizeof(texInfo));

	uint8_t tex4x4Info[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (tex4x4Info + 6) = 60;
	*(uint16_t *) (tex4x4Info + 4) = tex4x4Data.length >> 3;
	*(uint32_t *) (tex4x4Info + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.length;
	*(uint32_t *) (tex4x4Info + 16) = (*(uint32_t *) (tex4x4Info + 12)) + tex4x4Data.length;
	bstreamWrite(stream, tex4x4Info, sizeof(tex4x4Info));

	uint8_t plttInfo[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (plttInfo + 8) = 76 + nsbtx->nTextures * 28;
	*(uint16_t *) (plttInfo + 4) = paletteData.length >> 3;
	*(uint16_t *) (plttInfo + 6) = has4Color ? 0x8000 : 0;
	*(uint32_t *) (plttInfo + 12) = 92 + nsbtx->nTextures * 28 + nsbtx->nPalettes * 24 + texData.length + tex4x4Data.length + tex4x4PlttIdxData.length;
	bstreamWrite(stream, plttInfo, sizeof(plttInfo));

	{
		//write dictTex
		int dictOfs = nnsWriteDictionary(stream, nsbtx->textures, sizeof(TEXELS), nsbtx->nTextures, getTextureName, 8);
		int dictEndOfs = stream->pos;

		//write dict data
		//make sure to copy the texImageParams over
		bstreamSeek(stream, dictOfs, 0);
		for (int i = 0; i < nsbtx->nTextures; i++) {
			uint32_t dictData[2];
			TEXELS *texels = nsbtx->textures + i;
			int texImageParam = texels->texImageParam;
			dictData[0] = texImageParam;
			dictData[1] = 0x80000000 | TEXW(texImageParam) | (TEXH(texImageParam) << 11);

			bstreamWrite(stream, dictData, sizeof(dictData));
		}
		bstreamSeek(stream, dictEndOfs, 0);
	}
	{
		//write dictPltt
		int dictOfs = nnsWriteDictionary(stream, nsbtx->palettes, sizeof(PALETTE), nsbtx->nPalettes, getPaletteName, 4);
		int dictEndOfs = stream->pos;
		
		//write data
		bstreamSeek(stream, dictOfs, 0);
		for (int i = 0; i < nsbtx->nTextures; i++) {
			PALETTE *palette = nsbtx->palettes + i;
			uint16_t dictData[2];
			dictData[0] = paletteOffsets[i] >> 3;
			dictData[1] = palette->nColors <= 4;
			bstreamWrite(stream, dictData, sizeof(dictData));
		}
		bstreamSeek(stream, dictEndOfs, 0);
	}
	free(paletteOffsets);

	//write texData, tex4x4Data, tex4x4PlttIdxData, paletteData
	bstreamWrite(stream, texData.ptr, texData.length);
	bstreamWrite(stream, tex4x4Data.ptr, tex4x4Data.length);
	bstreamWrite(stream, tex4x4PlttIdxData.ptr, tex4x4PlttIdxData.length);
	bstreamWrite(stream, paletteData.ptr, paletteData.length);

	//write back the proper sizes
	DWORD endPos = stream->pos;
	stream->pos = 8;
	bstreamWrite(stream, &endPos, 4);
	if (nsbtx->mdl0 == NULL) {
		int tex0Size = endPos - 0x14;

		stream->pos = tex0Offset + 4;
		bstreamWrite(stream, &tex0Size, 4);
	} else {
		int mdl0Size = 8 + nsbtx->mdl0Size;
		int tex0Size = endPos - 0x18 - mdl0Size;

		stream->pos = tex0Offset + 4;
		bstreamWrite(stream, &tex0Size, 4);
		stream->pos = 0x14;
		bstreamWrite(stream, &tex0Offset, 4);
	}

	//free resources
	freeArray(&texData);
	freeArray(&tex4x4Data);
	freeArray(&tex4x4PlttIdxData);
	freeArray(&paletteData);

	return 0;
}

int nsbtxWriteBmd(NSBTX *nsbtx, BSTREAM *stream) {
	unsigned char header[0x3C];

	BMD_DATA *bmd = nsbtx->bmdData;
	bstreamWrite(stream, header, sizeof(header));
	bstreamWrite(stream, bmd->preTexture, bmd->preTextureSize);

	//write textures
	int texturePos = stream->pos;
	for (int i = 0; i < nsbtx->nTextures; i++) {
		unsigned char texEntry[0x14] = { 0 };
		TEXELS *texture = nsbtx->textures + i;

		int texImageParam = texture->texImageParam;
		*(uint32_t *) (texEntry + 0x08) = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
		*(uint16_t *) (texEntry + 0x0C) = TEXW(texImageParam);
		*(uint16_t *) (texEntry + 0x0E) = TEXH(texImageParam);
		*(uint32_t *) (texEntry + 0x10) = texImageParam;

		bstreamWrite(stream, texEntry, sizeof(texEntry));
	}

	//write texture names
	char terminator = '\0';
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		char *name = texture->name;
		int len = 0;
		for (; len < 16; len++) {
			if (name[len] == '\0') break;
		}

		uint32_t pos = stream->pos;
		bstreamSeek(stream, texturePos + i * 0x14, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, name, len);
		bstreamWrite(stream, &terminator, 1);
	}
	while (stream->pos & 3) { //pad
		bstreamWrite(stream, &terminator, 1);
	}

	//write palettes
	int palettePos = stream->pos;
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		unsigned char palEntry[0x10] = { 0 };
		PALETTE *palette = nsbtx->palettes + i;

		*(uint32_t *) (palEntry + 0x08) = palette->nColors * 2;
		*(uint32_t *) (palEntry + 0x0C) = 0xFFFFFFFF;

		bstreamWrite(stream, palEntry, sizeof(palEntry));
	}

	//write palette names
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		PALETTE *palette = nsbtx->palettes + i;
		char *name = palette->name;
		int len = 0;
		for (; len < 16; len++) {
			if (name[len] == '\0') break;
		}

		uint32_t pos = stream->pos;
		bstreamSeek(stream, palettePos + i * 0x10, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, name, len);
		bstreamWrite(stream, &terminator, 1);
	}
	while (stream->pos & 3) { //pad
		bstreamWrite(stream, &terminator, 1);
	}

	int materialPos = stream->pos;
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material += materialPos;
	}
	bstreamWrite(stream, bmd->materials, bmd->materialsSize);
	for (int i = 0; i < bmd->nMaterials; i++) {
		unsigned char *material = ((unsigned char *) bmd->materials) + i * 0x30;
		*(uint32_t *) material -= materialPos;
	}

	//write texture data
	int textureDataPos = stream->pos;
	for (int i = 0; i < nsbtx->nTextures; i++) {
		TEXELS *texture = nsbtx->textures + i;
		int texImageParam = texture->texImageParam;
		int texelSize = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
		uint32_t pos = stream->pos;

		bstreamSeek(stream, texturePos + i * 0x14 + 4, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, texture->texel, texelSize);
		if (FORMAT(texImageParam) == CT_4x4) {
			bstreamWrite(stream, texture->cmp, texelSize / 2);
		}
	}

	//write palette data
	for (int i = 0; i < nsbtx->nPalettes; i++) {
		PALETTE *palette = nsbtx->palettes + i;
		uint32_t pos = stream->pos;

		bstreamSeek(stream, palettePos + i * 0x10 + 4, 0);
		bstreamWrite(stream, &pos, sizeof(pos));
		bstreamSeek(stream, pos, 0);
		bstreamWrite(stream, palette->pal, palette->nColors * 2);
	}
	bstreamSeek(stream, 0, 0);

	*(uint32_t *) (header + 0x00) = bmd->scale;
	*(uint32_t *) (header + 0x04) = bmd->nBones;
	*(uint32_t *) (header + 0x08) = bmd->boneOffset;
	*(uint32_t *) (header + 0x0C) = bmd->nDisplaylists;
	*(uint32_t *) (header + 0x10) = bmd->displaylistOffset;
	*(uint32_t *) (header + 0x14) = nsbtx->nTextures;
	*(uint32_t *) (header + 0x18) = texturePos;
	*(uint32_t *) (header + 0x1C) = nsbtx->nPalettes;
	*(uint32_t *) (header + 0x20) = palettePos;
	*(uint32_t *) (header + 0x24) = bmd->nMaterials;
	*(uint32_t *) (header + 0x28) = materialPos;
	*(uint32_t *) (header + 0x2C) = bmd->transformOffset;
	*(uint32_t *) (header + 0x30) = bmd->field30;
	*(uint32_t *) (header + 0x34) = bmd->field34;
	*(uint32_t *) (header + 0x38) = textureDataPos;
	bstreamWrite(stream, header, sizeof(header));

	return 0;
}

int nsbtxWrite(NSBTX *nsbtx, BSTREAM *stream) {
	int fmt = nsbtx->header.format;
	switch (fmt) {
		case NSBTX_TYPE_NNS:
			return nsbtxWriteNsbtx(nsbtx, stream);
		case NSBTX_TYPE_BMD:
			return nsbtxWriteBmd(nsbtx, stream);
	}
	return 1;
}

int nsbtxWriteFile(NSBTX *nsbtx, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nsbtx, (OBJECT_WRITER) nsbtxWrite);
}
