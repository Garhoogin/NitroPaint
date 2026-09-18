#include <string.h>

#include "LZCore.h"

// -----------------------------------------------------------------------------------------------
// The PuCrunch Compression Format
//
// This is a format used in some games from Griptonite. It combines variable width LZ and RLE 
// into a single format. Rather than marking tokens as literals, LZ, RLE, or end-of-file,
// all bits are assumed to represent byte literals. Special tokens are instead denoted by
// writing the escape sequence, which is defined in the file header, and which may be redefined
// during the stream. Gamma codes are utilized heavily throughout the format.
//
// The compression header is similar to that of the SDK standard compression formats, using an
// identifier of hex 60.
//
// This is followed with a header for the PuCrunch data. This is another 4-byte header, which
// specifies the parameters for the compression:
//   u8 : RL table size             Must be a nonzero multiple of 4 no more than 32
//   u8 : Initial escape sequence
//   u8 : LZ extra bits             Should be between 0 and 24 (large values rare though)
//   u8 : Escape sequence length    Must be between 0 and 8
//
// The escape sequence may be any length between 0 and 8 bits long. The escape sequence itself
// may change throughout the stream, but the length is fixed from the header. A length of 0
// corresponds to all tokens being considered escape tokens. 
//
// The LZ extra bits indicate the number of extra bits used to encode LZ distances. Greater 
// values correspond to greater possible distances, though all distances become more expensive to
// encode. This value should be chosen based on the file's structure.
//
// The RL table immediately follows the headers. Its length must be nonzero and must be a multiple
// of 4 bytes long. The maximum number of entries is 31, but this must be rounded up in the size
// reported in the header. Because the size must be nonzero, its minimum length is 4 bytes.
//
// The bit stream parsing begins immediately following the RL table. While there are still bytes
// left to output, we read the number of bits in the escape sequence from the stream and check
// if it matches the escape sequence. If it is a match, then this is a special token. Otherwise,
// we should decode a byte literal. For a byte literal, we read the rest of the bit required to
// read a full byte, and output it as a byte literal. 
//
// For special tokens, there are the following types:
//   LZ copy
//   RL run
//   Escaped byte
//   End-of-file
//
// See code in the implementation for how these are distinguished. The format distinguishes
// in particular between 2-byte and long LZ copies. The 2-byte copies have a reduced range
// compared to long copies.
//
// RL copies utilize the RL table toward the start of the file. These byte values may be more 
// efficiently as repeated bytes. The array is indexed using Gamma codes, thus values closer
// to the front are used most efficiently. Since the table size is 31 entries at most, it is
// possible to specify arbitrary byte values outside the table as well, though with reduced
// storage efficiency. These length extensions only apply to long LZ copies.
//
// Escaped bytes give the stream the opportunity to redefine the escape sequence. Intuitively, an
// encoder should choose a bit sequence that doesn't appear in the file or is particularly rare.
// Sometimes it can't be guaranteed, and it could be that following the point where the escape
// appeared, the distribution of byte values is changed. The encoder may change the escape
// sequence here. Otherwise, it may write the same escape sequence again. 
// -----------------------------------------------------------------------------------------------

#define PUCRUNCH_MIN_DISTANCE    1


// ----- Compression routines

//
// length table for Gamma codes
//
static const unsigned char sGammaLengthTable[] = {
	 0,  1,  3,  3,  5,  5,  5,  5,  7,  7,  7,  7,  7,  7,  7,  7,
	 9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14
};

//
// estimated cost of a byte literal per # escape bits
//
static const unsigned char sEscapedLiteralEstimateTable[] = {
	11, 9, 9, 8, 8, 8, 8, 8, 8
};


//
// An entry in the RL frequency table to identify oft-used bytes.
//
typedef struct CxiPcRLlTableEntry_ {
	unsigned char entry;
	unsigned int freq;
} CxiPcRlTableEntry;

//
// Comparator for sorting the RL table (by descending frequency)
//
static int CxiPcRlTableEntryComparator(
	const void *e1,  // Object 1
	const void *e2   // Object 2
) {
	const CxiPcRlTableEntry *t1 = (const CxiPcRlTableEntry *) e1;
	const CxiPcRlTableEntry *t2 = (const CxiPcRlTableEntry *) e2;

	if (t1->freq < t2->freq) return  1;
	if (t1->freq > t2->freq) return -1;
	return 0;
}

