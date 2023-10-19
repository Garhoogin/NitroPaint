#pragma once

#define COMPRESSION_NONE             0
#define COMPRESSION_LZ77             1
#define COMPRESSION_LZ11             2
#define COMPRESSION_LZ11_COMP_HEADER 3
#define COMPRESSION_HUFFMAN_4        4
#define COMPRESSION_HUFFMAN_8        5
#define COMPRESSION_RLE              6
#define COMPRESSION_DIFF8            7
#define COMPRESSION_DIFF16           8
#define COMPRESSION_LZ77_HEADER      9
#define COMPRESSION_MVDK             10

//----- LZ77 functions

/******************************************************************************\
*
* Decompresses compressed data and returns a pointer to the decompressed buffer
* allocated with malloc.
*
* Parameters:
*	buffer					the compressed buffer
*	size					size of the compressed buffer
*	uncompressedSize		pointer that receives uncompressed size
*
* Returns:
*	A pointer to the decompressed data on success, or NULL on failure.
* 
\******************************************************************************/
unsigned char *CxDecompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressLZX(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressLZXComp(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressHuffman(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressRL(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxUnfilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxUnfilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressLZHeader(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);
unsigned char *CxDecompress(const unsigned char *buffer, unsigned int size, unsigned int *uncompressedSize);


/******************************************************************************\
*
* Advance a byte steam beyond a compressed segment.
*
* Parameters:
*	buffer					the input buffer
*	size					size of the buffer
*
* Returns:
*	A pointer to the end of the compressed segment, or NULL if an error occurs.
*
\******************************************************************************/
unsigned char *CxAdvanceLZX(const unsigned char *buffer, unsigned int size);


/******************************************************************************\
*
* Compresses a buffer and returns a pointer to an allocated buffer holding the
* compressed data.
*
* Parameters:
*	buffer					the buffer to compress
*	size					size of the buffer
*	compressedSize			pointer that receives the compressed size
*
* Returns:
*	A pointer to the compressed buffer on success, or NULL on failure.
*
\******************************************************************************/
unsigned char *CxCompressLZ(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressLZX(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressLZXComp(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressHuffman(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize, int nBits);
unsigned char *CxCompressHuffman4(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressHuffman8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressRL(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxFilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxFilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressLZHeader(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompressMvDK(const unsigned char *buffer, unsigned int size, unsigned int *compressedSize);
unsigned char *CxCompress(const unsigned char *buffer, unsigned int size, int compression, unsigned int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid compressed data
*	1 if the buffer does contain valid compressed data
*
\******************************************************************************/
int CxIsCompressedLZ(const unsigned char *buffer, unsigned int size);
int CxIsCompressedLZX(const unsigned char *buffer, unsigned int size);
int CxIsCompressedLZXComp(const unsigned char *buffer, unsigned int size);
int CxIsCompressedHuffman(const unsigned char *buffer, unsigned int size);
int CxIsCompressedHuffman4(const unsigned char *buffer, unsigned int size);
int CxIsCompressedHuffman8(const unsigned char *buffer, unsigned int size);
int CxIsCompressedRL(const unsigned char *buffer, unsigned int size);
int CxIsFilteredDiff8(const unsigned char *buffer, unsigned int size);
int CxIsFilteredDiff16(const unsigned char *buffer, unsigned int size);
int CxIsFilteredLZHeader(const unsigned char *buffer, unsigned int size);
int CxIsCompressedMvDK(const unsigned char *buffer, unsigned int size);

//----- Common functions

/******************************************************************************\
*
* Gets the type of compression on the data in a buffer.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	The compression type used, or COMPRESSION_NONE if none were identified.
*
\******************************************************************************/
int CxGetCompressionType(const unsigned char *buffer, unsigned int size);
