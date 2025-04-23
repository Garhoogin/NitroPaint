#pragma once

#include <stdint.h>
#include <limits.h>


//
// structure common status codes
//
typedef enum StStatus_ {
	ST_STATUS_OK,                   // operation succeeded
	ST_STATUS_NOMEM,                // not enough memory
	ST_STATUS_UNSUPPORTED,          // unsupported operation
	ST_STATUS_NOTFOUND              // element could not be found
} StStatus;

#define ST_SUCCEEDED(s)   ((s)==ST_STATUS_OK)


//
// structure common index values
//
#define ST_INDEX_FIRST                     0  // index of first element in a list
#define ST_INDEX_MAX            (SIZE_MAX-1)  // maximal index of a list
#define ST_INDEX_NOT_FOUND      (SIZE_MAX-0)  // special index indicating an element was not found


// comparator function for structures
typedef int (*StComparator) (const void *e1, const void *e2);

//hash function for structures
typedef unsigned int (*StHashFunction) (const void *elem);


// ----- list structure

#define ST_LIST_DEFAULT_CAPACITY 16

typedef struct StList_ {
	size_t elemSize;             // size of each element
	size_t length;               // length of list
	size_t capacity;             // buffer capacity of list
	int distinct;                // different entries do not compare equal
	void *buffer;                // list buffer
	StComparator comparator;     // comparator function for ordered lists
} StList;

StStatus StListCreate(StList *list, size_t elemSize, StComparator comparator);
StStatus StListFree(StList *list);
StStatus StListAdd(StList *list, const void *elem);
StStatus StListInsert(StList *list, size_t idx, const void *elem);
StStatus StListGet(StList *list, size_t idx, void *dest);
void *StListGetPtr(StList *list, size_t idx);
size_t StListIndexOf(StList *list, const void *elem);
StStatus StListRemove(StList *list, size_t idx);
StStatus StListClear(StList *list);
StStatus StListSort(StList *list, StComparator comparator);
StStatus StListMakeSorted(StList *list, StComparator comparator);
void *StListDecapsulate(StList *list, size_t *pLength);

#define StListCreateInline(list,type,comparator) (StListCreate((list),sizeof(type),(comparator)))
#define StListGetInline(list,type,idx)           (*((type)*)StListGetPtr((list),(idx)))
#define StListAddInline(list,type,elem)          do{type _x=(elem);(void)StListAdd((list),&_x);}while(0)


// ----- stack structure

typedef struct StList_ StStack;


StStatus StStackCreate(StStack *stack, size_t elemSize);
StStatus StStackFree(StStack *stack);
StStatus StStackPush(StStack *stack, const void *elem);
StStatus StStackPop(StStack *stack, void *elem);
void *StStackPopPtr(StStack *stack);
StStatus StStackClear(StStack *stack);

#define StStackCreateInline(stack,type)          (StStackCreate((stack),sizeof(type)))
#define StStackPopInline(stack,type)             (*((type)*)StStackPopPtr((stack)))
#define StStackPushInline(stack,type,elem)       do{(type) _x=(elem);(void)StStackPush((stack),&_x);}while(0)


// ----- map structure

typedef struct StMap_ {
	StList list;
	size_t sizeKey;
	size_t sizeValue;
	unsigned char *scratch;
} StMap;

StStatus StMapCreate(StMap *map, size_t sizeKey, size_t sizeValue);
StStatus StMapFree(StMap *map);
StStatus StMapPut(StMap *map, const void *key, const void *value);
StStatus StMapGet(StMap *map, const void *key, void *outValue);
StStatus StMapGetKeyValue(StMap *map, size_t i, void *outKey, void *outValue);
void *StMapGetPtr(StMap *map, const void *key);
