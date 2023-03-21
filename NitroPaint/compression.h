#pragma once

#define COMPRESSION_NONE             0
#define COMPRESSION_LZ77             1
#define COMPRESSION_LZ11             2
#define COMPRESSION_LZ11_COMP_HEADER 3
#define COMPRESSION_HUFFMAN_4        4
#define COMPRESSION_HUFFMAN_8        5
#define COMPRESSION_LZ77_HEADER      6

//----- LZ77 functions

/******************************************************************************\
*
* Decompresses LZ77-compressed data and returns a pointer to the decompressed
* buffer allocated with malloc.
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
char *lz77decompress(char *buffer, int size, unsigned int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with LZ77 and returns a pointer to an allocated buffer
* holding the compressed data.
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
char *lz77compress(char *buffer, int size, unsigned int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid LZ77 compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid LZ77 compressed data
*	1 if the buffer does contain valid LZ77 compressed data
*
\******************************************************************************/
int lz77IsCompressed(char *buffer, unsigned int size);

//----- LZ11 functions

/******************************************************************************\
*
* Decompresses LZ11-compressed data and returns a pointer to the decompressed
* buffer allocated with malloc.
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
char *lz11decompress(char *buffer, int size, int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with LZ11 and returns a pointer to an allocated buffer
* holding the compressed data.
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
char *lz11compress(char *buffer, int size, int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid LZ11 compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid LZ11 compressed data
*	1 if the buffer does contain valid LZ11 compressed data
*
\******************************************************************************/
int lz11IsCompressed(char *buffer, unsigned size);

//----- LZ11 header functions

/******************************************************************************\
*
* Decompresses LZ11 header compressed data and returns a pointer to the
* decompressed buffer allocated with malloc.
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
char *lz11CompHeaderDecompress(char *buffer, int size, int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with LZ11 header compression and returns a pointer to an
* allocated buffer holding the compressed data.
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
char *lz11CompHeaderCompress(char *buffer, int size, int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid LZ11 header compression
* compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid LZ11 header compressed data
*	1 if the buffer does contain valid LZ11 header compressed data
*
\******************************************************************************/
int lz11CompHeaderIsValid(char *buffer, unsigned size);

//----- Huffman functions

/******************************************************************************\
*
* Decompresses Huffman-compressed data and returns a pointer to the decompressed
* buffer allocated with malloc.
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
char *huffmanDecompress(unsigned char *buffer, int size, int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with Huffman and returns a pointer to an allocated buffer
* holding the compressed data.
*
* Parameters:
*	buffer					the buffer to compress
*	size					size of the buffer
*	compressedSize			pointer that receives the compressed size
*	nBits					Symbol size in bits; either 4 or 8
*
* Returns:
*	A pointer to the compressed buffer on success, or NULL on failure.
*
\******************************************************************************/
char *huffmanCompress(unsigned char *buffer, int size, int *compressedSize, int nBits);
char *huffman4Compress(unsigned char *buffer, int size, int *compressedSize);
char *huffman8Compress(unsigned char *buffer, int size, int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid Huffman compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid LZ77 compressed data
*	1 if the buffer does contain valid LZ77 compressed data
*
\******************************************************************************/
int huffmanIsCompressed(unsigned char *buffer, unsigned size);
int huffman4IsCompressed(unsigned char *buffer, unsigned size);
int huffman8IsCompressed(unsigned char *buffer, unsigned size);

//----- LZ77 header functions

/******************************************************************************\
*
* Decompresses LZ77-compressed data and returns a pointer to the decompressed
* buffer allocated with malloc.
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
char *lz77HeaderDecompress(char *buffer, int size, int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with LZ77 and returns a pointer to an allocated buffer
* holding the compressed data.
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
char *lz77HeaderCompress(char *buffer, int size, int *compressedSize);


/******************************************************************************\
*
* Determines whether the input buffer contains valid LZ77 compressed data.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*
* Returns:
*	0 if the buffer does not contain valid LZ77 compressed data
*	1 if the buffer does contain valid LZ77 compressed data
*
\******************************************************************************/
int lz77HeaderIsCompressed(unsigned char *buffer, unsigned size);

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
int getCompressionType(char *buffer, int size);


/******************************************************************************\
*
* Decompresses a buffer, automatically detecting the type of compression.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*	uncompressedSize		pointer receiving the uncompressed size
*
* Returns:
*	A buffer containing the decompressed data.
*
\******************************************************************************/
char *decompress(char *buffer, int size, int *uncompressedSize);


/******************************************************************************\
*
* Compresses a buffer with the compression algorithm of choice.
*
* Parameters:
*	buffer					the buffer to check
*	size					the size of the buffer
*	compression				the type of compression to use
*	compressedSize			pointer receiving the uncompressed size
*
* Returns:
*	A buffer containing the compressed data.
*
\******************************************************************************/
char *compress(char *buffer, int size, int compression, int *compressedSize);
