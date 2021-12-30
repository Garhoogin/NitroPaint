#include <stdio.h>


int g2dIsValid(char *buffer, unsigned int size) {
	if (size < 0x10) return 0;
	unsigned short endianness = *(unsigned short *) (buffer + 4);
	if (endianness != 0xFFFE && endianness != 0xFEFF) return 0;
	int fileSize = *(int *) (buffer + 8);
	if (fileSize != size) return 0;
	unsigned short headerSize = *(unsigned short *) (buffer + 0xC);
	if (headerSize < 0x10) return 0;
	int nSections = *(unsigned short *) (buffer + 0xE);
	
	//check sections
	unsigned int offset = headerSize;
	for (int i = 0; i < nSections; i++) {
		if (offset + 8 > size) return 0;
		unsigned int size = *(unsigned int *) (buffer + offset + 4);
		offset += size;
	}

	return 1;
}

int g2dGetNumberOfSections(char *buffer, unsigned int size) {
	return *(unsigned short *) (buffer + 0xE);
}

char *g2dGetSectionByIndex(char *buffer, unsigned int size, int index) {
	if (index >= g2dGetNumberOfSections(buffer, size)) return NULL;

	unsigned int offset = *(unsigned short *) (buffer + 0xC);
	for (int i = 0; i <= index; i++) {
		if (offset + 8 > size) return NULL;
		unsigned int size = *(unsigned int *) (buffer + offset + 4);

		if (i == index) return buffer + offset;
		offset += size;
	}
	return NULL;
}

char *g2dGetSectionByMagic(char *buffer, unsigned int size, unsigned int sectionMagic) {
	int nSections = g2dGetNumberOfSections(buffer, size);
	unsigned int offset = *(unsigned short *) (buffer + 0xC);

	for (int i = 0; i <= nSections; i++) {
		if (offset + 8 > size) return NULL;
		unsigned int magic = *(unsigned int *) (buffer + offset);
		unsigned int size = *(unsigned int *) (buffer + offset + 4);

		if (magic == sectionMagic) return buffer + offset;
		offset += size;
	}
	return NULL;
}