//
// Write a Gamma code
//
static void CxiBitWriterWriteGamma(CxiBitWriter *writer, uint32_t x) {
	CX_ASSERT(x >= 1 && x <= 0xFF);

	//find number of bits
	unsigned int nBit = 0;
	uint32_t y = x;
	while (y) {
		y >>= 1;
		nBit++;
	}

	//write nBit-1 ones, up to 7 (followed by a zero, unless all ones)
	nBit--;
	for (unsigned int i = 0; i < nBit; i++) {
		CxiBitWriterWriteBit(writer, 1);
	}
	if (nBit < 7) CxiBitWriterWriteBit(writer, 0);

	//low bits
	x -= 1 << nBit;
	CxiBitWriterWriteBitsBE(writer, x, nBit);
}

//
// Calculate the bit length of an 8-bit Gamma code.
//
static inline unsigned int CxiPcGammaLength(
	unsigned int x  // The number to encode.
) {
	CX_ASSERT(x >= 1 && x <= 0xFF);    // cannot encode zero or above 0xFF

	return sGammaLengthTable[x];
}

//
// Calculate the estimated bit length of a literal byte, given a number of escape bits.
//
static inline unsigned int CxiPcCalcLiteralCost(
	unsigned int escBits  // The number of escape bits.
) {
	CX_ASSERT(escBits <= 8);

	//approximate. On average: additional (3+x)/2^x bit cost per x escape bits
	return sEscapedLiteralEstimateTable[escBits];
}

//
// Calculate the bit length of an LZ node.
//
static unsigned int CxiPcCalcLzCost(
	unsigned int escBits,  // The number of escape bits.
	unsigned int nLzBits,  // The number of low LZ bits (8+extra)
	unsigned int length,   // The length of the LZ node, in bytes
	unsigned int distance  // The distance of the LZ node, in bytes
) {
	CX_ASSERT(nLzBits >= 8);
	CX_ASSERT(length >= 2 && length <= 0x100);
	CX_ASSERT(distance > 0 && distance <= (0xFEu << nLzBits));

	if (length == 2) {
		//short LZ copy: 00
		CX_ASSERT(distance <= 256);
		return escBits + 2 + 8;
	} else {
		//long LZ copy
		unsigned int costHi = CxiPcGammaLength(((distance - 1) >> nLzBits) + 1);
		return escBits + CxiPcGammaLength(length - 1) + nLzBits + costHi;
	}
}

//
// Calculate the bit length of an RL node.
//
static unsigned int CxiPcCalcRlCost(
	unsigned int escBits,  // The number of escape bits.
	unsigned int codelen,  // The cost of the RL byte.
	unsigned int rlLen     // The length of the RL node, in bytes.
) {
	//RL run
	CX_ASSERT(rlLen >= 2);

	unsigned int lenCost = 0;
	rlLen--;
	if (rlLen >= 0x80) {
		lenCost += CxiPcGammaLength((rlLen >> 8) + 1);
		rlLen &= 0xFF;
		rlLen = (rlLen >> 1) | 0x80;
	}
	lenCost += CxiPcGammaLength(rlLen);

	return escBits + 3 + lenCost + codelen;
}

//
// Calculate the bit length of a node in the compressed output.
//
static unsigned int CxiPcCalcNodeCost(
	const CxiLzNode *node,       // The compression node.
	unsigned char    rlCodeLen,  // The RL code length of a byte at this position.
	unsigned int     escBits,    // The number of escape bits.
	unsigned int     nLzBits     // The number of low LZ bits (8+extra)
) {
	if (node->length == 1) {
		//literal byte
		return CxiPcCalcLiteralCost(escBits);
	} else if (node->distance == 0) {
		//RL node
		if (rlCodeLen == 0) {
			//no code length: optimistically assume 5 bits
			return CxiPcCalcRlCost(escBits, 5, node->length);
		} else {
			//use the code length
			return CxiPcCalcRlCost(escBits, rlCodeLen, node->length);
		}
	} else {
		//LZ node
		return CxiPcCalcLzCost(escBits, nLzBits, node->length, node->distance);
	}
}

