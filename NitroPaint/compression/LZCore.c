#include <string.h>

#include "LZCore.h"
#include "struct.h"


// ----- Common LZ subroutines

static unsigned int CxiLzHash3(const unsigned char *p) {
	unsigned char c0 = p[0];         // A
	unsigned char c1 = p[0] ^ p[1];  // A ^ B
	unsigned char c2 = p[0] ^ p[2];  // (A ^ B) ^ (B ^ C)
	return (c0 ^ (c1 << 1) ^ (c2 << 2) ^ (c2 >> 7)) & 0x1FF;
}

static unsigned int CxiLzHash2(const unsigned char *p) {
	unsigned char c0 = p[0];         // A
	unsigned char c1 = p[0] ^ p[1];  // A ^ B
	return (c0 ^ (c1 << 2) ^ (c1 >> 7)) & 0x1FF;

}

void CxiLzStateInit(
	CxiLzState          *state,
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance
) {
	//internally, we don't support a min copy length of 1 byte.
	if (minLength < 2) minLength = 2;

	state->buffer = buffer;
	state->size = size;
	state->pos = 0;
	state->minLength = minLength;
	state->maxLength = maxLength;
	state->minDistance = minDistance;
	state->maxDistance = maxDistance;

	//set the hashing routine. If the min length is greater than 2, we use the 3-byte hash.
	//otherwise, we fall back to the 2-byte hash.
	if (minLength > 2) {
		state->pfnHash = CxiLzHash3;
	} else {
		state->pfnHash = CxiLzHash2;
	}

	for (unsigned int i = 0; i < 512; i++) {
		//init symbol lookup to empty
		state->symLookup[i] = UINT_MAX;
	}

	state->chain = (unsigned int *) calloc(state->maxDistance, sizeof(unsigned int));
	for (unsigned int i = 0; i < state->maxDistance; i++) {
		state->chain[i] = UINT_MAX;
	}
}

void CxiLzStateFree(CxiLzState *state) {
	free(state->chain);
}

static unsigned int CxiLzStateGetChainIndex(CxiLzState *state, unsigned int index) {
	return (state->pos - index) % state->maxDistance;
}

static unsigned int CxiLzStateGetChain(CxiLzState *state, int index) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	return state->chain[chainIndex];
}

static void CxiLzStatePutChain(CxiLzState *state, unsigned int index, unsigned int data) {
	unsigned int chainIndex = CxiLzStateGetChainIndex(state, index);

	state->chain[chainIndex] = data;
}

static void CxiLzStateSlideByte(CxiLzState *state) {
	if (state->pos >= state->size) return; // cannot slide

	//only update search structures when we have enough space left to necessitate searching.
	if ((state->size - state->pos) >= state->minLength) {
		//fetch next 3 bytes' hash
		unsigned int next = state->pfnHash(state->buffer + state->pos);

		//get the distance back to the next byte before sliding. If it exists in the window,
		//we'll have nextDelta less than UINT_MAX. We'll take this first occurrence and it 
		//becomes the offset from the current byte. Bear in mind the chain is 0-indexed starting
		//at a distance of 1. 
		unsigned int nextDelta = state->symLookup[next];
		if (nextDelta != UINT_MAX) {
			nextDelta++;
			if (nextDelta >= state->maxDistance) {
				nextDelta = UINT_MAX;
			}
		}
		CxiLzStatePutChain(state, 0, nextDelta);

		//increment symbol lookups
		for (int i = 0; i < 512; i++) {
			if (state->symLookup[i] != UINT_MAX) {
				state->symLookup[i]++;
				if (state->symLookup[i] > state->maxDistance) state->symLookup[i] = UINT_MAX;
			}
		}
		state->symLookup[next] = 0; // update entry for the current byte to the start of the chain
	}

	state->pos++;
}

void CxiLzStateSlide(CxiLzState *state, unsigned int nSlide) {
	while (nSlide--) CxiLzStateSlideByte(state);
}

static unsigned int CxiCompareMemory(
	const unsigned char *b1,
	const unsigned char *b2,
	unsigned int         nMax
) {
	//compare nMax bytes
	unsigned int nSame = 0;
	while (nMax > 0) {
		if (*(b1++) != *(b2++)) break;
		nMax--;
		nSame++;
	}
	return nSame;
}

int CxiLzConfirmMatch(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         pos,
	unsigned int         distance,
	unsigned int         length
) {
	(void) size;

	//compare string match
	return memcmp(buffer + pos, buffer + pos - distance, length) == 0;
}

unsigned int CxiLzSearch(
	CxiLzState   *state,
	unsigned int *pDistance
) {
	unsigned int nBytesLeft = state->size - state->pos;
	if (nBytesLeft < state->minLength) {
		*pDistance = 0;
		return 1;
	}

	unsigned int firstMatch = state->symLookup[state->pfnHash(state->buffer + state->pos)];
	if (firstMatch == UINT_MAX) {
		//return byte literal
		*pDistance = 0;
		return 1;
	}

	unsigned int distance = firstMatch + 1;
	unsigned int bestLength = 1, bestDistance = 0;

	unsigned int nMaxCompare = state->maxLength;
	if (nMaxCompare > nBytesLeft) nMaxCompare = nBytesLeft;

	//search backwards
	const unsigned char *curp = state->buffer + state->pos;
	while (distance <= state->maxDistance) {
		//check only if distance is at least minDistance
		if (distance >= state->minDistance) {
			//check if distance filtering is used
			int allowDst = 1;
			if (state->restrictDst != NULL) allowDst = state->restrictDst[distance];

			if (allowDst) {
				//confirm a match
				unsigned int matchLen = CxiCompareMemory(curp - distance, curp, nMaxCompare);
				if (matchLen > bestLength) {
					bestLength = matchLen;
					bestDistance = distance;
					if (bestLength == nMaxCompare) break;
				}
			}
		}

		if (distance == state->maxDistance) break;

		unsigned int next = CxiLzStateGetChain(state, distance);
		if (next == UINT_MAX) break;

		distance += next;
	}

	//if the best length we've found is less than the minimum allowed length, don't report a match
	if (bestLength < state->minLength) {
		bestLength = 1;
		distance = 0;
	}
	*pDistance = bestDistance;
	return bestLength;
}



