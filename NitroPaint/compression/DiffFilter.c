#include "CxPrivate.h"


// ----- Filtering routines

unsigned char *CxFilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *filteredSize) {
	char *out = (char *) calloc(size + 4, 1);
	*filteredSize = size + 4;

	*(uint32_t *) out = 0x80 | (size << 8);

	unsigned char last = 0;
	for (unsigned int i = 0; i < size; i++) {
		out[i + 4] = buffer[i] - last;
		last = buffer[i];
	}

	return out;
}

unsigned char *CxFilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *filteredSize) {
	unsigned int outSize = ((size + 1) & ~1) + 4; // round up to multiple of 2
	unsigned char *out = (unsigned char *) calloc(outSize, 1);
	*filteredSize = outSize;

	*(uint32_t *) out = 0x81 | (size << 8);

	uint16_t last = 0;
	for (unsigned int i = 0; i < size; i += 2) {
		uint16_t hw = buffer[i];
		if ((i + 1) < size) hw |= buffer[i + 1] << 8;

		*(uint16_t *) (out + 4 + i) = hw - last;
		last = hw;
	}

	return out;
}



// ----- Unfiltering routines

unsigned char *CxUnfilterDiff8(const unsigned char *buffer, unsigned int size, unsigned int *unfilteredSize) {
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	unsigned int dstOfs = 0, srcOfs = 4;;
	*unfilteredSize = uncompSize;

	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);

	unsigned char last = 0;
	while (dstOfs < uncompSize) {
		last += buffer[srcOfs++];
		out[dstOfs++] = last;
	}

	return out;
}

unsigned char *CxUnfilterDiff16(const unsigned char *buffer, unsigned int size, unsigned int *unfilteredSize) {
	uint32_t header = *(uint32_t *) buffer;
	unsigned int uncompSize = header >> 8;
	unsigned int dstOfs = 0, srcOfs = 4;;
	*unfilteredSize = uncompSize;

	unsigned char *out = (unsigned char *) calloc(uncompSize, 1);
	uint16_t last = 0;
	while (dstOfs < uncompSize) {
		last += *(uint16_t *) (buffer + srcOfs);
		srcOfs += 2;
		*(uint16_t *) (out + dstOfs) = last;
		dstOfs += 2;
	}

	return out;
}



// ----- Validation routines

int CxIsFilteredDiff8(const unsigned char *buffer, unsigned int size) {
	if (size < 4 || buffer[0] != 0x80) return 0;

	uint32_t head = *(uint32_t *) buffer;
	unsigned int uncompSize = head >> 8;

	if (uncompSize > size) return 0;

	//round up to a multiple of 4
	if (((size - 4 + 3) & ~3) != ((uncompSize + 3) & ~3)) return 0;
	return 1;
}

int CxIsFilteredDiff16(const unsigned char *buffer, unsigned int size) {
	if (size < 4 || buffer[0] != 0x81) return 0;
	if (size & 1) return 0;

	uint32_t head = *(uint32_t *) buffer;
	unsigned int uncompSize = head >> 8;

	if (uncompSize > size) return 0;

	//round up to a multiple of 4
	if (((size - 4 + 3) & ~3) != ((uncompSize + 3) & ~3)) return 0;
	return 1;
}