//
// Find the best escape bit sequence to use from a given start position.
//
static unsigned char CxiPcFindEscapeBits(
	const CxiLzNode     *nodes,    // The node graph buffer.
	const unsigned char *buffer,   // The byte buffer.
	unsigned int         start,    // The start position.
	unsigned int         size,     // The total input size.
	unsigned int         nEscBit   // The number of escape bits.
) {
	CX_ASSERT(nEscBit <= 8);

	unsigned int nPatterns = 1 << nEscBit;   // number of escape bit patterns
	unsigned int nPatternsLeft = nPatterns;  // number of candidate patterns remaining
	unsigned char usePatterns[256] = { 0 };

	//scan forward from the token buffer to find the longest unused pattern
	unsigned int pos = start;
	while (pos < size) {
		const CxiLzNode *node = &nodes[pos];
		CX_ASSERT(node->length >= 1);

		if (node->length == 1) {
			//byte literal
			unsigned char hi = buffer[pos] >> (8 - nEscBit);

			if (!usePatterns[hi]) {
				usePatterns[hi] = 1;
				nPatternsLeft--;

				//if this was the last pattern, we will use it as the escape bits.
				if (nPatternsLeft == 0) {
					return hi;
				}
			}
		}

		pos += node->length;
	}

	//we've run the whole file without exhausing all patterns. Search for an open one.
	for (unsigned int i = 0; i < nPatterns; i++) {
		if (!usePatterns[i]) return i;
	}
	CX_ASSERT(0); // should not reach here
}

//
// Maps the repeated strings of bytes in the buffer. At each byte position in the output will be
// the number of times that byte appears here and in the immediate future, up to the maximum
// length (FF00h).
//
static void CxiPcExploreRL(
	const unsigned char *buffer,  // The input buffer
	unsigned int         size,    // The input buffer size
	uint16_t            *rlLens   // The output RL length buffer
) {
	//find runs of bytes
	unsigned int nCurRun = 0, iRunStart = 0;
	for (unsigned int i = 0; i <= size; i++) {
		//we run one byte past the size, treating this ghost byte as a terminator.
		if (i == 0 || i == size || buffer[i] != buffer[i - 1]) {
			//using the accumulated count, write found RL matches.
			for (unsigned int j = 0; j < nCurRun; j++) {
				//decrement the length per each byte advance -- limit to FF00 length
				unsigned int nRunHere = nCurRun - j;
				rlLens[iRunStart + j] = nRunHere > 0xFF00 ? 0xFF00 : nRunHere;
			}

			//first byte or mismatched previous byte: reset count to 1
			nCurRun = 1;     // reset length
			iRunStart = i;   // mark start of run
		} else {
			//continuation of previous byte, increment count
			nCurRun++;
		}
	}
}

//
// Explores the input buffer to find the longest repeating byte sequences and LZ string matches.
//
static void CxiPcExploreLzRl(
	const unsigned char *buffer,     // The input buffer
	unsigned int         size,       // The input size
	unsigned int         maxWindow,  // The maximum LZ search window
	CxiLzNode           *nodes,      // The output LZ node array
	uint16_t            *rlLens      // The output RL length array
) {
	CxiLzState state;
	CxiLzStateInit(&state, buffer, size, 2, 256, PUCRUNCH_MIN_DISTANCE, maxWindow);

	//run forwards pass for exploration
	unsigned int pos = 0;
	while (pos < size) {
		//search LZ
		unsigned int distance;
		unsigned int length = CxiLzSearch(&state, &distance);
		nodes[pos].length = length;
		nodes[pos].distance = distance;
		nodes[pos].weight = 0;

		//if there was no LZ match, search for any 2-byte matches.
		if (nodes[pos].length < 2 && (pos + 1) < size) {

			const unsigned char *src = buffer + pos;
			for (unsigned int i = PUCRUNCH_MIN_DISTANCE; i <= 256 && i <= pos; i++) {
				const unsigned char *src2 = buffer + pos - i;
				if (src[0] == src2[0] && src[1] == src2[1]) {
					//found 2-byte
					nodes[pos].length = 2;
					nodes[pos].distance = i;
				}
			}

		}

		CxiLzStateSlide(&state, 1);
		pos++;
	}

	//explore RL
	CxiPcExploreRL(buffer, size, rlLens);

	CxiLzStateFree(&state);
}

//
// Collapses a node array into a token sequence according to greedy encoding.
//
static void CxiPcGraphOptimizeGreedy(
	const unsigned char *buffer,  // The input buffer
	unsigned int         size,    // The input buffer size
	CxiLzNode           *nodes,   // The node array
	const uint16_t      *rlLens   // The RL length array
) {
	(void) buffer;

	//process the graph forwards. At any position, try substituting a copy/repeat for a byte
	//literal. If the next token has a longer match, we take it.
	unsigned int pos = 0;
	while (pos < size) {
		CxiLzNode *node = &nodes[pos];
		unsigned int rlLen = rlLens[pos];

		//if the RL length exceeds the LZ length, we take it instead.
		if (rlLen > node->length) {
			node->length = rlLen;
			node->distance = 0;

		}
		pos++;
	}

	//next pass: explore length reduction
	pos = 0;
	while ((pos + 1) < size) {
		CxiLzNode *node = &nodes[pos];
		CxiLzNode *next = &nodes[pos + 1];

		if (next->length > node->length) {
			//next byte has better match, replace this by byte literal
			node->length = 1;
			node->distance = 0;
		}
		pos += node->length;
	}
}

