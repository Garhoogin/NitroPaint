#pragma once
#include <stdlib.h>

#include "compression.h"
#include "bstream.h"
#include "struct.h"

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
#define FILE_TYPE_FONT       13
#define FILE_TYPE_CMAP       14
#define FILE_TYPE_BNLL       15
#define FILE_TYPE_BNCL       16
#define FILE_TYPE_BNBL       17
#define FILE_TYPE_MESG       18
#define FILE_TYPE_MAX        19  // highest file type +1

// ----- common status codes
#define OBJ_STATUS_SUCCESS     0  // the operation completed successfully
#define OBJ_STATUS_INVALID     1  // the byte stream was not a valid object
#define OBJ_STATUS_NO_MEMORY   2  // not enough memory
#define OBJ_STATUS_UNSUPPORTED 3  // feature unsupported
#define OBJ_STATUS_NO_ACCESS   4  // access denied accessing a resource
#define OBJ_STATUS_MISMATCH    5  // a data mismatch error occurred
#define OBJ_STATUS_MAX         6

#define OBJ_SUCCEEDED(s)       ((s)==OBJ_STATUS_SUCCESS)


typedef struct ObjHeader_ ObjHeader;

// ----- file identification flag constants
typedef enum ObjIdFlag_ {
	OBJ_ID_HEADER            = (1 <<  0),  // File has a header.
	OBJ_ID_FOOTER            = (1 <<  1),  // File has a footer.
	OBJ_ID_SIGNATURE         = (1 <<  2),  // File has a signature (specific identifiable byte mark in the header)
	OBJ_ID_CHUNKED           = (1 <<  3),  // File is arranged as a sequence of binary blocks
	OBJ_ID_CHECKSUM          = (1 <<  4),  // File includes a checksum of data
	OBJ_ID_COMPRESSION       = (1 <<  5),  // File data includes compressed data (not counting total file compression)
	OBJ_ID_VALIDATED         = (1 <<  6),  // File includes data which may be trivially deemed invalid
	OBJ_ID_SIZE_CHECK        = (1 <<  7),  // File identification includes a check for the file's specific size
	OBJ_ID_OFFSETS           = (1 <<  8),  // File includes offsets to data fields
	OBJ_ID_WINCODEC          = (1 <<  9),  // File is a Windows codec (e.g. image file on the host system)
	OBJ_ID_WINCODEC_OVERRIDE = (1 << 10),  // File may validate as a Windows codec but is not one
} ObjIdFlag;


typedef int (*ObjReader) (ObjHeader *object, char *buffer, int size);
typedef int (*ObjWriter) (ObjHeader *object, BSTREAM *stream);
typedef int (*ObjInitProc) (ObjHeader *object);
typedef void (*ObjDispose) (ObjHeader *object);

// ----- callback function to identify file validity of a given format
typedef int (*ObjIdProc) (const unsigned char *buffer, unsigned int size);

typedef struct ObjTypeEntry_ {
	size_t size;        // The size of the object data
	char *name;         // The type name
	ObjReader reader;   // object reader routine
	ObjWriter writer;   // object writer routine
	ObjInitProc init;   // object initializer routine
	ObjDispose dispose; // object dispose routine
} ObjTypeEntry;

typedef struct ObjIdEntry_ {
	int type;          // The file type
	int format;        // The file format (specific to the type)
	char *name;        // The format name
	ObjIdFlag idFlag;  // The flags for identification
	ObjIdProc idProc;  // The callback for identification
} ObjIdEntry;

void ObjInitCommon(void);
void ObjRegisterType(int type, size_t objSize, const char *name, ObjReader reader, ObjWriter writer, ObjInitProc init, ObjDispose dispose);
void ObjRegisterFormat(int type, int format, const char *name, ObjIdFlag flag, ObjIdProc proc);
void ObjIdentifyMultipleByType(StList *list, const unsigned char *buffer, unsigned int size, int type);
int ObjIdentifyExByType(const unsigned char *buffer, unsigned int size, int type, int *pFormat);
int ObjIdentifyEx(const unsigned char *buffer, unsigned int size, int *pFormat);

ObjHeader *ObjAlloc(int type, int format);


typedef struct ObjLink_ {
	ObjHeader *to;
	StList from;
} ObjLink;

struct ObjHeader_ {
	int size;             // Size of the containing struct in bytes
	int type;             // The type of file
	int format;           // The format of file (specific to file type)
	int compression;      // The compression scheme used on this file
	ObjDispose dispose;   // The callback for freeing the file
	ObjWriter writer;     // The callback for writing the file to a stream
	void *combo;          // Pointer to a structure maintaining strict links to objects in the same file
	ObjLink link;         // A structure maintaining file links to this file
	char *fileLink;       // The name of the file that this object references
	char *comment;        // The stored file comment (if supported)
};

extern const char *const g_ObjCompressionNames[];

//
// Converts a status code into a string.
//
const wchar_t *ObjStatusToString(int status);

const char *ObjGetFileTypeName(int type);

const char *ObjGetFormatNameByType(int type, int format);

unsigned int ObjGetFormatCountByType(int type);

//
// Get a file name from a file path.
//
wchar_t *ObjGetFileNameFromPath(const wchar_t *path);

//
// Identify the type of a file based on its bytes and file name. File name is
// is only used as a fallback when regular detection becomes too vague to rely
// on.
//
int ObjIdentify(unsigned char *file, unsigned int size, const wchar_t *path, int knownType, int *pCompression, int *pFormat);

//
// Compute CRC16 checksum for an array of bytes.
//
unsigned short ObjComputeCrc16(const unsigned char *data, int length, unsigned short init);

//
// Free the resources held by an open file, after which it can be safely freed
//
void ObjFree(ObjHeader *header);

//
// Determines the validity of an object.
//
int ObjIsValid(ObjHeader *header);

//
// Read an entire file into memory from path. No decompression is performed.
//
void *ObjReadWholeFile(const wchar_t *name, unsigned int *size);

//
// Reads a file into the specified object with the given reader function.
//
int ObjReadFile(ObjHeader **ppObject, const wchar_t *name, int type, int format, int compression);

//
// Reads a file.
//
ObjHeader *ObjAutoReadFile(const wchar_t *path, int type);

//
// Read a buffer.
//
int ObjReadBuffer(ObjHeader **ppObject, const unsigned char *buffer, unsigned int size, int type, int format, int compression);

//
// Writes a file to the disk using a provided writer function.
//
int ObjWriteFile(ObjHeader *object, const wchar_t *name);

//
// Link an object to another in a directed way.
//
void ObjLinkObjects(ObjHeader *to, ObjHeader *from);

//
// Unlink an object from another.
//
void ObjUnlinkObjects(ObjHeader *to, ObjHeader *from);

//
// Sets a file link for an object.
//
void ObjSetFileLink(ObjHeader *obj, const wchar_t *link);

//
// Update the file links of all objects linking to this one.
//
void ObjUpdateLinks(ObjHeader *obj, const wchar_t *path);
