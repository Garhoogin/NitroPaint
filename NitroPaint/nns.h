#pragma once

int g2dIsValid(const unsigned char *buffer, unsigned int size);

int g2dIsOld(const unsigned char *buffer, unsigned int size);

int g2dGetNumberOfSections(const unsigned char *buffer, unsigned int size);

unsigned char *g2dGetSectionByIndex(const unsigned char *buffer, unsigned int size, int index);

unsigned char *g2dGetSectionByMagic(const unsigned char *buffer, unsigned int size, unsigned int sectionMagic);
