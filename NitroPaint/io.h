#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct IoHandle_ *IoHandle;
typedef int IoStatus;

typedef enum IoBool_ {
	IO_FALSE,
	IO_TRUE
} IoBool;


// -----------------------------------------------------------------------------------------------
// Name: IoConvertPath
//
// Converts a special path into an internal representation. This is necessary if a file path would
// refer to a pipe.
//
// Parameters:
//   path          The input file path
//
// Returns:
//   The output file path, allocated on the heap. The caller is responsible for freeing this with
//   free(). On successful return, this function returns a non-NULL poiner, and the caller is
//   required to pass the returned path to IoFreeConvertedPath when the path is no longer used.
//   On failure, this returns NULL.
// -----------------------------------------------------------------------------------------------
wchar_t *IoConvertPath(
	const wchar_t *path
);

// -----------------------------------------------------------------------------------------------
// Name: IoFreeConvertedPath
//
// Frees the internal resources held by a converted path. This should be called on any file path
// returned by IoConvertPath that is no longer being used.
//
// If the path passed to the function was allocated on the heap, it is still the caller's
// responsibility to free this buffer.
//
// Parameters:
//   path          The input file path
// -----------------------------------------------------------------------------------------------
void IoFreeConvertedPath(
	const wchar_t *path
);

// -----------------------------------------------------------------------------------------------
// Name: IoReadBytes
//
// Reads a specific number of bytes from a file handle. If fewer than the number of bytes may be
// read then the function returns an error.
//
// Parameters:
//   hFile         The file handle to read from
//   buffer        The buffer to receive the bytes
//   size          The size of the buffer receiving bytes from the file
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
IoStatus IoReadBytes(
	IoHandle     hFile,
	void        *buffer,
	unsigned int size
);

// -----------------------------------------------------------------------------------------------
// Name: IoWriteBytes
//
// Writes a specific number of bytes to a file handle. If fewer than the number of bytes may be
// written then the function returns an error.
//
// Parameters:
//   hFile         The file handle to write to
//   buffer        The buffer to write
//   size          The size of the buffer
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
IoStatus IoWriteBytes(
	IoHandle     hFile,
	const void  *buffer,
	unsigned int size
);

// -----------------------------------------------------------------------------------------------
// Name: IoReadWholeFile
//
// Reads a file entirely into memory.
//
// Parameters:
//   path          The file path
//   size          Pointer to an integer receiving the file's size
//
// Returns:
//   A pointer to the file's bytes on success, or NULL on failure.
// -----------------------------------------------------------------------------------------------
void *IoReadWholeFile(
	const wchar_t *path,
	unsigned int  *size
);

// -----------------------------------------------------------------------------------------------
// Name: IoWriteWholeFile
//
// Writes a byte sequence to a file.
//
// Parameters:
//   path          The file path
//   buffer        A pointer to the byte sequence to write
//   size          The buffer size
//
// Returns:
//   The status of the operation.
// -----------------------------------------------------------------------------------------------
IoStatus IoWriteWholeFile(
	const wchar_t *path,
	const void    *buffer,
	unsigned int   size
);


