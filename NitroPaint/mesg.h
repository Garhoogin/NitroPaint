#pragma once

#include "filecommon.h"

#define MESG_TYPE_INVALID 0
#define MESG_TYPE_BMG     1

#define MESG_ENCODING_UNDEFINED 0
#define MESG_ENCODING_ASCII     1
#define MESG_ENCODING_UTF16     2
#define MESG_ENCODING_SJIS      3
#define MESG_ENCODING_UTF8      4

typedef struct MesgEntry_ {
	void *message; // message text
	int id;        // message ID
	void *extra;   // extra parameter
} MesgEntry;

typedef struct MesgFile_ {
	OBJECT_HEADER header;

	MesgEntry *messages;
	unsigned int nMsg;
	unsigned int msgExtra;

	int encoding;

	int groupID;
	int colorID;

	int revEndian;

	int includeIdMap;
} MesgFile;

extern LPCWSTR gMesgFormatNames[];


unsigned int MesgSingleByteLength(const unsigned char *buf);
unsigned int MesgDoubleByteLength(const unsigned char *buf);


int MesgIsValid(const unsigned char *buffer, unsigned int size);

int MesgRead(MesgFile *mesg, const unsigned char *buffer, unsigned int size);

int MesgReadFile(MesgFile *mesg, LPCWSTR path);

int MesgWrite(MesgFile *mesg, BSTREAM *stream);

int MesgWriteFile(MesgFile *mesg, LPCWSTR path);

