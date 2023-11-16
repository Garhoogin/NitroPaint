#include <stdio.h>

#include "cellgen.h"

#define CELLGEN_DIR_H     0
#define CELLGEN_DIR_V     1
#define CELLGEN_DIR_NONE -1

#define CELLGEN_MAX_DIV_H   3
#define CELLGEN_MAX_DIV_V   3
#define CELLGEN_MAX_DIV     (CELLGEN_MAX_DIV_H+CELLGEN_MAX_DIV_V)
#define CELLGEN_MAX_PACK    64

#define CELLGEN_ROUNDUP8(x)  (((x)+7)&~7)

static void CellgenGetXYBounds(COLOR32 *px, const unsigned char *ignores, int width, int height, int xMin, int xMax, int yMin, int yMax,
		int *pxMin, int *pxMax, int *pyMin, int *pyMax) {

	//clamp bounds
	yMin = min(max(yMin, 0), height);
	yMax = min(max(yMax, 0), height);
	xMin = min(max(xMin, 0), width);
	xMax = min(max(xMax, 0), width);

	//init -1 to indicate none found (yet)
	//must be same in case none ever found
	int xOutMin = -1, xOutMax = -1;
	int yOutMin = -1, yOutMax = -1;

	//scan rectangle
	for (int y = yMin; y < yMax; y++) {
		for (int x = xMin; x < xMax; x++) {
			COLOR32 c = px[y * width + x];
			int a = ((c >> 24) & 0xFF) >= 128;

			int ignore = 0;
			if (ignores != NULL) {
				ignore = ignores[y * width + x];
			}

			//ignore marks pixel as effectively transparent
			if (ignore) a = 0;

			if (a) {
				if (xOutMin == -1 || x < xOutMin) xOutMin = x;
				if (yOutMin == -1 || y < yOutMin) yOutMin = y;
				if (x + 1 > xOutMax) xOutMax = x + 1;
				if (y + 1 > yOutMax) yOutMax = y + 1;
			}
		}
	}

	*pxMin = xOutMin;
	*pxMax = xOutMax;
	*pyMin = yOutMin;
	*pyMax = yOutMax;
}

static int CellgenObjContainsPixels(COLOR32 *px, const unsigned char *ignores, int width, int height, OBJ_BOUNDS *bounds) {
	int xMin, xMax, yMin, yMax;
	CellgenGetXYBounds(px, ignores, width, height, bounds->x, bounds->x + bounds->width, bounds->y, bounds->y + bounds->height,
		&xMin, &xMax, &yMin, &yMax);

	//if xMin == xMax or yMin == yMax, this bound is empty.
	return xMin != xMax && yMin != yMax;
}

static int CellgenCountTransparent(COLOR32 *px, const unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *bounds) {
	int xMin = bounds->x, xMax = bounds->x + bounds->width;
	int yMin = bounds->y, yMax = bounds->y + bounds->height;
	yMin = min(max(yMin, 0), height);
	yMax = min(max(yMax, 0), height);
	xMin = min(max(xMin, 0), width);
	xMax = min(max(xMax, 0), width);

	int nTransparent = 0;
	for (int y = yMin; y < yMax; y++) {
		for (int x = xMin; x < xMax; x++) {
			COLOR32 c = px[y * width + x];
			int a = (c >> 24) >= 128;

			int skip = 0;
			if (accountBuf != NULL) {
				skip = accountBuf[y * width + x];
			}
			if (skip) a = 0;

			if (!a) nTransparent++;
		}
	}
	return nTransparent;
}

static int CellgenGetLargestObjDimension(int x) {
	if (x >= 64) return 64;
	if (x >= 32) return 32;
	if (x >= 16) return 16;
	return 8;
}

static int CellgenAdjustCorrectObjWidth(int height, int width) {
	if (height == 64 && width == 8) return 32;
	if (height == 64 && width == 16) return 32;
	if (height == 8 && width == 64) return 32;
	if (height == 16 && width == 64) return 32;
	return width;
}

static int CellgenAdjustCorrectObjHeight(int width, int height) {
	//same as the converse
	return CellgenAdjustCorrectObjWidth(width, height);
}