//
// Collapses a node array into a token sequence according to optimal encoding. The optimal
// encoding is dependent on every step before here, so we may hope for a local optimum.
//
static void CxiPcGraphOptimize(
	const unsigned char *buffer,    // The input buffer.
	unsigned int         size,      // The input buffer size.
	CxiLzNode           *nodes,     // The node array.
	const uint16_t      *rlLens,    // The input RL length buffer.
	unsigned int         escBits,   // The number of escape bits.
	unsigned int         nLzExtra,  // The number of extra low LZ bits.
	const unsigned char *freqTbl,   // The RL high-frequency table.
	unsigned int         nFreqTbl   // The RL high-frequency table size.
) {
	//calculate the cost of each byte literal for use by RL. This makes the RL optimization
	//much faster.
	unsigned int rlByteCosts[0x100];
	for (unsigned int i = 0; i < 0x100; i++) {
		//byte cost without table
		rlByteCosts[i] = 3 + CxiPcGammaLength(0x20 | (i >> 3));
	}
	for (unsigned int i = 0; i < nFreqTbl && i < 31; i++) {
		//byte cost with table (check existing entry: because the table is padded in length)
		unsigned int cost = CxiPcGammaLength(i + 1);
		if (cost < rlByteCosts[freqTbl[i]]) rlByteCosts[freqTbl[i]] = cost;
	}

	//iterate backwards from the end and collapse the graph as we go, minimizing the cost function.
	unsigned pos = size;
	while (pos--) {
		CxiLzNode *node = &nodes[pos];
		unsigned int rlLen = rlLens[pos];

		//we have node->length and rlLen. If node->length > 1, we have LZ. If rlLen > 1, we have RL.
		unsigned int cost = CxiPcCalcNodeCost(node, rlByteCosts[buffer[pos]], escBits, 8 + nLzExtra);
		if ((pos + node->length) < size) cost += nodes[pos + node->length].weight;

		//check LZ.
		if (node->length > 1) {
			unsigned int initLength = node->length, checkLength = node->length;
			while (--checkLength > 1) {
				unsigned int effectiveDistance = node->distance;

				//check checkLength==2: must also check distance is within range
				if (checkLength == 2 && node->distance > 256) {
					//check for in-range distances here (TODO: separate hash table perhaps?)
					unsigned int matchDistance = UINT_MAX;
					for (unsigned int checkDistance = PUCRUNCH_MIN_DISTANCE; checkDistance <= 256 && checkDistance <= pos; checkDistance++) {
						const unsigned char *src = buffer + pos - checkDistance;
						if (src[0] == buffer[pos] && src[1] == buffer[pos + 1]) {
							//found
							matchDistance = checkDistance;
							break;
						}
					}
					if (matchDistance == UINT_MAX) break; // no match

					//else, update the effective distance for this iteration
					effectiveDistance = matchDistance;
				}

				//get next cost (test)
				unsigned int testCost = CxiPcCalcLzCost(escBits, 8 + nLzExtra, checkLength, effectiveDistance);
				if ((pos + checkLength) < size) testCost += nodes[pos + checkLength].weight;

				if (testCost < cost) {
					//update best
					cost = testCost;
					node->length = checkLength;
					node->distance = effectiveDistance;
				}
			}

			if (node->length < initLength && node->length > 2) {
				//try reducing the distance of match (ONLY for long matches: short matches do not
				//benefit from distance reduction, and we may have had to do it for them earlier anyway)
				for (unsigned int checkDistance = PUCRUNCH_MIN_DISTANCE; checkDistance < node->distance; checkDistance++) {
					if (CxiLzConfirmMatch(buffer, size, pos, checkDistance, node->length)) {
						//found
						node->distance = checkDistance;
						break;
					}
				}
			}
		}

		//check RL.
		if (rlLen > 1) {
			unsigned int checkLength = rlLen;
			while (checkLength > 1) {
				unsigned int testCost = CxiPcCalcRlCost(escBits, rlByteCosts[buffer[pos]], checkLength);
				if ((pos + checkLength) < size) testCost += nodes[pos + checkLength].weight;

				if (testCost < cost) {
					//update best (switch to RL)
					cost = testCost;
					node->length = checkLength;
					node->distance = 0; // mark as RL
				}

				checkLength--;

				//HACK: checking only one RL length (not really necessary to check them all in practice)
				break;
			}
		}

		//put weight
		node->weight = cost;
	}
}

