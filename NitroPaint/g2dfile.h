#pragma once

int g2dIsValid(char *buffer, unsigned int size);

int g2dIsOld(char *buffer, unsigned int size);

int g2dGetNumberOfSections(char *buffer, unsigned int size);

char *g2dGetSectionByIndex(char *buffer, unsigned int size, int index);

char *g2dGetSectionByMagic(char *buffer, unsigned int size, unsigned int sectionMagic);