static void CellgenAccountRegion(unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *bounds) {
	int objXMin = bounds->x, objXMax = bounds->x + bounds->width;
	int objYMin = bounds->y, objYMax = bounds->y + bounds->height;

	//clamp to image size
	objXMin = min(max(objXMin, 0), width);
	objXMax = min(max(objXMax, 0), width);
	objYMin = min(max(objYMin, 0), height);
	objYMax = min(max(objYMax, 0), height);

	//account for all of this OBJ's pixels
	for (int y = objYMin; y < objYMax; y++) {
		for (int x = objXMin; x < objXMax; x++) {
			accountBuf[y * width + x] = 1;
		}
	}
}

static void CellgenSplitObj(OBJ_BOUNDS *obj, int dir, OBJ_BOUNDS *out1, OBJ_BOUNDS *out2) {
	if (dir == CELLGEN_DIR_NONE) return;
	memcpy(out1, obj, sizeof(OBJ_BOUNDS));
	memcpy(out2, obj, sizeof(OBJ_BOUNDS));

	if (dir == CELLGEN_DIR_H) {
		out1->width /= 2;
		out2->width /= 2;
		out2->x += out1->width;
	} else if (dir == CELLGEN_DIR_V) {
		out1->height /= 2;
		out2->height /= 2;
		out2->y += out1->height;
	}
}

static int CellgenDecideSplitDirection(COLOR32 *px, const unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *bounds) {
	//can we split both ways?
	int canHSplit = 0, canVSplit = 0;

	if (bounds->width != 8) {
		if (bounds->width != 64 && bounds->height != 64) canHSplit = 1; //all dimensions without a 64
		if (bounds->width == 64 && bounds->height == 64) canHSplit = 1; //64x64
		if (bounds->width == 64 && bounds->height == 32) canHSplit = 1; //64x32
		if (bounds->width <= 32 && bounds->height <= 32) canHSplit = 1; //any 32 and under
	}
	if (bounds->height != 8) {
		if (bounds->width != 64 && bounds->height != 64) canVSplit = 1; //all dimensions without 64
		if (bounds->width == 64 && bounds->height == 64) canVSplit = 1; //64x64
		if (bounds->width == 32 && bounds->height == 64) canVSplit = 1; //32x64
		if (bounds->width <= 32 && bounds->height <= 32) canVSplit = 1; //any 32 and under
	}

	//if only one direction is splittable, return that
	if (!canHSplit && !canVSplit) return CELLGEN_DIR_NONE;
	if (canHSplit && !canVSplit) return CELLGEN_DIR_H;
	if (!canHSplit && canVSplit) return CELLGEN_DIR_V;

	//else, decide split direction.
	OBJ_BOUNDS tempH[2], tempV[2];
	CellgenSplitObj(bounds, CELLGEN_DIR_H, tempH + 0, tempH + 1);
	CellgenSplitObj(bounds, CELLGEN_DIR_V, tempV + 0, tempV + 1);

	int diffH = CellgenCountTransparent(px, accountBuf, width, height, tempH + 0)
		- CellgenCountTransparent(px, accountBuf, width, height, tempH + 1);
	int diffV = CellgenCountTransparent(px, accountBuf, width, height, tempV + 0)
		- CellgenCountTransparent(px, accountBuf, width, height, tempV + 1);
	if (diffH < 0) diffH = -diffH;
	if (diffV < 0) diffV = -diffV;

	//the larger difference is how we want to split
	if (diffH > diffV) return CELLGEN_DIR_H;
	if (diffV > diffH) return CELLGEN_DIR_V;

	if (bounds->width > bounds->height) return CELLGEN_DIR_H;
	return CELLGEN_DIR_V;
}

