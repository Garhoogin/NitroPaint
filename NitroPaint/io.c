#include "io.h"

#include <Windows.h>
#include <string.h>


#define HANDLE_PATH_PREFIX   L"handle:\\\\"


// ----- routines for operating on paths

static IoBool IoiPathStartsWith(const wchar_t *str, const wchar_t *substr) {
	if (wcslen(substr) > wcslen(str)) return IO_FALSE;
	return !_wcsnicmp(str, substr, wcslen(substr));
}

static IoBool IoiIsPathNamedPipe(const wchar_t *path) {
	//pipe path takes the form '\\server\pipe\...'
	if (!IoiPathStartsWith(path, L"\\\\")) return IO_FALSE;

	path += 2;
	const wchar_t *pipe = wcschr(path, L'\\');
	if (pipe == NULL) return IO_FALSE;

	if (!IoiPathStartsWith(pipe, L"\\pipe\\")) return IO_FALSE;
	return IO_TRUE;
}

static IoBool IoiIsPathHandle(const wchar_t *path) {
	return IoiPathStartsWith(path, HANDLE_PATH_PREFIX);
}

static IoHandle IoiHandleFromPath(const wchar_t *path) {
	return (IoHandle) _wtol(path + wcslen(HANDLE_PATH_PREFIX));
}


// ----- path conversion routines

wchar_t *IoConvertPath(const wchar_t *path) {
	//if a file is from a named pipe, we open the pipe and construct a path indicating the
	//handle value. We do this to implement the required special handling of a file served
	//from a pipe.

	if (IoiIsPathNamedPipe(path)) {
		//pipe format: \\server\pipe\ID:FileName

		//open the pipe
		HANDLE hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) return NULL; // invalid file path or could not be opened

		//get the filename component
		const wchar_t *pipeName = wcschr(path + 2, L'\\') + 6;

		const wchar_t *filename = wcschr(pipeName, L':');
		if (filename != NULL) {
			//skip the colon
			filename++;
		} else {
			//revert to the pipe name as the file name
			filename = pipeName;
		}

		wchar_t handlebuf[32] = { 0 };
		int handlelen = wsprintfW(handlebuf, L"%lu", (unsigned long) hFile);

		wchar_t *buf = (wchar_t *) calloc(wcslen(HANDLE_PATH_PREFIX) + handlelen + 1 + wcslen(filename) + 1, sizeof(wchar_t));
		wsprintfW(buf, L"%s%s\\%s", HANDLE_PATH_PREFIX, handlebuf, filename);

		return buf;
	} else {
		//duplicate the string
		return _wcsdup(path);
	}
}

void IoFreeConvertedPath(const wchar_t *path) {
	//assume the caller owns the string (they are responsible for freeing)

	if (IoiIsPathHandle(path)) {
		//parse int handle (truncates on slash)
		IoHandle hFile = IoiHandleFromPath(path);
		CloseHandle((HANDLE) hFile);
	}
}



// ----- file I/O routines

IoStatus IoReadBytes(IoHandle hFile, void *buffer, unsigned int size) {
	unsigned char *bufferBytes = (unsigned char *) buffer;
	unsigned int ofs = 0, nRemaining = size;

	//read loop
	while (nRemaining > 0) {
		DWORD dwRead;
		BOOL b = ReadFile(hFile, bufferBytes + ofs, nRemaining, &dwRead, NULL);
		if (!b) return GetLastError();

		//successful API call, check for zero bytes
		if (dwRead == 0) return ERROR_FILE_OFFLINE;

		//next
		ofs += dwRead;
		nRemaining -= dwRead;
	}

	return ERROR_SUCCESS;
}

IoStatus IoWriteBytes(IoHandle hFile, const void *buffer, unsigned int size) {
	unsigned char *bufferBytes = (unsigned char *) buffer;
	unsigned int ofs = 0, nRemaining = size;

	//read loop
	while (nRemaining > 0) {
		DWORD dwWritten;
		BOOL b = WriteFile(hFile, bufferBytes + ofs, nRemaining, &dwWritten, NULL);
		if (!b) return GetLastError();

		//successful API call, check for zero bytes
		if (dwWritten == 0) return ERROR_FILE_OFFLINE;

		//next
		ofs += dwWritten;
		nRemaining -= dwWritten;
	}

	return ERROR_SUCCESS;
}

