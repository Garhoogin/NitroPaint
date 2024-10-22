#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>

#include "struct.h"


#ifdef _MSC_VER
#ifndef inline
#define inline __inline
#endif
#endif



// ----- internal list function

static StStatus StListEnsureCapacity(StList *list, size_t cap) {
	if (list->capacity >= cap) return ST_STATUS_OK;

	//expand size to fit capacity
	size_t newcap = list->capacity * 3 / 2;
	if (newcap < cap) newcap = cap;

	void *newbuf = realloc(list->buffer, newcap * list->elemSize);
	if (newbuf == NULL && newcap > cap) {
		//try reduced size
		newcap = cap;
		newbuf = realloc(list->buffer, newcap * list->elemSize);
	}

	if (newbuf == NULL) {
		//no memory
		return ST_STATUS_NOMEM;
	}

	list->buffer = newbuf;
	list->capacity = newcap;
	return ST_STATUS_OK;
}

static StStatus StListShrinkInternal(StList *list, size_t to) {
	size_t newcap = to;
	if (newcap == list->capacity) return ST_STATUS_OK;
	
	//reallocate buffer. if fails, return error
	void *newbuf = realloc(list->buffer, newcap * list->elemSize);
	if (newbuf == NULL) return ST_STATUS_NOMEM;

	list->buffer = newbuf;
	list->capacity = newcap;
	return ST_STATUS_OK;
}

static StStatus StListCheckShrink(StList *list) {
	//if the list is full, do nothing
	if (list->capacity == list->length) return ST_STATUS_OK;

	//check 2/3 usage, else clamp size
	if (list->length >= (list->capacity * 3 / 2)) return ST_STATUS_OK;

	//shrink. not critical if this fails. (would never fail under normal circumstances anyway).
	(void) StListShrinkInternal(list, list->length);
	return ST_STATUS_OK;
}

static inline void *StListGetElemPtr(StList *list, size_t index) {
	return (void *) (((uintptr_t) list->buffer) + (index * list->elemSize));
}

static void StListInsertInternal(StList *list, size_t index, const void *p) {
	//assumed internal state: list capacity has room for the inserted element (prior call to StListEnsureCapacity)

	void *e1 = StListGetElemPtr(list, index);
	if (index < list->length) {
		//if inserting not at the end, move elements
		size_t nElemMove = list->length - index;
		void *e2 = StListGetElemPtr(list, index + 1);
		memmove(e2, e1, nElemMove * list->elemSize);
	}
	memcpy(e1, p, list->elemSize);

	list->length++;
}

static size_t StListSearchElem(StList *list, const void *elem) {
	//if list is empty, put at beginning
	if (list->length == 0) return 0;

	//narrow in on an insertion point (hi non-inclusive)
	size_t lo = 0, hi = list->length;
	while ((hi - lo) > 1) {
		size_t med = lo + (hi - lo) / 2;
		void *atmed = StListGetElemPtr(list, med);
		int cmp = list->comparator(atmed, elem);

		if (cmp < 0) {
			//keep bottom half of search zone, discarding med
			hi = med;
		} else if (cmp > 0) {
			//keep top half of search zone, discarding med
			lo = med + 1;
		} else {
			//found
			return med;
		}
	}
	return lo;
}



// ----- public list API

StStatus StListCreate(StList *list, size_t elemSize, StComparator comparator) {
	list->comparator = comparator;
	list->elemSize = elemSize;
	list->length = 0;
	list->capacity = ST_LIST_DEFAULT_CAPACITY;
	list->buffer = malloc(list->capacity * list->elemSize);

	//check create succeeded
	while (list->buffer == NULL && list->capacity) {
		//shrink capacity and try again
		list->capacity >>= 1;
		list->buffer = malloc(list->capacity * list->elemSize);
	}
	return ST_STATUS_OK;
}

StStatus StListFree(StList *list) {
	list->length = 0;
	list->elemSize = 0;
	list->capacity = 0;

	if (list->buffer != NULL) {
		free(list->buffer);
		list->buffer = NULL;
	}
	return ST_STATUS_OK;
}

StStatus StListAdd(StList *list, const void *elem) {
	if (list->length > ST_INDEX_MAX) return ST_STATUS_NOMEM;

	StStatus status = StListEnsureCapacity(list, list->length + 1);
	if (!ST_SUCCEEDED(status)) return status;

	size_t pos;
	if (list->comparator == NULL) {
		//append to the end of the list (no sorting)
		pos = list->length;
	} else {
		//insert into the list sorted
		pos = StListSearchElem(list, elem);
	}
	StListInsertInternal(list, pos, elem);
	return ST_STATUS_OK;
}

