#pragma once
#include <stdlib.h>

#include "compression.h"
#include "bstream.h"
#include "struct.h"

#define FILE_TYPE_INVALID     0
#define FILE_TYPE_PALETTE     1
#define FILE_TYPE_CHARACTER   2
#define FILE_TYPE_SCREEN      3
#define FILE_TYPE_CELL        4
#define FILE_TYPE_NSBTX       5
#define FILE_TYPE_TEXTURE     7
#define FILE_TYPE_NANR        8
#define FILE_TYPE_IMAGE       9
#define FILE_TYPE_COMBO2D    10
#define FILE_TYPE_NMCR       11
#define FILE_TYPE_NMAR       12
#define FILE_TYPE_FONT       13
#define FILE_TYPE_CMAP       14
#define FILE_TYPE_BNLL       15
#define FILE_TYPE_BNCL       16
#define FILE_TYPE_BNBL       17
#define FILE_TYPE_MESG       18
#define FILE_TYPE_SCENE      19
#define FILE_TYPE_MAX        20  // highest file type +1

// ----- common status codes
#define OBJ_STATUS_SUCCESS            0  // the operation completed successfully
#define OBJ_STATUS_INVALID            1  // the byte stream did not represent a valid object
#define OBJ_STATUS_NO_MEMORY          2  // not enough memory
#define OBJ_STATUS_UNSUPPORTED        3  // feature unsupported
#define OBJ_STATUS_NO_ACCESS          4  // access denied when accessing a resource
#define OBJ_STATUS_MISMATCH           5  // a data mismatch error occurred
#define OBJ_STATUS_OUTSTANDING_REFS   6  // the resource could not be freed because of outstanding references
#define OBJ_STATUS_ALREADY_REGISTERED 7  // the object could not be registered because it was already registered
#define OBJ_STATUS_MAX                8

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

typedef struct ObjIdEntry_ {
	int type;          // The file type
	int format;        // The file format (specific to the type)
	char *name;        // The format name
	ObjIdFlag idFlag;  // The flags for identification
	ObjIdProc idProc;  // The callback for identification
	ObjReader reader;  // Object reader routine
	ObjWriter writer;  // Object writer routine
} ObjIdEntry;

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
	ObjHeader *combo;     // Pointer to a structure maintaining strict links to objects in the same file
	ObjLink link;         // A structure maintaining file links to this file
	char *fileLink;       // The name of the file that this object references
	char *comment;        // The stored file comment (if supported)
};

// -----------------------------------------------------------------------------------------------
// Name: ObjInitCommon
//
// Initializes the object manager. This function must be called once before any other object
// manager routines are used.
// -----------------------------------------------------------------------------------------------
void ObjInitCommon(void);

// -----------------------------------------------------------------------------------------------
// Name: ObjRegisterType
//
// Register an object type with the object manager. This provides the object manager with the
// required information to allocate, initialize, and free an object of the new type.
//
// The objSize parameter is the size of the object's structure. After the object is allocated,
// the init function pointer is called on the new struct, if specified. When the object is
// being freed, the dispose function pointer is called before memory is released.
//
// The type ID may be passed as 0, in which case an ID will be allocated by the manager. When
// the ID is nonzero, it must be unique, and an error is returned if an object of that ID is
// already registered.
//
// The type name must not be NULL, and is copied internally by the object manager.
//
// Parameters:
//   type          The type ID of the object.
//   objSize       The object structure size.
//   name          The object type name.
//   init          The initialization routine for the object type.
//   dispose       The disposal routine for the object type.
//
// Returns:
//   The ID of the registered type, or 0 on failure.
// -----------------------------------------------------------------------------------------------
int ObjRegisterType(
	int         type,
	size_t      objSize,
	const char *name,
	ObjInitProc init,
	ObjDispose  dispose
);

// -----------------------------------------------------------------------------------------------
// Name: ObjRegisterFormat
//
// Register a file format with the object manager. This provides the object manager with the
// information required to validate a byte sequence against the format, read one from a byte
// sequence, and write one to a byte stream.
//
// The type ID must be nonzero and reference a registered object type. The format ID may be zero,
// in which case a format ID will be allocated by the object manager. If it is nonzero, it must
// not be the same as a registered format of the object type. They are not globally unique.
//
// The format name must not be NULL, and is copied internally by the object manager.
//
// Parameters:
//   entry         The format information.
//
// Returns:
//   The ID of the registered file format, or 0 on failure.
// -----------------------------------------------------------------------------------------------
int ObjRegisterFormat(
	const ObjIdEntry *entry
);

// -----------------------------------------------------------------------------------------------
// Name: ObjAlloc
//
// Allocates an object of the specified type and format. The type field should be a type ID 
// returned by ObjRegisterType, and format should be a format ID returned by ObjRegisterFormat. 
// 
// On success, the function returns a pointer to the object, initialized by the object type's
// initialization routine. On failure, this function returns NULL.
//
// Parameters:
//   type          The object type
//   format        The file format
//
// Returns:
//   The pointer to the object, on success, or NULL on failure.
// -----------------------------------------------------------------------------------------------
ObjHeader *ObjAlloc(
	int type,
	int format
);

// -----------------------------------------------------------------------------------------------
// Name: ObjFree
//
// Frees the resources held by an object. Before releasing memory held, the object's dispose
// routine is called. After returning, access to the object is undefined.
//
// Parameters:
//   obj           The object to be freed
// -----------------------------------------------------------------------------------------------
void ObjFree(
	ObjHeader *obj
);

