#pragma once

#include <stdint.h>

#include "CxPrivate.h"


typedef enum CxiLzStatus_ {
	CX_LZ_OK,      // Success
	CX_LZ_NOMEM    // No memory
} CxiLzStatus;


//struct for representing tokenized LZ data
typedef struct CxiLzToken_ {
	uint8_t isReference;
	union {
		uint8_t symbol;
		struct {
			int16_t length;
			int16_t distance;
		};
	};
} CxiLzToken;

//struct for keeping track of LZ sliding window
typedef struct CxiLzState_ {
	const unsigned char *buffer;
	unsigned int size;
	unsigned int pos;
	unsigned int minLength;
	unsigned int maxLength;
	unsigned int minDistance;
	unsigned int maxDistance;
	unsigned int symLookup[512];
	unsigned int *chain;
	unsigned int (*pfnHash) (const unsigned char *p);

	unsigned char *restrictDst;  // map of allowed distances
	unsigned char *restrictLen;  // map of allowed lengths
	unsigned int *lengthDown;    // map of valid length rounded down
} CxiLzState;

//struct for mapping an LZ graph
typedef struct CxiLzNode_ {
	uint32_t distance;         // distance of node if reference
	uint32_t length;           // length of node
	uint32_t weight;           // weight of node
} CxiLzNode;

typedef unsigned int (*CxiLzNodeEvaluator) (unsigned int length);




int CxiLzConfirmMatch(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         pos,
	unsigned int         distance,
	unsigned int         length
);



CxiLzStatus CxiLzStateInit(
	CxiLzState          *state,
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance
);

void CxiLzStateFree(
	CxiLzState *state
);

void CxiLzStateSlide(
	CxiLzState  *state,
	unsigned int nSlide
);

unsigned int CxiLzSearch(
	CxiLzState   *state,
	unsigned int *pDistance
);

CxiLzStatus CxiLzEnableRestrictDistance(
	CxiLzState *state,
	int         enable
);

void CxiLzAllowDistance(
	CxiLzState  *state,
	unsigned int distance
);

CxiLzStatus CxiLzEnableRestrictLength(
	CxiLzState *state,
	int         enable
);

void CxiLzAllowLength(
	CxiLzState  *state,
	unsigned int length
);

CxiLzStatus CxiLzFinalizeAllowedLengths(
	CxiLzState *state
);



// ----- graph building routines 

CxiLzNode *CxiLzGraphExploreOnState(
	CxiLzState *state
);

CxiLzNode *CxiLzGraphExplore(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance
);

void CxiLzGraphCollapse(
	const unsigned char *buffer,
	CxiLzNode           *nodes,
	unsigned int         size,
	unsigned int         minLength,
	CxiLzNodeEvaluator   pfnNodeEval
);

CxiLzToken *CxiLzGraphToTokens(
	const unsigned char *buffer,
	const CxiLzNode     *nodes,
	unsigned int         size,
	unsigned int        *pnToken
);



// ----- Token operations

CxiLzToken *CxiLzTokenizeGreedy(
	const unsigned char *buffer,
	unsigned int         size,
	unsigned int         minLength,
	unsigned int         maxLength,
	unsigned int         minDistance,
	unsigned int         maxDistance,
	unsigned int        *pnTokens
);

