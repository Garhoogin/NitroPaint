#pragma once
#include "bstream.h"

// ----- NNS G2D functions

int NnsG2dIsValid(const unsigned char *buffer, unsigned int size);

int NnsG2dIsOld(const unsigned char *buffer, unsigned int size);

int NnsG2dGetNumberOfSections(const unsigned char *buffer, unsigned int size);

unsigned char *NnsG2dGetSectionByIndex(const unsigned char *buffer, unsigned int size, int index);

unsigned char *NnsG2dGetSectionByMagic(const unsigned char *buffer, unsigned int size, unsigned int sectionMagic);

// ----- NNS G3D functions

typedef char *(*NnsGetResourceNameCallback) (void *resource);


int NnsG3dWriteDictionary(BSTREAM *stream, void *resources, int itemSize, int nItems, NnsGetResourceNameCallback getName, int dictEntrySize);