// ----- graph building routines 

CxiLzNode *CxiLzGraphExplore(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance
) {
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, minLength, maxLength, minDistance, maxDistance);

	//create node list and fill in the maximum string reference sizes
	CxiLzNode *nodes = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
	unsigned int pos = 0;
	while (pos < size) {
		unsigned int dst;
		unsigned int len = CxiLzSearch(&state, &dst);

		//store longest found match
		nodes[pos].length = len;
		nodes[pos].distance = dst;

		pos++;
		CxiLzStateSlide(&state, 1);
	}
	CxiLzStateFree(&state);

	return nodes;
}

void CxiLzGraphCollapse(
	const unsigned char *buffer,
	CxiLzNode           *nodes,
	unsigned int         size,
	unsigned int         minLength,
	CxiLzNodeEvaluator   pfnNodeEval
) {
	//work backwards from the end of file
	unsigned int pos = size;
	while (pos--) {
		//get node at pos
		CxiLzNode *node = &nodes[pos];

		//search for largest LZ string match
		unsigned int len = nodes[pos].length;
		unsigned int dist = nodes[pos].distance;

		//if node takes us to the end of the buffer, set weight to cost of this node.
		if ((pos + len) == size) {
			//token takes us to the end of the buffer, its weight equals this token cost.
			node->length = len;
			node->distance = dist;
			node->weight = pfnNodeEval(len);
		} else {
			//else, search LZ matches from here down.
			unsigned int weightBest = UINT_MAX;
			unsigned int lenBest = 1;
			while (len) {
				//measure cost
				unsigned int weightNext = nodes[pos + len].weight;
				unsigned int weight = pfnNodeEval(len) + weightNext;
				if (weight < weightBest) {
					lenBest = len;
					weightBest = weight;
				}

				//decrement length w.r.t. length discontinuity
				len--;
				if (len != 0 && len < minLength) len = 1;
			}

			//put node
			node->length = lenBest;
			node->distance = dist;
			node->weight = weightBest;
		}
	}
}

void CxiLzEnableRestrictDistance(
	CxiLzState *state,
	int         enable
) {
	//enable or disable distance restriction
	if (enable) {
		//either allocate or clear
		if (state->restrictDst == NULL) {
			state->restrictDst = (unsigned char *) calloc(state->maxDistance + 1, 1);
		} else {
			memset(state->restrictDst, 0, state->maxDistance + 1);
		}
	} else {
		//disable (free list)
		free(state->restrictDst);
		state->restrictDst = NULL;
	}
}

void CxiLzAllowDistance(
	CxiLzState  *state,
	unsigned int distance
) {
	//check distance
	if (state->restrictDst == NULL || distance > state->maxDistance) return;

	state->restrictDst[distance] = 1;
}

CxiLzToken *CxiLzGraphToTokens(
	const unsigned char *buffer,
	const CxiLzNode     *nodes,
	unsigned int         size,
	unsigned int        *pnToken
) {
	//traverse the node buffer to get the length in tokens
	unsigned int nToken = 0, pos = 0;
	while (pos < size) {
		const CxiLzNode *node = &nodes[pos];

		nToken++;
		pos += node->length;
	}
	
	//allocate token buffer
	CxiLzToken *tokens = (CxiLzToken *) calloc(nToken, sizeof(CxiLzToken));

	pos = 0;
	for (unsigned int i = 0; i < nToken; i++) {
		const CxiLzNode *node = &nodes[pos];
		CxiLzToken *tok = &tokens[i];

		if (node->length > 1) {
			//reference (length>1)
			tok->isReference = 1;
			tok->length = node->length;
			tok->distance = node->distance;
		} else {
			//byte literal (length==1)
			tok->isReference = 0;
			tok->symbol = buffer[i];
		}

		pos += node->length;
	}
	
	*pnToken = nToken;
	return tokens;
}



// ----- Token operations

CxiLzToken *CxiLzTokenizeGreedy(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance,
	unsigned int        *pnTokens
) {
	StList tokenBuffer;
	StStatus s = StListCreateInline(&tokenBuffer, CxiLzToken, NULL);
	if (!ST_SUCCEEDED(s)) return NULL;

	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, minLength, maxLength, minDistance, maxDistance);

	//feed tokens
	unsigned int curpos = 0;
	while (curpos < size) {
		//search backwards
		unsigned int length, distance;
		length = CxiLzSearch(&state, &distance);

		CxiLzToken token;
		if (length > 1) {
			token.isReference = 1;
			token.length = length;
			token.distance = distance;

			curpos += length;
		} else {
			token.isReference = 0;
			token.symbol = buffer[curpos++];
		}

		s = StListAdd(&tokenBuffer, &token);
		if (!ST_SUCCEEDED(s)) goto Error;

		CxiLzStateSlide(&state, length);
	}

	CxiLzStateFree(&state);
	*pnTokens = tokenBuffer.length;
	return (CxiLzToken *) tokenBuffer.buffer;

Error:
	CxiLzStateFree(&state);
	StListFree(&tokenBuffer);
	*pnTokens = 0;
	return NULL;
}
