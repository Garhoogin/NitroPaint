#include <stdio.h>
#include <stdint.h>

int g2dIsValid(char *buffer, unsigned int size) {
	if (size < 0x10) return 0;
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	if (endianness != 0xFFFE && endianness != 0xFEFF && endianness != 0) return 0;

	int isOld = (endianness == 0);

	uint32_t fileSize = *(uint32_t *) (buffer + 8);
	if (fileSize != size && !(fileSize + 8 == size && isOld)) { //old G2D Runtime subtracts 8
		fileSize = (fileSize + 3) & ~3; 
		if (fileSize != size) return 0;
	}
	uint16_t headerSize = *(uint16_t *) (buffer + 0xC);
	if (headerSize < 0x10) return 0;
	int nSections = *(uint16_t *) (buffer + 0xE);
	

	//check sections
	unsigned int offset = headerSize;
	for (int i = 0; i < nSections; i++) {
		if (offset + 8 > size) return 0;

		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		offset += size;
		if (isOld) offset += 8;
	}

	return 1;
}

int g2dIsOld(char *buffer, unsigned int size) {
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	return endianness == 0;
}

int g2dGetNumberOfSections(char *buffer, unsigned int size) {
	return *(uint16_t *) (buffer + 0xE);
}

char *g2dGetSectionByIndex(char *buffer, unsigned int size, int index) {
	if (index >= g2dGetNumberOfSections(buffer, size)) return NULL;

	uint16_t endianness = *(uint16_t *) (buffer + 4);
	int isOld = (endianness == 0);

	uint32_t offset = *(uint16_t *) (buffer + 0xC);
	for (int i = 0; i <= index; i++) {
		if (offset + 8 > size) return NULL;
		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		if (isOld) size += 8;

		if (i == index) return buffer + offset;
		offset += size;
	}
	return NULL;
}

char *g2dGetSectionByMagic(char *buffer, unsigned int size, unsigned int sectionMagic) {
	int nSections = g2dGetNumberOfSections(buffer, size);
	uint32_t offset = *(uint16_t *) (buffer + 0xC);
	uint16_t endianness = *(uint16_t *) (buffer + 4);
	int isOld = (endianness == 0);

	for (int i = 0; i <= nSections; i++) {
		if (offset + 8 > size) return NULL;
		uint32_t magic = *(uint32_t *) (buffer + offset);
		uint32_t size = *(uint32_t *) (buffer + offset + 4);
		if (isOld) size += 8;

		if (magic == sectionMagic) return buffer + offset;
		offset += size;
	}
	return NULL;
}
