#pragma once

#include <stdint.h>
#include <stdlib.h>

#ifdef _MSC_VER
#define inline __inline
#endif

#ifdef _MSC_VER
#ifdef _DEBUG
#define CX_ASSERT(x)  if (!(x))__debugbreak()
#else
#define CX_ASSERT(x)  __assume(x)
#endif
#else
#define CX_ASSERT(x)
#endif

void *CxiShrink(void *block, unsigned int to);

unsigned int CxiIlog2(unsigned int x);


// ----- Bit routines

uint32_t CxiBitReverse32(uint32_t x);

unsigned char CxiBitReverse8(unsigned char x);

uint32_t CxiByteSwap(uint32_t x);



typedef struct CxiBitReader_ {
	const unsigned char *start;
	const unsigned char *end;
	const unsigned char *pos;
	uint32_t current;
	uint8_t nBitsBuffered;
	uint8_t error;
	uint8_t beBits  : 1;  // big-endian bit order
	uint8_t beBytes : 1;  // big-endian byte order (requires full word buffer)
	uint32_t nBitsRead;
} CxiBitReader;

typedef struct CxiBitWriter_ {
	uint32_t *bits;
	unsigned int nWords;
	unsigned int nBitsInLastWord;
	unsigned int nWordsAlloc;
	unsigned int length;
} CxiBitWriter;



// ----- Bit reader

void CxiBitReaderInit(
	CxiBitReader        *reader,
	const unsigned char *pos,
	const unsigned char *end,
	int                  beBits,
	int                  beBytes
);

uint32_t CxiBitReaderReadBit(
	CxiBitReader *reader
);

uint32_t CxiBitReaderReadBits(
	CxiBitReader *reader,
	unsigned int  nBits
);



// ----- Bit writer

void CxiBitWriterInit(
	CxiBitWriter *writer
);

void CxiBitWriterFree(
	CxiBitWriter *writer
);

void CxiBitWriterWriteBit(
	CxiBitWriter *writer,
	int           bit
);

void *CxiBitWriterGetBytes(
	CxiBitWriter *writer,
	int           wordAlign,
	int           beBytes,
	int           beBits,
	unsigned int *size
);

void CxiBitWriterWriteBits(
	CxiBitWriter *writer,
	uint32_t      bits,
	unsigned int  nBits
);

void CxiBitWriterWriteBitsBE(
	CxiBitWriter *writer,
	uint32_t      bits,
	unsigned int  nBits
);
