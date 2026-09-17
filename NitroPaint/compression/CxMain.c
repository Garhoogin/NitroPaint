#include "compression.h"



// ----- Generic Routines

int CxGetCompressionType(const unsigned char *buffer, unsigned int size) {
	if (CxIsFilteredLZHeader   (buffer, size)) return COMPRESSION_LZ77_HEADER;
	if (CxIsCompressedLZ       (buffer, size)) return COMPRESSION_LZ77;
	if (CxIsCompressedLZX      (buffer, size)) return COMPRESSION_LZ11;
	if (CxIsCompressedLZXComp  (buffer, size)) return COMPRESSION_LZ11_COMP_HEADER;
	if (CxIsCompressedRL       (buffer, size)) return COMPRESSION_RLE;
	if (CxIsCompressedHuffman4 (buffer, size)) return COMPRESSION_HUFFMAN_4;
	if (CxIsCompressedHuffman8 (buffer, size)) return COMPRESSION_HUFFMAN_8;
	if (CxIsFilteredDiff8      (buffer, size)) return COMPRESSION_DIFF8;
	if (CxIsFilteredDiff16     (buffer, size)) return COMPRESSION_DIFF16;
	if (CxIsCompressedPuCrunch (buffer, size)) return COMPRESSION_PUCRUNCH;
	if (CxIsCompressedMvDK     (buffer, size)) return COMPRESSION_MVDK;
	if (CxIsCompressedVlx      (buffer, size)) return COMPRESSION_VLX;
	if (CxIsCompressedAsh      (buffer, size)) return COMPRESSION_ASH;

	return COMPRESSION_NONE;
}

int CxIsCompressed(const unsigned char *buffer, unsigned int size, int type) {
	switch (type) {
		case COMPRESSION_NONE             : return 1;
		case COMPRESSION_LZ77             : return CxIsCompressedLZ(buffer, size);
		case COMPRESSION_LZ77_HEADER      : return CxIsFilteredLZHeader(buffer, size);
		case COMPRESSION_LZ11             : return CxIsCompressedLZX(buffer, size);
		case COMPRESSION_RLE              : return CxIsCompressedRL(buffer, size);
		case COMPRESSION_HUFFMAN_4        : return CxIsCompressedHuffman4(buffer, size);
		case COMPRESSION_HUFFMAN_8        : return CxIsCompressedHuffman8(buffer, size);
		case COMPRESSION_DIFF8            : return CxIsFilteredDiff8(buffer, size);
		case COMPRESSION_DIFF16           : return CxIsFilteredDiff16(buffer, size);
		case COMPRESSION_ASH              : return CxIsCompressedAsh(buffer, size);
		case COMPRESSION_MVDK             : return CxIsCompressedMvDK(buffer, size);
		case COMPRESSION_VLX              : return CxIsCompressedVlx(buffer, size);
		case COMPRESSION_LZ11_COMP_HEADER : return CxIsCompressedLZXComp(buffer, size);
		case COMPRESSION_PUCRUNCH         : return CxIsCompressedPuCrunch(buffer, size);
	}
	return 0;
}

unsigned char *CxDecompress(const unsigned char *buffer, unsigned int size, int type, unsigned int *uncompressedSize) {
	switch (type) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*uncompressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return CxDecompressLZ(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11:
			return CxDecompressLZX(buffer, size, uncompressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return CxDecompressLZXComp(buffer, size, uncompressedSize);
		case COMPRESSION_HUFFMAN_4:
		case COMPRESSION_HUFFMAN_8:
			return CxDecompressHuffman(buffer, size, uncompressedSize);
		case COMPRESSION_LZ77_HEADER:
			return CxDecompressLZHeader(buffer, size, uncompressedSize);
		case COMPRESSION_RLE:
			return CxDecompressRL(buffer, size, uncompressedSize);
		case COMPRESSION_DIFF8:
			return CxUnfilterDiff8(buffer, size, uncompressedSize);
		case COMPRESSION_DIFF16:
			return CxUnfilterDiff16(buffer, size, uncompressedSize);
		case COMPRESSION_MVDK:
			return CxDecompressMvDK(buffer, size, uncompressedSize);
		case COMPRESSION_VLX:
			return CxDecompressVlx(buffer, size, uncompressedSize);
		case COMPRESSION_ASH:
			return CxDecompressAsh(buffer, size, uncompressedSize);
		case COMPRESSION_PUCRUNCH:
			return CxDecompressPuCrunch(buffer, size, uncompressedSize);
	}
	return NULL;
}

unsigned char *CxCompress(const unsigned char *buffer, unsigned int size, int compression, unsigned int *compressedSize) {
	switch (compression) {
		case COMPRESSION_NONE:
		{
			void *copy = malloc(size);
			memcpy(copy, buffer, size);
			*compressedSize = size;
			return copy;
		}
		case COMPRESSION_LZ77:
			return CxCompressLZ(buffer, size, compressedSize);
		case COMPRESSION_LZ11:
			return CxCompressLZX(buffer, size, compressedSize);
		case COMPRESSION_LZ11_COMP_HEADER:
			return CxCompressLZXComp(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_4:
			return CxCompressHuffman4(buffer, size, compressedSize);
		case COMPRESSION_HUFFMAN_8:
			return CxCompressHuffman8(buffer, size, compressedSize);
		case COMPRESSION_LZ77_HEADER:
			return CxCompressLZHeader(buffer, size, compressedSize);
		case COMPRESSION_RLE:
			return CxCompressRL(buffer, size, compressedSize);
		case COMPRESSION_DIFF8:
			return CxFilterDiff8(buffer, size, compressedSize);
		case COMPRESSION_DIFF16:
			return CxFilterDiff16(buffer, size, compressedSize);
		case COMPRESSION_MVDK:
			return CxCompressMvDK(buffer, size, compressedSize);
		case COMPRESSION_VLX:
			return CxCompressVlx(buffer, size, compressedSize);
		case COMPRESSION_ASH:
			return CxCompressAsh(buffer, size, compressedSize);
		case COMPRESSION_PUCRUNCH:
			return CxCompressPuCrunch(buffer, size, compressedSize);
	}
	return NULL;
}
