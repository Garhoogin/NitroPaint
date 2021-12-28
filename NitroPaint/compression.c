#include <stdlib.h>
#include <string.h>

#include "compression.h"

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

char *lz11decompress(char *buffer, int size, int *uncompressedSize){
	//decompress the input buffer. 
	if(size < 4) return NULL;

	//find the length of the decompressed buffer.
	int length = *(int *) (buffer) >> 4;

	//create a buffer for the decompressed buffer
	char *result = (char *) malloc(length);
	if(result == NULL) return NULL;
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
				unsigned char low2, low3;
				int mode = high >> 4;

				int len = 0, offs = 0;
				switch (mode) {
					case 0:
						low2 = buffer[offset++];
						len = ((high << 4) | (low >> 4)) + 0x11; //8-bit length +0x11
						offs = (((low & 0xF) << 8) | low2) + 1; //12-bit offset
						break;
					case 1:
						low2 = buffer[offset++];
						low3 = buffer[offset++];
						len = (((high & 0xF) << 12) | (low << 4) | (low2 >> 4)) + 0x111; //16-bit length +0x111
						offs = (((low2 & 0xF) << 8) | low3) + 1; //12-bit offset
						break;
					default:
						len = (high >> 4) + 1; //4-bit length +0x1 (but >= 3)
						offs = (((high & 0xF) << 8) | low) + 1; //12-bit offset
						break;
				}

				//write back
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

char *lz11compress(char *buffer, int size, int *compressedSize) {
	int compressedMaxSize = 4 + 9 * ((size + 7) >> 3);
	char *compressed = (char *) malloc(compressedMaxSize);
	char *compressedBase = compressed;
	*(unsigned *) compressed = size << 8;
	*compressed = 0x11;
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
				int nCompare = 0xFFFF + 0x111; //max run length
				if (nCompare > j) nCompare = j;
				int nBytesLeft = size - nProcessedBytes;
				int nAbsoluteMaxCompare = 0xFFFF + 0x111; //max run length
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
				//encode the match.

				if (biggestRun <= 0x10) {
					//First byte has high nybble as length minus 1, low nybble as the high byte of the offset.
					*(compressed++) = ((biggestRun - 1) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 2;
				} else if (biggestRun <= 0xFF + 0x11) {
					//First byte has the high 4 bits of run length minus 0x11
					//Second byte has the low 4 bits of the run length minus 0x11 in the high nybble
					*(compressed++) = (biggestRun - 0x11) >> 4;
					*(compressed++) = (((biggestRun - 0x11) & 0xF) << 4) | ((biggestRunIndex - 1) >> 8);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;
					nSize += 3;
				} else if (biggestRun <= 0xFFFF + 0x111) {
					//First byte is 0x10 ORed with the high 4 bits of run length minus 0x111
					*(compressed++) = 0x10 | (((biggestRun - 0x111) >> 12) & 0xF);
					*(compressed++) = ((biggestRun - 0x111) >> 4) & 0xFF;
					*(compressed++) = (((biggestRun - 0x111) & 0xF) << 4) | (((biggestRunIndex - 1) >> 8) & 0xF);
					*(compressed++) = (biggestRunIndex - 1) & 0xFF;

					nSize += 4;
				}
				//advance the buffer
				buffer += biggestRun;
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
}

int lz77IsCompressed(char *buffer, unsigned size) {
	if (size < 4) return 0;
	if (*buffer != 0x10) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	if ((length / 144) * 17 + 4 <= size) return 1;
	return 0;
}

int lz11IsCompressed(char *buffer, unsigned size) {
	if (size < 4) return 0;
	if (*buffer != 0x11) return 0;
	unsigned length = (*(unsigned *) buffer) >> 8;
	if (size <= 7 + length * 9 / 8) return 1;
	return 0;
}

int lz11CompHeaderIsValid(char *buffer, unsigned size) {
	if (size < 0x18) return 0;
	unsigned magic = *(unsigned *) buffer;
	if (magic != 'COMP' && magic != 'PMOC') return 0;
	unsigned payloadLength = *(unsigned *) (buffer + 0xC);
	if (payloadLength != *(unsigned *) (buffer + 0x10)) return 0;
	if (payloadLength + 0x14 != size) return 0;
	if (!lz11IsCompressed(buffer + 0x14, size - 0x14)) return 0;

	int uncompLength = *(unsigned *) (buffer + 0x14) >> 8;
	if (uncompLength != *(unsigned *) (buffer + 4)) return 0;
	return 1;
}

char *lz11CompHeaderDecompress(char *buffer, int size, int *uncompressedSize) {
	if (size < 0x18) return NULL;
	return lz11decompress(buffer + 0x14, size - 0x14, uncompressedSize);
}

char *lz11CompHeaderCompress(char *buffer, int size, int *compressedSize) {
	char header[] = { 'P', 'M', 'O', 'C',  0, 0, 0, 0,  1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 };
	*(unsigned *) (header + 4) = size;

	int payloadSize;
	char *lz11 = lz11compress(buffer, size, &payloadSize);
	*(unsigned *) (header + 0xC) = payloadSize;
	*(unsigned *) (header + 0x10) = payloadSize;

	char *finished = malloc(sizeof(header) + payloadSize);
	memcpy(finished, header, sizeof(header));
	memcpy(finished + sizeof(header), lz11, payloadSize);
	free(lz11);

	*compressedSize = payloadSize + sizeof(header);
	return finished;
}

int getCompressionType(char *buffer, int size) {
	if (lz77IsCompressed(buffer, size)) return COMPRESSION_LZ77;
	if (lz11IsCompressed(buffer, size)) return COMPRESSION_LZ11;
	if (lz11CompHeaderIsValid(buffer, size)) return COMPRESSION_LZ11_COMP_HEADER;

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
		case COMPRESSION_LZ11:
			return lz11decompress(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return lz11CompHeaderDecompress(buffer, size, uncompressedSize);
	}
	return NULL;
}

char *compress(char *buffer, int size, int compression, int *compressedSize) {
	switch (compression) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*compressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return lz77compress(buffer, size, compressedSize);
		case COMPRESSION_LZ11:
			return lz11compress(buffer, size, compressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return lz11CompHeaderCompress(buffer, size, compressedSize);
	}
	return NULL;
}
