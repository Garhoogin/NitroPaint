#pragma once
#include "compression.h"
#include "bstream.h"
#include <Windows.h>

#define FILE_TYPE_INVALID    0
#define FILE_TYPE_PALETTE    1
#define FILE_TYPE_CHARACTER  2
#define FILE_TYPE_SCREEN     3
#define FILE_TYPE_CELL       4
#define FILE_TYPE_NSBTX      5
#define FILE_TYPE_TEXTURE    7
#define FILE_TYPE_NANR       8
#define FILE_TYPE_IMAGE      9
#define FILE_TYPE_COMBO2D    10
#define FILE_TYPE_NMCR       11
#define FILE_TYPE_NMAR       12

// ----- common status codes
#define OBJ_STATUS_SUCCESS     0  //the operation completed successfully
#define OBJ_STATUS_INVALID     1  //the byte stream was not a valid object
#define OBJ_STATUS_NO_MEMORY   2  //not enough memory
#define OBJ_STATUS_UNSUPPORTED 3  //feature unsupported
#define OBJ_STATUS_NO_ACCESS   4  //access denied accessing a resource

#define OBJ_SUCCEEDED(s)       ((s)==OBJ_STATUS_SUCCESS)

typedef struct ObjLink_ {
	int nFrom;
	struct OBJECT_HEADER_ *to;
	struct OBJECT_HEADER_ **from;
} ObjLink;

typedef struct OBJECT_HEADER_ {
	int size;
	int type;
	int format;
	int compression;
	void (*dispose) (struct OBJECT_HEADER_ *);
	ObjLink link;
	char *fileLink;
	char *comment;
} OBJECT_HEADER;

typedef int (*OBJECT_READER) (OBJECT_HEADER *object, char *buffer, int size);
typedef int (*OBJECT_WRITER) (OBJECT_HEADER *object, BSTREAM *stream);

extern LPCWSTR g_ObjCompressionNames[];

LPCWSTR *ObjGetFormatNamesByType(int type);

//
// Get a file name from a file path.
//
LPWSTR ObjGetFileNameFromPath(LPCWSTR path);

//
// Identify the type of a file based on its bytes and file name. File name is
// is only used as a fallback when regular detection becomes too vague to rely
// on.
//
int ObjIdentify(char *file, int size, LPCWSTR path);

//
// Compute CRC16 checksum for an array of bytes.
//
unsigned short ObjComputeCrc16(unsigned char *data, int length, unsigned short init);

//
// Initialize a file's OBJECT_HEADER. The size field must be set before calling
// this function to the size of the whole file object.
//
void ObjInit(OBJECT_HEADER *header, int type, int format);

//
// Compress a file given its path using the specified compression type.
//
void ObjCompressFile(LPWSTR name, int compression);

//
// Free the resources held by an open file, after which it can be safely freed
//
void ObjFree(OBJECT_HEADER *header);

//
// Determines the validity of an object.
//
int ObjIsValid(OBJECT_HEADER *header);

//
// Read an entire file into memory from path. No decompression is performed.
//
void *ObjReadWholeFile(LPCWSTR name, int *size);

//
// Reads a file into the specified object with the given reader function.
//
int ObjReadFile(LPCWSTR name, OBJECT_HEADER *object, OBJECT_READER reader);

//
// Writes a file to the disk using a provided writer function.
//
int ObjWriteFile(LPCWSTR name, OBJECT_HEADER *object, OBJECT_WRITER writer);

//
// Link an object to another in a directed way.
//
void ObjLinkObjects(OBJECT_HEADER *to, OBJECT_HEADER *from);

//
// Unlink an object from another.
//
void ObjUnlinkObjects(OBJECT_HEADER *to, OBJECT_HEADER *from);

//
// Sets a file link for an object.
//
void ObjSetFileLink(OBJECT_HEADER *obj, const wchar_t *link);

//
// Update the file links of all objects linking to this one.
//
void ObjUpdateLinks(OBJECT_HEADER *obj, const wchar_t *path);
