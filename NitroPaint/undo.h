#pragma once
#include <Windows.h>

typedef struct UNDO_ {
	int elementSize;
	void (*freeFunction) (void *);
	int stackPosition;
	int stackSize;
	void *elements;
	CRITICAL_SECTION criticalSection;
} UNDO;

void undoInitialize(UNDO *undoStack, int elementSize);

void *undoGetElement(UNDO *undoStack, int index);

void *undoGetStackPosition(UNDO *undoStack);

void *undo(UNDO *undoStack);

void *redo(UNDO *undoStack);

void undoAdd(UNDO *undoStack, void *elem);

void undoDestroy(UNDO *undo);