// -----------------------------------------------------------------------------------------------
// Name: ObjIdentify
//
// Identify the type, format, and compression of a sequence of bytes. 
//
// First, the compression format of the buffer is identified. Next, the buffer is validated
// against the registered file formats. The format of highest confidence is chosen. Optionally,
// the type of object may be specified by the knownType parameter, or FILE_TYPE_INVALID if it is
// not known beforehand.
//
// In the case where the path is specified, it is checked for suffixes for certain types of files
// files before checking the rest of the format registry. 
//
// Parameters:
//   buffer        The input byte buffer
//   size          The size of the input buffer
//   path          The file path (if it exists)
//   knownType     The type of object (optional)
//   pCompression  The pointer to the validated compression format (optional)
//   pFormat       The pointer to the validated format (optional)
//
// Returns:
//   The type ID of the format that validated the buffer, or FILE_TYPE_INVALID if none.
// -----------------------------------------------------------------------------------------------
int ObjIdentify(
	unsigned char *buffer,
	unsigned int   size,
	const wchar_t *path,
	int            knownType,
	int           *pCompression,
	int           *pFormat
);

// -----------------------------------------------------------------------------------------------
// Name: ObjIdentifyMultipleByType
//
// Validate a byte sequence against the registered file formats. Optionally, only formats for a
// specific object type can be validated. When more than one format validates the buffer, multiple
// entries are added to the output list. The output elements are added in descending order of
// confidence.
//
// This function does not perform any decompression of the input buffer.
//
// Parameters:
//   list          The output list of ObjIdEntry (must be already initialized)
//   buffer        The input byte buffer
//   size          The size of the input byte buffer.
//   type          The type of object (optional).
// -----------------------------------------------------------------------------------------------
void ObjIdentifyMultipleByType(
	StList              *list,
	const unsigned char *buffer,
	unsigned int        size,
	int                 type
);

// -----------------------------------------------------------------------------------------------
// Name: ObjIdentifyExByType
//
// Validate a byte sequence against the registered file formats. Optionally, only formats for a
// specific object type can be validated. When more than one format validates the buffer, only
// the one with highest confidence is returned. 
//
// This function does not perform any decompression of the input buffer.
//
// Parameters:
//   buffer        The input byte buffer
//   size          The size of the input buffer
//   type          The type of object (optional).
//   pFormat       The pointer to the validated format (optional)
//
// Returns:
//   The type ID of the format that validated the buffer, or FILE_TYPE_INVALID if none.
// -----------------------------------------------------------------------------------------------
int ObjIdentifyExByType(
	const unsigned char *buffer,
	unsigned int         size,
	int                  type,
	int                 *pFormat
);

// -----------------------------------------------------------------------------------------------
// Name: ObjIdentifyEx
//
// Validate a byte sequence against the registered file formats. When more than one format
// validates the buffer, only the one with highest confidence is returned. 
//
// This function does not perform any decompression of the input buffer.
//
// Parameters:
//   buffer        The input byte buffer
//   size          The size of the input buffer
//   pFormat       The pointer to the validated format (optional)
//
// Returns:
//   The type ID of the format that validated the buffer, or FILE_TYPE_INVALID if none.
// -----------------------------------------------------------------------------------------------
int ObjIdentifyEx(
	const unsigned char *buffer,
	unsigned int         size,
	int                 *pFormat
);

// -----------------------------------------------------------------------------------------------
// Name: ObjReadBuffer
//
// Reads an object from a byte buffer.
//
// Parameters:
//   ppObject      The pointer to receive the allocated object
//   buffer        The input byte buffer
//   size          The size of the input buffer
//   type          The object type
//   format        The file format
//   compression   The compression format
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
int ObjReadBuffer(
	ObjHeader          **ppObject,
	const unsigned char *buffer,
	unsigned int         size,
	int                  type,
	int                  format,
	int                  compression
);

// -----------------------------------------------------------------------------------------------
// Name: ObjReadBuffer
//
// Reads an object from a file.
//
// Parameters:
//   ppObject      The pointer to receive the allocated object
//   path          The input byte buffer
//   type          The object type
//   format        The file format
//   compression   The compression format
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
int ObjReadFile(
	ObjHeader    **ppObject,
	const wchar_t *path,
	int            type,
	int            format,
	int            compression
);

// -----------------------------------------------------------------------------------------------
// Name: ObjAutoReadFile
//
// Reads an object from a file with automatic type, format, and compression detection.
//
// Parameters:
//   path          The input byte buffer
//   type          The object type
//
// Returns:
//   The allocated object if successful, or NULL on failure.
// -----------------------------------------------------------------------------------------------
ObjHeader *ObjAutoReadFile(
	const wchar_t *path,
	int            type
);

// -----------------------------------------------------------------------------------------------
// Name: ObjWrite
//
// Writes an object to a byte stream.
//
// Parameters:
//   object        The object
//   path          The byte stream
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
int ObjWrite(
	ObjHeader *object,
	BSTREAM   *stream
);

// -----------------------------------------------------------------------------------------------
// Name: ObjWriteFile
//
// Writes an object to a file.
//
// Parameters:
//   object        The input byte buffer
//   path          The object type
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
int ObjWriteFile(
	ObjHeader     *object,
	const wchar_t *path
);




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
// Compute CRC16 checksum for an array of bytes.
//
unsigned short ObjComputeCrc16(const unsigned char *data, int length, unsigned short init);

//
// Determines the validity of an object.
//
int ObjIsValid(ObjHeader *header);

//
// Read an entire file into memory from path. No decompression is performed.
//
void *ObjReadWholeFile(const wchar_t *name, unsigned int *size);

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



extern const char *const g_ObjCompressionNames[];