static int CellgenProcessSubdivision(COLOR32 *px, unsigned char *accountBuf, int width, int height, int aggressiveness, 
		OBJ_BOUNDS *boundBuffer, int *pCount, int index, int maxDepth) {
	//should we give up here based on the aggressiveness parameter?
	int area = boundBuffer[index].width * boundBuffer[index].height;
	int nTrans = CellgenCountTransparent(px, accountBuf, width, height, boundBuffer + index);

	//if nTrans / size >= proportionRequired
	if (nTrans * 100 / area < (100 - aggressiveness)) return 0; //did not split

	//can split?
	int splitDir = CellgenDecideSplitDirection(px, accountBuf, width, height, boundBuffer + index);
	if (splitDir == CELLGEN_DIR_NONE) return 0; //did not split

	//can split. 
	int split1Index = index;   //same
	int split2Index = *pCount; //end
	OBJ_BOUNDS original;
	memcpy(&original, boundBuffer + index, sizeof(OBJ_BOUNDS));

	//do split
	CellgenSplitObj(&original, splitDir, boundBuffer + split1Index, boundBuffer + split2Index);
	(*pCount)++; //buffer grew by 1

	//are either of the two OBJ fully transparent?
	int split1HasPixels = CellgenObjContainsPixels(px, accountBuf, width, height, boundBuffer + split1Index);
	int split2HasPixels = CellgenObjContainsPixels(px, accountBuf, width, height, boundBuffer + split2Index);

	int didCull = 0;
	if (!split1HasPixels) {
		//split 1 can be culled
		boundBuffer[split1Index].width = 0; //removing would be dangerous
		boundBuffer[split1Index].height = 0; //so just set size to (0,0)
		didCull = 1;
	}
	if (!split2HasPixels) {
		//split 2 can be culled
		boundBuffer[split2Index].width = 0;
		boundBuffer[split2Index].height = 0;
		didCull = 1;
	}

	//try split children
	int split1DidCull = 0, split2DidCull = 0;
	if (split1HasPixels && maxDepth > 1) {
		split1DidCull = CellgenProcessSubdivision(px, accountBuf, width, height, aggressiveness, boundBuffer, pCount, split1Index, maxDepth - 1);
	}
	if (split2HasPixels && maxDepth > 1) {
		split2DidCull = CellgenProcessSubdivision(px, accountBuf, width, height, aggressiveness, boundBuffer, pCount, split2Index, maxDepth - 1);
	}

	//if either child did cull, we cannot re-merge.
	if (split1DidCull || split2DidCull) didCull = 1;

	if (!didCull) {
		//merge back (no culling happened)
		memcpy(boundBuffer + split1Index, &original, sizeof(OBJ_BOUNDS));
		boundBuffer[split2Index].width = 0;  //split 2 effectively deleted
		boundBuffer[split2Index].height = 0;
	}

	//return based on whether we culled
	return didCull;
}

#define SHIFT_FLAG_BOTTOM       1
#define SHIFT_FLAG_RIGHT        2
#define SHIFT_FLAG_NOREMOVE     4

static int CellgenShiftObj(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj, int nObj, int flag) {
	//edges to fit pixels to
	int matchLeft = !(flag & SHIFT_FLAG_RIGHT);
	int matchTop = !(flag & SHIFT_FLAG_BOTTOM);

	for (int i = 0; i < nObj; i++) {
		//get OBJ current bounds
		OBJ_BOUNDS *bounds = obj + i;
		int objXMin = bounds->x, objXMax = bounds->x + bounds->width;
		int objYMin = bounds->y, objYMax = bounds->y + bounds->height;

		//get pixel bounds of this OBJ
		int bxMin, bxMax, byMin, byMax;
		CellgenGetXYBounds(px, accountBuf, width, height, objXMin, objXMax, objYMin, objYMax, &bxMin, &bxMax, &byMin, &byMax);

		//if xMin == xMax or yMin == yMax, this OBJ has become useless in this step, so remove it.
		if ((bxMin == bxMax || byMin == byMax) && !(flag & SHIFT_FLAG_NOREMOVE)) {
			memmove(obj + i, obj + i + 1, (nObj - i - 1) * sizeof(OBJ_BOUNDS));
			nObj--;
			i--;
			continue;
		}

		//if xMin > objXMin, slide right the difference
		//if yMin > objYMin, slide down the difference
		if (matchLeft) {
			if (bxMin > objXMin) {
				bounds->x += (bxMin - objXMin);
			}
		} else {
			if (bxMax < objXMax) {
				bounds->x -= (objXMax - bxMax);
			}
		}
		if (matchTop) {
			if (byMin > objYMin) {
				bounds->y += (byMin - objYMin);
			}
		} else {
			if (byMax < objYMax) {
				bounds->y -= (objYMax - byMax);
			}
		}

		//recompute bounds
		objXMin = bounds->x, objXMax = bounds->x + bounds->width;
		objYMin = bounds->y, objYMax = bounds->y + bounds->height;

		//account for all of this OBJ's pixels
		CellgenAccountRegion(accountBuf, width, height, bounds);
	}

	return nObj;
}