//
// Creates the RL high-frequency table.
//
static unsigned int CxiPcCreateRlTable(
	const unsigned char *buffer,  // The input buffer.
	unsigned int         size,    // The input buffer size.
	const CxiLzNode     *nodes,   // The input node buffer.
	unsigned char       *freqTbl  // The output table.
) {
	//next, run forwards and gather statistics for the RL table.
	CxiPcRlTableEntry rlFreq[0x100] = { 0 };
	for (unsigned int i = 0; i < 0x100; i++) rlFreq[i].entry = i;

	unsigned int pos = 0;
	while (pos < size) {
		const CxiLzNode *node = &nodes[pos];

		//increment RL frequency table entry
		if (node->length > 1 && node->distance == 0) {
			unsigned char b = buffer[pos];
			rlFreq[b].freq++;
		}

		pos += node->length;
	}

	//sort the frequency table by frequency, and select the first (up to) 32 elements.
	unsigned int nFreqTbl = 0;
	qsort(rlFreq, 0x100, sizeof(CxiPcRlTableEntry), CxiPcRlTableEntryComparator);

	//take up to the first 31
	for (unsigned int i = 0; i < 31; i++) {
		if (rlFreq[i].freq == 0) break;

		//put
		freqTbl[i] = rlFreq[i].entry;
		nFreqTbl++;
	}
	if (nFreqTbl == 0) nFreqTbl = 1; // table cannot be 0 length
	nFreqTbl = (nFreqTbl + 3) & ~3;  // round up to mult. of 4

	return nFreqTbl;
}

