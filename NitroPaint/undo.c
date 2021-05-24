#include "undo.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void undoInitialize(UNDO *undoStack, int elementSize) {
	undoStack->elementSize = elementSize;
	undoStack->stackPosition = -1;
	undoStack->freeFunction = NULL;
	undoStack->stackSize = 0;
	undoStack->elements = calloc(1, elementSize);
	InitializeCriticalSection(&undoStack->criticalSection);
}

void *undoGetElement(UNDO *undoStack, int index) {
	return (void *) (((uintptr_t) undoStack->elements) + (index * undoStack->elementSize));
}

void *undoGetStackPosition(UNDO *undoStack) {
	return undoGetElement(undoStack, undoStack->stackPosition);
}

void *undo(UNDO *undoStack) {
	EnterCriticalSection(&undoStack->criticalSection);
	if (undoStack->stackPosition > 0) {
		undoStack->stackPosition--;
	}
	void *position = undoGetStackPosition(undoStack);
	LeaveCriticalSection(&undoStack->criticalSection);
	return position;
}

void *redo(UNDO *undoStack) {
	EnterCriticalSection(&undoStack->criticalSection);
	if (undoStack->stackPosition < undoStack->stackSize - 1) {
		undoStack->stackPosition++;
	}
	void *position = undoGetStackPosition(undoStack);
	LeaveCriticalSection(&undoStack->criticalSection);
	return position;
}

void undoAdd(UNDO *undoStack, void *elem) {
	EnterCriticalSection(&undoStack->criticalSection);
	if (undoStack->stackPosition != undoStack->stackSize - 1) {
		for (int i = undoStack->stackPosition + 1; i < undoStack->stackSize; i++) {
			if (undoStack->freeFunction != NULL) {
				undoStack->freeFunction(undoGetElement(undoStack, i));
			}
		}
	}
	int newPosition = undoStack->stackPosition + 1;
	undoStack->stackPosition = newPosition;
	undoStack->stackSize = newPosition + 1;
	undoStack->elements = realloc(undoStack->elements, undoStack->stackSize * undoStack->elementSize);
	memcpy(undoGetElement(undoStack, newPosition), elem, undoStack->elementSize);
	LeaveCriticalSection(&undoStack->criticalSection);
}

void undoDestroy(UNDO *undo) {
	EnterCriticalSection(&undo->criticalSection);
	for (int i = 0; i < undo->stackSize; i++) {
		if (undo->freeFunction != NULL) {
			undo->freeFunction(undoGetElement(undo, i));
		}
	}
	undo->elementSize = 0;
	undo->stackSize = 0;
	undo->stackPosition = -1;
	free(undo->elements);
	undo->elements = NULL;
	undo->freeFunction = NULL;
	LeaveCriticalSection(&undo->criticalSection);
	DeleteCriticalSection(&undo->criticalSection);
}