static int CellgenIterateAllShifts(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj, int nObj) {
	//all combinations of flags
	for (int i = 0; i < 4; i++) {
		memset(accountBuf, 0, width * height);
		nObj = CellgenShiftObj(px, accountBuf, width, height, obj, nObj, i);
	}
	return nObj;
}

static int CellgenTryIterateSplit(COLOR32 *px, unsigned char *accountBuf, int width, int height, int agr, OBJ_BOUNDS *obj, int nObj, int maxDepth) {
	//memset(accountBuf, 0, width * height);

	int nObjInit = nObj;
	for (int i = 0; i < nObjInit; i++) {
		OBJ_BOUNDS *bounds = obj + i;
		//int nTrans = CellgenCountTransparent(px, accountBuf, obj->width, obj->height, bounds);
		//if (nTrans < 64) continue;

		//TODO: adjustable criteria here based on nTrans or nTrans/size?

		int boundBufferSize = 1;
		OBJ_BOUNDS boundBuffer[CELLGEN_MAX_PACK] = { 0 }; //maximum number of split OBJ
		memcpy(boundBuffer, bounds, sizeof(OBJ_BOUNDS));

		//try subdividing to cull regions
		int didCull = CellgenProcessSubdivision(px, accountBuf, width, height, agr, boundBuffer, &boundBufferSize, 0, maxDepth);
		if (!didCull) continue;

		//did cull, so process accordingly
		int nObjWritten = 0;
		for (int j = 0; j < CELLGEN_MAX_PACK; j++) {
			if (boundBuffer[j].width == 0 && boundBuffer[j].height == 0) continue; //deleted entry

			//account this created entry
			CellgenAccountRegion(accountBuf, width, height, boundBuffer + j);

			//if nObjWritten == 0, write to bounds (obj[i])
			if (nObjWritten == 0) {
				memcpy(bounds, boundBuffer + j, sizeof(OBJ_BOUNDS));
			} else {
				//else write to obj + nObj
				memcpy(obj + nObj, boundBuffer + j, sizeof(OBJ_BOUNDS));
				nObj++;
			}

			nObjWritten++;
		}
	}

	return nObj;
}

static int CellgenIsSizeAllowed(int w, int h) {
	//up to 32x32 all are allowed
	if (w <= 32 && h <= 32) return 1;

	//beyond that only these are allowed
	if (w == 64 && h == 64) return 1;
	if (w == 64 && h == 32) return 1;
	if (w == 32 && h == 64) return 1;
	return 0;
}

static int CellgenTryCoalesce(OBJ_BOUNDS *obj, int nObj) {
	//try coalesce
	for (int i = 0; i < nObj; i++) {
		OBJ_BOUNDS *obj1 = obj + i;
		int w1 = obj1->width;
		int h1 = obj1->height;

		for (int j = i + 1; j < nObj; j++) {
			OBJ_BOUNDS *obj2 = obj + j;
			int w2 = obj2->width;
			int h2 = obj2->height;

			//must be same size
			if (w1 != w2 || h1 != h2) continue;

			//cannot be 64x64
			if (w1 == 64 && h1 == 64) continue;

			//X or Y must match
			if (obj1->x != obj2->x && obj1->y != obj2->y) continue;

			//test positions
			int removeIndex = -1;
			if (obj1->y == obj2->y) {
				//same row
				if ((obj1->x + w1) == obj2->x && CellgenIsSizeAllowed(w1 * 2, h1)) {
					obj1->width *= 2;
					removeIndex = j;
				}
				if ((obj2->x + w2) == obj1->x && CellgenIsSizeAllowed(w2 * 2, h2)) {
					obj2->width *= 2;
					removeIndex = i;
				}
			} else if (obj1->x == obj2->x) {
				//same column
				if ((obj1->y + h1) == obj2->y && CellgenIsSizeAllowed(w1, h1 * 2)) {
					obj1->height *= 2;
					removeIndex = j;
				}
				if ((obj2->y + h2) == obj1->y && CellgenIsSizeAllowed(w2, h2 * 2)) {
					obj2->height *= 2;
					removeIndex = i;
				}
			}

			//remove
			if (removeIndex != -1) {
				memmove(obj + removeIndex, obj + removeIndex + 1, (nObj - removeIndex - 1) * sizeof(OBJ_BOUNDS));

				if (removeIndex == i) i--; //so we can iterate the new entry there
				nObj--;
				break;
			}
		}
	}

	return nObj;
}

