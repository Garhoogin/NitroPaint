#pragma once

/*
* char * lz77Decompress(char * buffer, int size, int * uncompressedSize)
*
* Returns a pointer to the uncompressed data.
*
* Returns:
*  NULL if the call failed
*  the pointer to the uncompressed data if the call was successful
*/
char *lz77decompress(char *buffer, int size, int *uncompressedSize);


/*
* char * lz77Compress(char * buffer, int size, int * compressedSize)
*
* Returns a pointer to a buffer containing the compressed data.
*
* Returns:
*  NULL if the call failed
*  the pointer to the compressed data if the call was successful
*/
char *lz77compress(char *buffer, int size, int *compressedSize);


/*
* int lz77IsCompressed(char * buffer, unsigned size)
*
* Determines whether the input buffer contains valid lz77 compressed data.
*
* Returns:
*  0 if the buffer does not contain valid lz77 compressed data
*  1 if the buffer does contain valid lz77 compressed data
*/
int lz77IsCompressed(char *buffer, unsigned size);