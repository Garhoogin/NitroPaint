#include "CxPrivate.h"

// ----- bit routines

uint32_t CxiBitReverse32(uint32_t x) {
	x = ((x & 0xFFFF0000) >> 16) | ((x & ~0xFFFF0000) << 16);
	x = ((x & 0xFF00FF00) >>  8) | ((x & ~0xFF00FF00) <<  8);
	x = ((x & 0xF0F0F0F0) >>  4) | ((x & ~0xF0F0F0F0) <<  4);
	x = ((x & 0xCCCCCCCC) >>  2) | ((x & ~0xCCCCCCCC) <<  2);
	x = ((x & 0xAAAAAAAA) >>  1) | ((x & ~0xAAAAAAAA) <<  1);
	return x;
}

unsigned char CxiBitReverse8(unsigned char x) {
	return CxiBitReverse32(x << 24);
}

uint32_t CxiByteSwap(uint32_t x) {
	return ((x & 0xFF000000) >> 24)
		 | ((x & 0x00FF0000) >>  8)
		 | ((x & 0x0000FF00) <<  8)
		 | ((x & 0x000000FF) << 24);
}


// ----- Misc routines

void *CxiShrink(void *block, unsigned int to) {
	void *newblock = realloc(block, to);
	if (newblock == NULL) {
		//alloc fail, return old block
		return block;
	}
	return newblock;
}

unsigned int CxiIlog2(unsigned int x) {
	unsigned int y = 0;
	while (x) {
		x >>= 1;
		y++;
	}
	return y - 1;
}



// ----- Bit reader routines

static void CxiBitReaderFetch(CxiBitReader *reader) {
	//when bit and byte endianness do not match, we must fetch full words. When they match,
	//we can get by with fetching one byte at a time.
	int fullWords = reader->beBits != reader->beBytes;
	unsigned int unitSize = fullWords ? 4 : 1;

	if ((reader->pos + unitSize) <= reader->end) {
		if (!fullWords) {
			//fetch byte
			reader->current = *reader->pos;
		} else {
			//fetch word
			reader->current = reader->pos[0] | (reader->pos[1] << 8) | (reader->pos[2] << 16) | (reader->pos[3] << 24);
			if (reader->beBytes) {
				reader->current = CxiByteSwap(reader->current);
			}
		}
		reader->nBitsBuffered = 8 * unitSize;
		reader->pos += unitSize;

		//in big endian bit order we internally reverse the bit buffer
		if (reader->beBits) {
			if (!fullWords) {
				reader->current = CxiBitReverse8(reader->current);
			} else {
				reader->current = CxiBitReverse32(reader->current);
			}
		}
	} else {
		//out of bounds access
		reader->error = 1;
	}
}

void CxiBitReaderInit(CxiBitReader *reader, const unsigned char *pos, const unsigned char *end, int beBits, int beBytes) {
	reader->pos = pos;
	reader->end = end;
	reader->start = pos;
	reader->beBits = beBits;
	reader->beBytes = beBytes;
	reader->nBitsBuffered = 0;
	reader->nBitsRead = 0;
	reader->current = 0;
	reader->error = 0;
}

uint32_t CxiBitReaderReadBit(CxiBitReader *reader) {
	if (reader->nBitsBuffered == 0) {
		//fetch next bits
		CxiBitReaderFetch(reader);
	}

	uint32_t current = reader->current;
	reader->current >>= 1;
	reader->nBitsBuffered--;
	reader->nBitsRead++;
	return current & 1;
}

uint32_t CxiBitReaderReadBits(CxiBitReader *reader, unsigned int nBits) {
	uint32_t string = 0, i = 0;
	for (i = 0; i < nBits; i++) {
		uint32_t bit = CxiBitReaderReadBit(reader);
		if (reader->error) return string;

		if (reader->beBits) {
			string <<= 1;
			string |= bit;
		} else {
			string |= bit << i;
		}
	}

	return string;
}



// ----- Bit writer routines

void CxiBitWriterInit(CxiBitWriter *writer) {
	writer->nWords = 0;
	writer->length = 0;
	writer->nBitsInLastWord = 32;
	writer->nWordsAlloc = 16;
	writer->bits = (uint32_t *) calloc(writer->nWordsAlloc, sizeof(uint32_t));
}

void CxiBitWriterFree(CxiBitWriter *writer) {
	free(writer->bits);
}

void CxiBitWriterWriteBit(CxiBitWriter *writer, int bit) {
	if (writer->nBitsInLastWord == 32) {
		writer->nBitsInLastWord = 0;
		writer->nWords++;
		if (writer->nWords > writer->nWordsAlloc) {
			unsigned int newAllocSize = (writer->nWordsAlloc + 2) * 3 / 2;
			writer->bits = realloc(writer->bits, newAllocSize * 4);
			writer->nWordsAlloc = newAllocSize;
		}
		writer->bits[writer->nWords - 1] = 0;
	}

	writer->bits[writer->nWords - 1] |= bit << (31 - writer->nBitsInLastWord);
	writer->nBitsInLastWord++;
	writer->length++;
}

void *CxiBitWriterGetBytes(CxiBitWriter *writer, int wordAlign, int beBytes, int beBits, unsigned int *size) {
	//allocate buffer
	unsigned int outSize = writer->nWords * 4;
	if (!wordAlign && beBytes != beBits) {
		//nBitsInLast word is 32 if last word is full, 0 if empty.
		if (writer->nBitsInLastWord <= 24) outSize--;
		if (writer->nBitsInLastWord <= 16) outSize--;
		if (writer->nBitsInLastWord <= 8) outSize--;
		if (writer->nBitsInLastWord <= 0) outSize--;
	}
	unsigned char *outbuf = (unsigned char *) calloc(outSize, 1);

	//this function handles converting byte and bit orders from the internal
	//representation. Internally, we store the bit sequence as an array of
	//words, where the first bits are inserted at the most significant bit.
	for (unsigned int i = 0; i < outSize; i++) {
		uint32_t word = writer->bits[i / 4];
		if (beBytes) word = CxiByteSwap(word);

		//if little endian bit order, swap here
		uint8_t byte = (word >> (8 * (i % 4))) & 0xFF;
		if (!beBits) byte = CxiBitReverse8(byte);
		outbuf[i] = byte;
	}

	*size = outSize;
	return outbuf;
}

void CxiBitWriterWriteBits(CxiBitWriter *writer, uint32_t bits, unsigned int nBits) {
	for (unsigned int i = 0; i < nBits; i++) CxiBitWriterWriteBit(writer, (bits >> i) & 1);
}

void CxiBitWriterWriteBitsBE(CxiBitWriter *writer, uint32_t bits, unsigned int nBits) {
	for (unsigned int i = 0; i < nBits; i++) CxiBitWriterWriteBit(writer, (bits >> (nBits - 1 - i)) & 1);
}