//
// Writes a compressed node sequence to a byte array,
//
static unsigned char *CxiPcWriteCompression(
	const unsigned char  *buffer,    // The input buffer
	unsigned int          size,      // The input buffer size
	const CxiLzNode      *nodes,     // The input node buffer
	unsigned int          escBits,   // The number of escape bits
	unsigned int          nLzExtra,  // The number of extra low LZ bits
	const unsigned char  *freqTbl,   // The frequency table
	unsigned int          nFreqTbl,  // The frequency table size
	unsigned int         *pOutSize   // The output buffer size
) {
	CX_ASSERT(nFreqTbl <= 0x20 && nFreqTbl > 0 && !(nFreqTbl & 3));

	//find the longest LZ offset in the file. We may not need as many bits to represent
	//the final string as we used to budget for the initial tokenization.
	//NOTE: this makes the size w.r.t. the # of extra LZ bits non monotonic!!
	unsigned int maxOffset = 1;
	unsigned int pos = 0;
	while (pos < size) {
		const CxiLzNode *node = &nodes[pos];

		if (node->length > 2 && node->distance > 0) {
			//specifically >2 length LZ, since only those use the extra LZ bits.
			if (node->distance > maxOffset) maxOffset = node->distance;
		}

		pos += node->length;
	}

	//how many extra bits do we need? Low 8-bit fixed offset + upper Gamma code
	{
		unsigned int nNeededLzExtra = 0;
		unsigned int hiBits = (maxOffset - 1) >> 8;
		while (hiBits > 0xFD) {
			hiBits >>= 1;
			nNeededLzExtra++;
		}
		nLzExtra = nNeededLzExtra;
	}
	unsigned int nLzBits = 8 + nLzExtra;

	//get the initial escape sequence.
	unsigned char initEsc = CxiPcFindEscapeBits(nodes, buffer, 0, size, escBits);
	unsigned char curEsc = initEsc;

	CxiBitWriter writer;
	CxiBitWriterInit(&writer);

	pos = 0;
	while (pos < size) {
		const CxiLzNode *node = &nodes[pos];

		if (node->length == 1) {
			unsigned int hi = buffer[pos] >> (8 - escBits);
			if (hi == curEsc) {
				CxiBitWriterWriteBitsBE(&writer, curEsc, escBits);

				//reseed escape
				curEsc = CxiPcFindEscapeBits(nodes, buffer, pos + 1, size, escBits);

				//put escaped byte
				CxiBitWriterWriteGamma(&writer, 1);
				CxiBitWriterWriteBit(&writer, 1);
				CxiBitWriterWriteBit(&writer, 0);
				CxiBitWriterWriteBitsBE(&writer, curEsc, escBits);
				CxiBitWriterWriteBitsBE(&writer, ((unsigned char) (buffer[pos] << escBits)) >> escBits, 8 - escBits);
			} else {
				//put byte
				CxiBitWriterWriteBitsBE(&writer, buffer[pos], 8);
			}
		} else if (node->distance == 0) {
			//RL
			CxiBitWriterWriteBitsBE(&writer, curEsc, escBits);
			CxiBitWriterWriteGamma(&writer, 1);
			CxiBitWriterWriteBit(&writer, 1);
			CxiBitWriterWriteBit(&writer, 1);

			unsigned int rlLen = node->length - 1; // we write length-1
			if (rlLen >= 0x80) {
				CxiBitWriterWriteGamma(&writer, ((rlLen >> 1) & 0x7F) | 0x80);
				CxiBitWriterWriteBit(&writer, rlLen & 1);
				CxiBitWriterWriteGamma(&writer, (rlLen >> 8) + 1);
			} else {
				CxiBitWriterWriteGamma(&writer, rlLen);
			}

			//find byte location in table
			unsigned char repByte = buffer[pos];
			unsigned int tableIndex = UINT_MAX;
			for (unsigned int i = 0; i < nFreqTbl; i++) {
				if (freqTbl[i] == repByte) {
					tableIndex = i;
					break;
				}
			}

			//if byte in table, write the index
			if (tableIndex < 31) {
				//write table index
				CxiBitWriterWriteGamma(&writer, tableIndex + 1);
			} else {
				//write direct byte
				CxiBitWriterWriteGamma(&writer, (buffer[pos] >> 3) | 0x20);
				CxiBitWriterWriteBitsBE(&writer, buffer[pos] & 0x7, 3);
			}
		} else {
			//LZ
			CxiBitWriterWriteBitsBE(&writer, curEsc, escBits);
			CxiBitWriterWriteGamma(&writer, node->length - 1);
			if (node->length == 2) {
				//2-byte LZ
				CxiBitWriterWriteBit(&writer, 0);
				CxiBitWriterWriteBitsBE(&writer, node->distance - 1, 8);
			} else {
				//>2-byte lZ
				unsigned int distance = node->distance - 1;
				CxiBitWriterWriteGamma(&writer, (distance >> nLzBits) + 1);
				CxiBitWriterWriteBitsBE(&writer, distance & ((1 << nLzBits) - 1), nLzBits);
			}
		}

		pos += node->length;
	}

	//termination sequence
	CxiBitWriterWriteBitsBE(&writer, curEsc, escBits);
	CxiBitWriterWriteGamma(&writer, 2);
	CxiBitWriterWriteGamma(&writer, 0xFF);

	unsigned int bitsSize;
	void *bits = CxiBitWriterGetBytes(&writer, 1, 0, 1, &bitsSize);
	CxiBitWriterFree(&writer);

	unsigned int complen = 4 + 4 + nFreqTbl + bitsSize;
	unsigned char *comp = (unsigned char *) calloc(complen, 1);
	comp[0] = 0x60;
	comp[1] = (size >>  0) & 0xFF;
	comp[2] = (size >>  8) & 0xFF;
	comp[3] = (size >> 16) & 0xFF;
	comp[4] = nFreqTbl;
	comp[5] = initEsc;
	comp[6] = nLzExtra;
	comp[7] = escBits;
	memcpy(comp + 8, freqTbl, nFreqTbl);
	memcpy(comp + 8 + nFreqTbl, bits, bitsSize);

	*pOutSize = complen;
	return comp;
}