static int CellgenTryRemoveOverlapping(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj, int nObj) {
	//scan through list of OBJ and see if they are redundant
	for (int i = 0; i < nObj; i++) {
		OBJ_BOUNDS *obj1 = obj + i;
		memset(accountBuf, 0, width * height);

		for (int j = 0; j < nObj; j++) {
			if (j == i) continue;
			OBJ_BOUNDS *obj2 = obj + j;

			CellgenAccountRegion(accountBuf, width, height, obj2);
		}

		//is OBJ useful?
		int nTrans = CellgenCountTransparent(px, accountBuf, width, height, obj1);
		if (nTrans == obj1->width * obj1->height) {
			//not useful
			memmove(obj1, obj1 + 1, (nObj - i - 1) * sizeof(OBJ_BOUNDS));
			nObj--;
			i--;
		}
	}

	return nObj;
}


static int CellgenTryAggressiveMerge(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj1, OBJ_BOUNDS *obj2) {
	int w = obj1->width, h = obj1->height;
	int x1 = obj1->x, y1 = obj1->y, x2 = obj2->x, y2 = obj2->y;
	if (obj2->width != w || obj2->height != h) return 0; //cannot merge

	//get bounding box of both objects
	int xMin1, yMin1, xMax1, yMax1;
	int xMin2, yMin2, xMax2, yMax2;
	CellgenGetXYBounds(px, accountBuf, width, height, x1, x1 + w, y1, y1 + h, &xMin1, &xMax1, &yMin1, &yMax1);
	CellgenGetXYBounds(px, accountBuf, width, height, x2, x2 + w, y2, y2 + h, &xMin2, &xMax2, &yMin2, &yMax2);

	//get total bounding box
	int xMin = min(xMin1, xMin2);
	int xMax = max(xMax1, xMax2);
	int yMin = min(yMin1, yMin2);
	int yMax = max(yMax1, yMax2);

	//bounding region within twice the region of the input OBJ?
	int boundWidth = xMax - xMin;
	int boundHeight = yMax - yMin;
	if (boundWidth > w && boundHeight > h) return 0; //cannot do in one merge (must be within one dimension)

	//within an existing bound??
	if (boundWidth <= w && boundHeight <= h) {
		obj1->x = xMin;
		obj1->y = yMin;
		return 1;
	}

	//within twice width?
	if (boundWidth <= (2 * w) && boundHeight <= h) {
		int newWidth = w * 2;
		if (!CellgenIsSizeAllowed(newWidth, h)) return 0;

		obj1->x = xMin;
		obj1->y = yMin;
		obj1->width = newWidth;
		return 1;
	}

	//within twice height?
	if (boundHeight <= (2 * h) && boundWidth <= w) {
		int newHeight = h * 2;
		if (!CellgenIsSizeAllowed(w, newHeight)) return 0;

		obj1->x = xMin;
		obj1->y = yMin;
		obj1->height *= 2;
		return 1;
	}

	return 0;
}

static int CellgenCoalesceAggressively(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj, int nObj) {
	memset(accountBuf, 0, width * height);

	//some cells may not be directly adjacent but still make for viable merges. 
	//the goal here is to coalesce those that would be candidates for this kind
	//of merge.
	for (int i = 0; i < nObj; i++) {
		OBJ_BOUNDS *obj1 = obj + i;
		int w1 = obj1->width;
		int h1 = obj1->height;

		for (int j = i + 1; j < nObj; j++) {
			OBJ_BOUNDS *obj2 = obj + j;
			int w2 = obj2->width;
			int h2 = obj2->height;

			//must be same size
			if (w1 != w2 || h1 != h2) continue;

			//cannot be 64x64
			if (w1 == 64 && h1 == 64) continue;

			//set account buffer to every OBJ excluding these two
			memset(accountBuf, 0, width * height);
			for (int k = 0; k < nObj; k++) {
				if (k == i || k == j) continue;
				CellgenAccountRegion(accountBuf, width, height, obj + k);
			}

			//can these merge?
			int merged = CellgenTryAggressiveMerge(px, accountBuf, width, height, obj1, obj2);

			//if merged...
			if (merged) {
				//consume obj2
				memmove(obj2, obj2 + 1, (nObj - j - 1) * sizeof(OBJ_BOUNDS));

				nObj--;
				i--;
				break;
			}
		}
	}

	return nObj;
}

