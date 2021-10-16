#pragma once

int g2dIsValid(char *buffer, int size);

int g2dGetNumberOfSections(char *buffer, int size);

char *g2dGetSectionByIndex(char *buffer, int size, int index);

char *g2dGetSectionByMagic(char *buffer, int size, unsigned int sectionMagic);