unsigned char *CxCompressPuCrunch(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize) {
	//get max LZ extra bits
	unsigned int maxWindow = 0xFE00, maxLzExtra = 0;
	while (maxWindow < size && maxLzExtra < 8) {
		maxWindow <<= 1;
		maxLzExtra++;
	}

	//we will vary this from its initial value downwards to find an optimal setting.
	unsigned int nLzExtra = maxLzExtra;

	CxiLzNode *nodes  = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));  // buffer for LZ matches
	uint16_t  *rlLens = (uint16_t  *) calloc(size, sizeof(uint16_t ));  // buffer for RL matches
	CxiPcExploreLzRl(buffer, size, maxWindow, nodes, rlLens);

	//we will try this from 0-8 to find the best value. We do this after exploration.
	unsigned char freqTbl[32] = { 0 };
	unsigned int nFreqTbl = 0;

	unsigned int bestCompSize = UINT_MAX;
	unsigned char *bestComp = NULL;
	for (unsigned int escBits = 0; escBits <= 2; escBits++) {
		memset(freqTbl, 0, sizeof(freqTbl));

		//run 2-pass: one graph optimize to build the RL table, then one more optimize.
		{
			CxiLzNode *nodesCopy = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
			memcpy(nodesCopy, nodes, size * sizeof(CxiLzNode));

			CxiPcGraphOptimizeGreedy(buffer, size, nodesCopy, rlLens);
			nFreqTbl = CxiPcCreateRlTable(buffer, size, nodesCopy, freqTbl);

			free(nodesCopy);
		}

		CxiLzNode *nodesCopy = (CxiLzNode *) calloc(size, sizeof(CxiLzNode));
		for (unsigned int j = 0; j < 2; j++) {
			memcpy(nodesCopy, nodes, size * sizeof(CxiLzNode));

			//run backwards pass for node collapse
			CxiPcGraphOptimize(buffer, size, nodesCopy, rlLens, escBits, nLzExtra, freqTbl, nFreqTbl);
			nFreqTbl = CxiPcCreateRlTable(buffer, size, nodesCopy, freqTbl);
		}

		//compress
		unsigned int compSize;
		unsigned char *comp = CxiPcWriteCompression(buffer, size, nodesCopy, escBits, nLzExtra, freqTbl, nFreqTbl, &compSize);
		free(nodesCopy);

		if (compSize < bestCompSize) {
			//new best
			free(bestComp);
			bestCompSize = compSize;
			bestComp = comp;
		}
	}

	free(rlLens);
	free(nodes);

	*compressedSize = bestCompSize;
	return bestComp;
}



// ----- Decompression routines

static uint32_t CxiBitReaderReadGamma(CxiBitReader *reader) {
	unsigned int len = 0;
	for (unsigned int i = 0; i < 7; i++) {
		int b = CxiBitReaderReadBit(reader);
		if (!b) break;

		len++;
	}

	uint32_t x = 1;
	for (unsigned int i = 0; i < len; i++) {
		x <<= 1;
		x |= (uint32_t) CxiBitReaderReadBit(reader);
	}
	return x;
}


unsigned char *CxDecompressPuCrunch(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize) {
	uint32_t header = *(const uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);

	const unsigned char *info = buffer + 4;
	const unsigned char *freqTable = info + 4;
	unsigned int freqTblSize = info[0];  // size of RL table
	unsigned char esc        = info[1];  // initial escape sequence
	unsigned int nLzExtra    = info[2];  // # extra bits LZ
	unsigned int escBits     = info[3];  // # ecsape bits

	const unsigned char *bitStmStart = info + 4 + freqTblSize;

	CxiBitReader reader;
	CxiBitReaderInit(&reader, bitStmStart, buffer + size, 1, 0);

	unsigned int nLzBits = 8 + nLzExtra;
	unsigned int outpos = 0;
	while (1) {
		//feed command
		unsigned char initBits = CxiBitReaderReadBits(&reader, escBits);

		if (initBits != esc) {
			//byte literal, assemble the remaining bits from the stream
			unsigned char rest = CxiBitReaderReadBits(&reader, 8 - escBits);
			unsigned char b = (initBits << (8 - escBits)) | rest;

			out[outpos++] = b;
		} else {
			//escape sequence processing
			uint32_t x = CxiBitReaderReadGamma(&reader) + 1;

			if (x > 2) {
				//LZ copy
				unsigned int hi = CxiBitReaderReadGamma(&reader) - 1;
				if (hi == 0xFE) break;

				unsigned int offset = (hi << nLzBits) | CxiBitReaderReadBits(&reader, nLzBits);
				offset++;

				for (unsigned int i = 0; i < x; i++) {
					out[outpos] = out[outpos - offset];
					outpos++;
				}
			} else if (!CxiBitReaderReadBit(&reader)) {
				//2-byte LZ
				unsigned int offset = CxiBitReaderReadBits(&reader, 8) + 1;

				out[outpos + 0] = out[outpos + 0 - offset];
				out[outpos + 1] = out[outpos + 1 - offset];
				outpos += 2;
			} else if (!CxiBitReaderReadBit(&reader)) {
				//escaped literal
				unsigned char newEsc = CxiBitReaderReadBits(&reader, escBits);

				unsigned char b = (esc << (8 - escBits)) | CxiBitReaderReadBits(&reader, 8 - escBits);
				out[outpos++] = b;

				//put new escape
				esc = newEsc;
			} else {
				//RLE
				unsigned int rlLen = CxiBitReaderReadGamma(&reader);

				if (rlLen >= 0x80) {
					//feed in one bit and out the msb
					rlLen = (rlLen << 1) + CxiBitReaderReadBit(&reader);
					rlLen &= 0xFF;

					//high bits of length
					rlLen |= (CxiBitReaderReadGamma(&reader) - 1) << 8;
				}
				rlLen++;

				//get literal
				unsigned int bRepeat = CxiBitReaderReadGamma(&reader);
				if (bRepeat < 32) {
					bRepeat = freqTable[bRepeat - 1];
				} else {
					bRepeat = (bRepeat << 3) | CxiBitReaderReadBits(&reader, 3);
					bRepeat &= 0xFF;
				}

				//put repeat
				for (unsigned int i = 0; i < rlLen; i++) {
					out[outpos++] = bRepeat;
				}
			}
		}
	}

	*uncompressedSize = uncompSize;
	return out;
}