static IoStatus IoGetFileSize(IoHandle hFile, IoBool bRemoteProtocol, size_t *pSize) {
	IoStatus err = ERROR_SUCCESS;
	DWORD dwSizeLow = 0;

	if (!bRemoteProtocol) {
		//handle points to a real file
		DWORD dwSizeHigh;
		dwSizeLow = GetFileSize(hFile, &dwSizeHigh);

		//not support file size > 4GB
		if (dwSizeHigh != 0) err = ERROR_FILE_TOO_LARGE;
	} else {
		err = IoReadBytes(hFile, &dwSizeLow, sizeof(dwSizeLow));
	}

	//return true size on non-error status
	*pSize = err ? 0 : (size_t) dwSizeLow;
	return err;
}



IoStatus IoReadWholeFileEx(const wchar_t *path, void **pBuffer, unsigned int *pSize) {
	IoStatus err = ERROR_SUCCESS;
	size_t dwSizeLow = 0;
	void *buffer = NULL;
	IoBool bIsHandlePath = IoiIsPathHandle(path);

	IoHandle hFile;

	if (!bIsHandlePath) {
		//open handle
		hFile = (IoHandle) CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	} else {
		//handle is already open
		hFile = IoiHandleFromPath(path);
	}

	//checking the handle
	if (hFile == INVALID_HANDLE_VALUE) {
		err = GetLastError();
		goto Error;
	}

	//getting the file size
	err = IoGetFileSize(hFile, bIsHandlePath, &dwSizeLow);
	if (err) goto Error;

	//allocate the buffer, taking care not to allocate 0 bytes (NULL is an error return)
	buffer = malloc(dwSizeLow == 0 ? 1 : dwSizeLow);
	if (buffer == NULL) {
		err = ERROR_OUTOFMEMORY;
		goto Error;
	}

	//for a pipe or network file, a file read may be incomplete.
	err = IoReadBytes(hFile, buffer, dwSizeLow);
	if (err) goto Error;

Error:
	if (err == ERROR_SUCCESS) {
		//if the user requests to keep the handle after file read, we don't close the handle.
		if (!bIsHandlePath) CloseHandle(hFile);
	} else {
		//free resoures and return error
		if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
		free(buffer);
		buffer = NULL;
		dwSizeLow = 0;
	}

	*pSize = dwSizeLow;
	*pBuffer = buffer;
	return err;
}

void *IoReadWholeFile(const wchar_t *path, unsigned int *size) {
	void *buffer;
	(void) IoReadWholeFileEx(path, &buffer, size);
	return buffer;
}

IoStatus IoWriteWholeFile(const wchar_t *path, const void *buffer, unsigned int size) {
	IoBool bIsHandle = IoiPathStartsWith(path, HANDLE_PATH_PREFIX);

	IoHandle hFile;
	if (!bIsHandle) {
		//file path represents a normal file path
		hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else {
		//file path represents a file handle
		hFile = IoiHandleFromPath(path);
	}

	if (hFile == INVALID_HANDLE_VALUE) {
		return GetLastError();
	}

	IoStatus status = ERROR_SUCCESS;

	if (bIsHandle) {
		DWORD dwSize = size;
		status = IoWriteBytes(hFile, &dwSize, sizeof(dwSize));
		if (status) goto Error;
	}
	status = IoWriteBytes(hFile, buffer, size);
	if (status) goto Error;

Error:
	//close handle if not persistent
	if (!bIsHandle) CloseHandle(hFile);
	return status;
}



// ----- Error routines

wchar_t *IoGetErrorMessage(IoStatus status) {
	//keep the old buffer and free it the next time around (buffer is only good for one call!)
	static wchar_t *buf = NULL;
	if (buf != NULL) LocalFree(buf);
	buf = NULL;

	//format the message
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, (DWORD) status, 0, (LPWSTR) &buf, 0, NULL);
	return buf;
}