static int CellgenCondenseObj(COLOR32 *px, unsigned char *accountBuf, int width, int height, int cx, int cy, OBJ_BOUNDS *obj, int nObj) {
	memset(accountBuf, 0, width * height);

	//push towards center
	for (int i = 0; i < nObj; i++) {
		OBJ_BOUNDS *o = obj + i;
		int x = o->x + o->width / 2;
		int y = o->y + o->height / 2;

		//if x > cx, push left
		//if y > cy, push up
		int flag = 0;
		if (x > cx) flag |= SHIFT_FLAG_RIGHT;  //right edge
		if (y > cy) flag |= SHIFT_FLAG_BOTTOM; //bottom edge
		CellgenShiftObj(px, accountBuf, width, height, o, 1, flag | SHIFT_FLAG_NOREMOVE);
		CellgenAccountRegion(accountBuf, width, height, o);
	}

	return nObj;
}

static int CellgenRemoveHalfRedundant(COLOR32 *px, unsigned char *accountBuf, int width, int height, OBJ_BOUNDS *obj, int nObj) {
	//for each, add all others to account buffer and remove half
	for (int i = 0; i < nObj; i++) {
		memset(accountBuf, 0, width * height);

		for (int j = 0; j < nObj; j++) {
			if (j == i) continue;
			CellgenAccountRegion(accountBuf, width, height, obj + j);
		}

		//try make split
		int n = CellgenTryIterateSplit(px, accountBuf, width, height, 100, obj + i, 1, 1);
		if (n == 0) {
			//object was removed
			memmove(obj + i, obj + i + 1, (nObj - i - 1) * sizeof(OBJ_BOUNDS));
			nObj--;
			i--;
		}
	}

	return nObj;
}

static int CellgenSizeComparator(const void *v1, const void *v2) {
	OBJ_BOUNDS *b1 = (OBJ_BOUNDS *) v1;
	OBJ_BOUNDS *b2 = (OBJ_BOUNDS *) v2;
	return (b2->width * b2->height) - (b1->width * b1->height);
}

static int CellgenPositionComparator(const void *v1, const void *v2) {
	OBJ_BOUNDS *b1 = (OBJ_BOUNDS *) v1;
	OBJ_BOUNDS *b2 = (OBJ_BOUNDS *) v2;

	//scan top-down left-right
	if (b2->y > b1->y) return -1;
	if (b2->y < b1->y) return 1;
	if (b2->x > b1->x) return -1;
	if (b2->x < b1->x) return 1;
	return 0;
}