StStatus StListInsert(StList *list, size_t idx, const void *elem) {
	//cannot insert into a sorted list
	if (list->comparator != NULL) return ST_STATUS_UNSUPPORTED;
	if (list->length > ST_INDEX_MAX) return ST_STATUS_NOMEM;
	if (idx > list->length) return ST_STATUS_NOTFOUND; // allow idx==list->length

	StStatus status = StListEnsureCapacity(list, list->length + 1);
	if (!ST_SUCCEEDED(status)) return status;

	StListInsertInternal(list, idx, elem);
	return ST_STATUS_OK;
}

StStatus StListGet(StList *list, size_t idx, void *dest) {
	if (idx >= list->length) return ST_STATUS_NOTFOUND;

	void *src = StListGetElemPtr(list, idx);
	memcpy(dest, src, list->elemSize);
	return ST_STATUS_OK;
}

void *StListGetPtr(StList *list, size_t idx) {
	if (idx >= list->length) return NULL;

	return StListGetElemPtr(list, idx);
}

size_t StListIndexOf(StList *list, const void *elem) {
	//search list
	if (list->comparator == NULL) {
		//if the list is unsorted, we can't binary search.
		size_t i;
		for (i = 0; i < list->length; i++) {
			void *e1 = StListGetElemPtr(list, i);
			if (memcmp(e1, elem, list->elemSize) == 0) return i;
		}
	} else {
		size_t pos = StListSearchElem(list, elem);
		void *at = StListGetElemPtr(list, pos);

		//if we found it, return the index (optimize for unique comparisons)
		if (memcmp(at, elem, list->elemSize) == 0) return pos;

		//scan backwards for other matching
		size_t pos2 = pos;
		while (pos2-- > 0) {
			void *p2 = StListGetElemPtr(list, pos2);
			if (list->comparator(elem, p2) != 0) break;
			if (memcmp(elem, p2, list->elemSize) == 0) return pos2;
		}

		//scan forwards for other matching
		pos2 = pos;
		while (++pos2 < list->length) {
			void *p2 = StListGetElemPtr(list, pos2);
			if (list->comparator(elem, p2) != 0) break;
			if (memcmp(elem, p2, list->elemSize) == 0) return pos2;
		}
	}
	return ST_INDEX_NOT_FOUND;
}

StStatus StListRemove(StList *list, size_t idx) {
	if (idx >= list->length) return ST_STATUS_NOTFOUND;

	size_t nElemMove = list->length - idx - 1;
	if (nElemMove > 0) {
		void *elem1 = StListGetElemPtr(list, idx);
		void *elem2 = StListGetElemPtr(list, idx + 1);
		memmove(elem1, elem2, nElemMove * list->elemSize);
	}
	list->length--;

	(void) StListCheckShrink(list);
	return ST_STATUS_OK;
}

StStatus StListClear(StList *list) {
	list->length = 0;
	(void) StListCheckShrink(list);
	return ST_STATUS_OK;
}

StStatus StListShrink(StList *list) {
	(void) StListShrinkInternal(list, list->length);
	return ST_STATUS_OK;
}

StStatus StListSort(StList *list, StComparator comparator) {
	//can't sort a sorted list
	if (list->comparator != NULL) return ST_STATUS_UNSUPPORTED;

	qsort(list->buffer, list->length, list->elemSize, comparator);
	return ST_STATUS_OK;
}

StStatus StListMakeSorted(StList *list, StComparator comparator) {
	StListSort(list, comparator);
	list->comparator = comparator;
	return ST_STATUS_OK;
}




// ----- stack API

StStatus StStackCreate(StStack *stack, size_t elemSize) {
	return StListCreate((StList *) stack, elemSize, NULL);
}

StStatus StStackFree(StStack *stack) {
	return StListFree((StList *) stack);
}

StStatus StStackPush(StStack *stack, const void *elem) {
	return StListAdd((StList *) stack, elem);
}

StStatus StStackPop(StStack *stack, void *elem) {
	if (stack->length == 0) return ST_STATUS_NOTFOUND;

	(void) StListGet((StList *) stack, stack->length - 1, elem);
	stack->length--;
	return ST_STATUS_OK;
}

void *StStackPopPtr(StStack *stack) {
	if (stack->length == 0) return NULL;

	void *p = StListGetElemPtr((StList *) stack, stack->length - 1);
	stack->length--;
	return p;
}

StStatus StStackClear(StStack *stack) {
	return StListClear((StList *) stack);
}