// ----- Validation

int CxIsCompressedPuCrunch(const unsigned char *buffer, unsigned int size) {
	//header check
	if (size < 8 || buffer[0] != 0x60) return 0;

	uint32_t header = *(const uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	
	const unsigned char *info      = buffer + 4;
	const unsigned char *freqTable = buffer + 8;
	unsigned int  freqTblSize = info[0];  // size of RL table
	unsigned char esc         = info[1];  // initial escape sequence
	unsigned int  nLzExtra    = info[2];  // # extra bits LZ
	unsigned int  escBits     = info[3];  // # ecsape bits
	
	//limits of header fields:
	if (freqTblSize > (size - 8))                                        return 0;
	if ((freqTblSize & 3) || (freqTblSize > 0x20) || (freqTblSize == 0)) return 0;
	if (escBits > 8 || nLzExtra > 24)                                    return 0;

	CxiBitReader reader;
	const unsigned char *bitStmStart = info + 4 + freqTblSize;
	CxiBitReaderInit(&reader, bitStmStart, buffer + size, 1, 0);

	unsigned int nLzBits = nLzExtra + 8;
	unsigned int outpos = 0;
	while (1) {
		//feed command
		unsigned char initBits = CxiBitReaderReadBits(&reader, escBits);

		if (initBits != esc) {
			//byte literal, assemble the remaining bits from the stream
			if (outpos++ >= uncompSize) return 0;

			CxiBitReaderReadBits(&reader, 8 - escBits);
		} else {
			//escape sequence processing
			uint32_t x = CxiBitReaderReadGamma(&reader) + 1;

			if (x > 2) {
				//LZ copy
				unsigned int hi = CxiBitReaderReadGamma(&reader) - 1;
				if (hi == 0xFE) break;

				unsigned int offset = (hi << nLzBits) | CxiBitReaderReadBits(&reader, nLzBits);
				offset++;

				if (offset > outpos)           return 0;
				if (x > (uncompSize - outpos)) return 0;
				outpos += x;
			} else if (!CxiBitReaderReadBit(&reader)) {
				//2-byte LZ
				unsigned int offset = CxiBitReaderReadBits(&reader, 8) + 1;
				if (offset > outpos)           return 0;
				if (2 > (uncompSize - outpos)) return 0;

				outpos += 2;
			} else if (!CxiBitReaderReadBit(&reader)) {
				//escaped literal
				if (outpos++ >= uncompSize) return 0;

				//put new escape
				esc = CxiBitReaderReadBits(&reader, escBits);
				CxiBitReaderReadBits(&reader, 8 - escBits);
			} else {
				//RLE
				unsigned int rlLen = CxiBitReaderReadGamma(&reader);

				if (rlLen >= 0x80) {
					//feed in one bit and out the msb
					rlLen = (rlLen << 1) + CxiBitReaderReadBit(&reader);
					rlLen &= 0xFF;

					//high bits of length
					rlLen |= (CxiBitReaderReadGamma(&reader) - 1) << 8;
				}
				rlLen++;

				//feed literal
				unsigned int tableIndex = CxiBitReaderReadGamma(&reader) - 1;
				if (tableIndex >= 31) {
					CxiBitReaderReadBits(&reader, 3);
				} else {
					//bounds check the table access
					if ((8 + tableIndex) >= size) return 0;
				}

				//check copy length
				if (rlLen > (uncompSize - outpos)) return 0;
				outpos += rlLen;
			}
		}
	}

	//valid? check the stream error state
	return !reader.error;
}