OBJ_BOUNDS *CellgenMakeCell(COLOR32 *px, int width, int height, int aggressiveness, int full, int *pnObj) {
	//get image bounds
	int xMin, xMax, yMin, yMax;
	CellgenGetXYBounds(px, NULL, width, height, 0, width, 0, height, &xMin, &xMax, &yMin, &yMax);

	//if full image rectangle requested
	if (full) {
		xMin = yMin = 0;
		xMax = width;
		yMax = height;
	}

	int boundingWidth = xMax - xMin;
	int boundingHeight = yMax - yMin;
	
	//pump up bounds to a multiple of 8
	boundingWidth = (boundingWidth + 7) & ~7;
	boundingHeight = (boundingHeight + 7) & ~7;
	xMax = xMin + boundingWidth;
	yMax = yMin + boundingHeight;

	//trivial case: (0, 0) size
	if (boundingWidth == 0 && boundingHeight == 0) {
		*pnObj = 0;
		return NULL;
	}

	//trivial case: one 8x8 OBJ required
	if (boundingWidth <= 8 && boundingHeight <= 8) {
		OBJ_BOUNDS *obj = (OBJ_BOUNDS *) malloc(sizeof(OBJ_BOUNDS));
		obj->x = xMin;
		obj->y = yMin;
		obj->width = 8;
		obj->height = 8;

		*pnObj = 1;
		return obj;
	}

	int nObj = 0;
	OBJ_BOUNDS *obj = (OBJ_BOUNDS *) calloc(256 / 8 * 512 / 8, sizeof(OBJ_BOUNDS)); //max possible

	//greedily split into OBJ as large as we can fit
	for (int y = yMin; y < yMax;) {
		//get OBJ height for this row
		int yRemaining = CELLGEN_ROUNDUP8(yMax - y);
		int objHeight = CellgenGetLargestObjDimension(yRemaining);

		//scan across
		for (int x = xMin; x < xMax;) {
			int xRemaining = CELLGEN_ROUNDUP8(xMax - x);
			int objWidth = CellgenGetLargestObjDimension(xRemaining);

			//for this OBJ height, some widths may be disallowed.
			objWidth = CellgenAdjustCorrectObjWidth(objHeight, objWidth);

			//slot in
			obj[nObj].x = x;
			obj[nObj].y = y;
			obj[nObj].width = objWidth;
			obj[nObj].height = objHeight;
			nObj++;

			//next column
			x += objWidth;
		}

		//next row
		y += objHeight;
	}

	//remove all OBJ not occupying any opaque pixels
	if (aggressiveness > 0) {
		for (int i = 0; i < nObj; i++) {
			OBJ_BOUNDS *bounds = obj + i;

			//check bounding region
			int bxMin, bxMax, byMin, byMax;
			CellgenGetXYBounds(px, NULL, width, height, bounds->x, bounds->x + bounds->width, bounds->y, bounds->y + bounds->height,
				&bxMin, &bxMax, &byMin, &byMax);

			if (bxMin == bxMax && byMin == byMax) {
				//remove
				memmove(obj + i, obj + i + 1, (nObj - i - 1) * sizeof(OBJ_BOUNDS));
				nObj--;
				i--;
			}
		}
	}

	//we now have a set of OBJ that fully cover the image region with all OBJ
	//covering some amount of pixels. Now, adjust the OBJ positions so that their
	//pixels are towards the top and left edge.
	
	//create a "pixels accounted" buffer, 1 byte per pixel of image. It will keep
	//track of which opaque pixels of image have been accounted for in some OBJ. 
	//this will be useful for potentially overlapping OBJ, so we know which pixels
	//overlap and don't need to worry about (so we may potentially move another OBJ
	//over).
	unsigned char *accountBuf = (unsigned char *) calloc(width * height, sizeof(unsigned char));
	
	//run one shift round
	if (aggressiveness > 0)
		nObj = CellgenIterateAllShifts(px, accountBuf, width, height, obj, nObj);

	//run 6 rounds (maximum possible times an OBJ can be divided)
	if (aggressiveness > 0) {
		for (int i = 0; i < CELLGEN_MAX_DIV; i++) {
			//next, begin the subdivision step.
			memset(accountBuf, 0, width * height);
			nObj = CellgenTryIterateSplit(px, accountBuf, width, height, aggressiveness, obj, nObj, CELLGEN_MAX_DIV);
			nObj = CellgenTryCoalesce(obj, nObj);

			//iterate OBJ shift again
			if (aggressiveness > 0)
				nObj = CellgenIterateAllShifts(px, accountBuf, width, height, obj, nObj);

			//remove any overlapped
			nObj = CellgenTryRemoveOverlapping(px, accountBuf, width, height, obj, nObj);
		}
	}

	//try to aid in coalescing: push objects towards the center and remove overlapping
	if (aggressiveness > 0) {
		CellgenCondenseObj(px, accountBuf, width, height, (xMin + xMax) / 2, (yMin + yMax) / 2, obj, nObj);
		nObj = CellgenTryRemoveOverlapping(px, accountBuf, width, height, obj, nObj);

		//reverse OBJ array and try splitting one last time
		for (int i = 0; i < nObj / 2; i++) {
			OBJ_BOUNDS aux;
			memcpy(&aux, obj + i, sizeof(aux));
			memcpy(obj + i, obj + nObj - i - 1, sizeof(aux));
			memcpy(obj + nObj - i - 1, &aux, sizeof(aux));
		}

		memset(accountBuf, 0, width * height);
		nObj = CellgenTryIterateSplit(px, accountBuf, width, height, aggressiveness, obj, nObj, CELLGEN_MAX_DIV);
		nObj = CellgenTryRemoveOverlapping(px, accountBuf, width, height, obj, nObj);
	}

	//try aggressive coalesce
	if (aggressiveness > 0) {
		for (int i = 0; i < CELLGEN_MAX_DIV; i++) {
			nObj = CellgenCoalesceAggressively(px, accountBuf, width, height, obj, nObj);
		}
	}

	//order from big->small
	qsort(obj, nObj, sizeof(OBJ_BOUNDS), CellgenSizeComparator);

	//remove objects that are over half overlapped by another, starting from big
	if (aggressiveness > 0) {
		nObj = CellgenTryRemoveOverlapping(px, accountBuf, width, height, obj, nObj);
		nObj = CellgenRemoveHalfRedundant(px, accountBuf, width, height, obj, nObj);
	}

	//sort OBJ by position
	qsort(obj, nObj, sizeof(OBJ_BOUNDS), CellgenPositionComparator);

	//resize buffer and return
	free(accountBuf);
	obj = realloc(obj, nObj * sizeof(OBJ_BOUNDS));
	*pnObj = nObj;
	return obj;
}

