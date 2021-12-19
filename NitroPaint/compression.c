#include <stdlib.h>
#include <string.h>

#include "compression.h"

int getCompressionType(char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) return COMPRESSION_LZ77;

	return COMPRESSION_NONE;
}

char *decompress(char *buffer, int size, int *uncompressedSize) {
	int type = getCompressionType(buffer, size);
	switch (type) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*uncompressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return lz77decompress(buffer, size, uncompressedSize);
	}
	return NULL;
}

char *lz77decompress(char *buffer, int size, int *uncompressedSize){
	//decompress the input buffer. 
	//input is invalid if the size is less than 4.
	if(size < 4) return NULL;
	//find the length of the decompressed buffer.
	int length = *(int *) (buffer + 1) & 0xFFFFFF;
	//create a buffer for the decompressed buffer
	char *result = (char *) malloc(length);
	if(!result) return NULL;
	*uncompressedSize = length;
	//initialize variables
	int offset = 4;
	int dstOffset = 0;
	int x = 0;
	while(1){
		unsigned char head = buffer[offset];
		offset++;
		//loop 8 times
		for(int i = 0; i < 8; i++){
			int flag = head >> 7;
			head <<= 1;
			if(!flag){
				result[dstOffset] = buffer[offset];
				dstOffset++, offset++;
				if(dstOffset == length) return result;
			} else {
				unsigned char high = buffer[offset++];
				unsigned char low = buffer[offset++];

				//length of uncompressed chunk and offset
				int offs = (((high & 0xF) << 8) | low) + 1;
				int len = (high >> 4) + 3;
				x = 1;
				for(int j = 0; j < len; j++){
					result[dstOffset] = result[dstOffset - offs];
					dstOffset++;
					if(dstOffset == length) return result;
				}
			}
		}
	}
	return result;
}

int compareMemory(char *b1, char *b2, int nMax, int nAbsoluteMax) {
	int nSame = 0;
	if (nMax > nAbsoluteMax) nMax = nAbsoluteMax;
	//count up to nMax. If all match, count 0x12-nMax bytes. The b1 just starts over.
	for (int i = 0; i < nMax; i++) {
		if (*(b1++) == *(b2++)) nSame++;
		else break;
	}
	if (nSame == nMax) {
		b1 -= nMax;
		for (int i = 0; i < nAbsoluteMax - nMax; i++) {
			if (*(b1++) == *(b2++)) nSame++;
			else break;
		}
	}
	return nSame;
}

char *lz77compress(char *buffer, int size, int *compressedSize){
	int compressedMaxSize = 4 + 9 * ((size + 7) >> 3);
	char *compressed = (char *) malloc(compressedMaxSize);
	char *compressedBase = compressed;
	*(unsigned *) compressed = size << 8;
	*compressed = 0x10;
	int nProcessedBytes = 0;
	int nSize = 4;
	compressed += 4;
	while (1) {
		//make note of where to store the head for later.
		char *headLocation = compressed;
		compressed++;
		nSize++;
		//initialize the head.
		char head = 0;

		//set when the end of the file is reached, and the result needs to be zero-padded.
		int isDone = 0;

		//repeat 8x (8 bits per byte)
		for (int i = 0; i < 8; i++) {
			head <<= 1;

			if (isDone) {
				*(compressed++) = 0;
				nSize++;
				continue;
			}

			//search backwards up to 0xFFF bytes.
			int maxSearch = 0x1000;
			if (maxSearch > nProcessedBytes) maxSearch = nProcessedBytes;

			//the biggest match, and where it was
			int biggestRun = 0, biggestRunIndex = 0;

			//begin searching backwards.
			for (int j = 2; j < maxSearch; j++) {
				//compare up to 0xF bytes, at most j bytes.
				int nCompare = 0x12;
				if (nCompare > j) nCompare = j;
				int nBytesLeft = size - nProcessedBytes;
				int nAbsoluteMaxCompare = 0x12;
				if (nAbsoluteMaxCompare > nBytesLeft) nAbsoluteMaxCompare = nBytesLeft;
				int nMatched = compareMemory(buffer - j, buffer, nCompare, nAbsoluteMaxCompare);
				if (nMatched > biggestRun) {
					if (biggestRun == 0x12) break;
					biggestRun = nMatched;
					biggestRunIndex = j;
				}
			}

			//if the biggest run is at least 3, then we use it.
			if (biggestRun >= 3) {
				head |= 1;
				nProcessedBytes += biggestRun;
				//encode the match. First byte has high nybble as length minus 3, low nybble as the high byte of the offset.
				*(compressed++) = ((biggestRun - 3) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
				*(compressed++) = (biggestRunIndex - 1) & 0xFF;
				//advance the buffer
				buffer += biggestRun;
				nSize += 2;
				if (nProcessedBytes >= size) isDone = 1;
			} else {
				*(compressed++) = *(buffer++);
				nProcessedBytes++;
				nSize++;
				if (nProcessedBytes >= size) isDone = 1;
			}
		}
		*headLocation = head;
		if (nProcessedBytes >= size) break;
	}
	*compressedSize = nSize;
	return realloc(compressedBase, nSize);

}//22999

int lz77IsCompressed(char *buffer, unsigned size) {
	if (size < 4) return 0;
	if (*buffer != 0x10 && *buffer != 0x11) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	if ((length / 144) * 17 + 4 <= size) return 1;
	return 0;
}