OBJ_BOUNDS *CellgenEnsureRatio(OBJ_BOUNDS *obj, int nObj, int maxRatio, int *pnOutObj) {
	//for each OBJ, split any whose aspect ratio exceeds maxRatio
	for (int i = 0; i < nObj; i++) {
		OBJ_BOUNDS *bound = obj + i;
		int wide = (bound->width > bound->height);

		int ratio = 1;
		if (wide) ratio = bound->width / bound->height;
		else ratio = bound->height / bound->width;

		if (ratio <= maxRatio) continue;

		//make space for new OBJ
		obj = realloc(obj, (nObj + 1) * sizeof(OBJ_BOUNDS));
		memmove(obj + i + 1, obj + i, (nObj - i) * sizeof(OBJ_BOUNDS));
		nObj++;

		//split
		OBJ_BOUNDS temp[2];
		CellgenSplitObj(bound, wide ? CELLGEN_DIR_H : CELLGEN_DIR_V, temp + 0, temp + 1);
		memcpy(obj + i, temp, sizeof(temp));
		i--; //allow obj to be recursively processed
	}

	*pnOutObj = nObj;
	return obj;
}

void CellgenGetBounds(COLOR32 *px, int width, int height, int *pxMin, int *pxMax, int *pyMin, int *pyMax) {
	//get bounding box
	CellgenGetXYBounds(px, NULL, width, height, 0, width, 0, height, pxMin, pxMax, pyMin, pyMax);
}

OBJ_IMAGE_SLICE *CellgenSliceImage(COLOR32 *px, int width, int height, OBJ_BOUNDS *bounds, int nObj) {
	unsigned char *accountBuf = (unsigned char *) calloc(width * height, 1);
	OBJ_IMAGE_SLICE *slices = (OBJ_IMAGE_SLICE *) calloc(nObj, sizeof(OBJ_IMAGE_SLICE));
	
	//go in order
	for (int i = 0; i < nObj; i++) {
		OBJ_IMAGE_SLICE *slice = slices + i;
		OBJ_BOUNDS *obj = bounds + i;
		memcpy(&slice->bounds, obj, sizeof(OBJ_BOUNDS));

		//get pixel array
		for (int y = obj->y; y < obj->y + obj->height; y++) {
			for (int x = obj->x; x < obj->x + obj->width; x++) {
				if (x < 0 || y < 0 || x >= width || y >= height) continue;

				int objX = x - obj->x, objY = y - obj->y;
				COLOR32 col = px[x + y * width];
				int ignore = accountBuf[x + y * width];
				if (!ignore) {
					slice->px[objX + objY * obj->width] = col;
				}
			}
		}

		//account it
		CellgenAccountRegion(accountBuf, width, height, obj);
	}

	free(accountBuf);
	return slices;
}
