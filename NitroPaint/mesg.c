#include "mesg.h"

LPCWSTR gMesgFormatNames[] = { L"Invalid", L"BMG", NULL };

static void MesgFree(OBJECT_HEADER *header) {
	MesgFile *mesg = (MesgFile *) header;

	for (unsigned int i = 0; i < mesg->nMsg; i++) {
		free(mesg->messages[i].message);
		free(mesg->messages[i].extra);
	}
	free(mesg->messages);
}

void MesgInit(MesgFile *mesg, int format) {
	mesg->header.size = sizeof(MesgFile);
	ObjInit(&mesg->header, FILE_TYPE_MESG, format);

	mesg->header.writer = (OBJECT_WRITER) MesgWrite;
	mesg->header.dispose = MesgFree;
}


unsigned int MesgSingleByteLength(const unsigned char *buf) {
	const unsigned char *start = buf;

	//we scan the string for a null terminator. We must consider the escape sequences.
	while (1) {
		unsigned char c = *(buf++);

		//if the byte is zero and not in an escape sequence, this ends the string.
		if (!c) break;

		if (c == 0x1A) {
			//parse tag
			unsigned int escLen = *(buf++);
			buf += escLen - 2; // 2 byte for escape+length
		}
	}

	return buf - start;
}


unsigned int MesgDoubleByteLength(const unsigned char *buf) {
	const unsigned char *start = buf;

	//we scan the string for a null terminator. We must consider the escape sequences.
	while (1) {
		unsigned char cLo = *(buf++);
		unsigned char cHi = *(buf++);

		//if the byte is zero and not in an escape sequence, this ends the string.
		if (!cLo && !cHi) break;

		uint16_t c = cLo | (cHi << 8);
		if (c == 0x001A) {
			//parse tag
			unsigned int escLen = *(buf++);
			buf += escLen - 3; // 3 byte for escape+length
		}
	}

	return buf - start;
}


static int JFileCheckSignature(const unsigned char *p, const char *signature, int revEndian) {
	//xor source address with 3 for reverse endian check
	unsigned int iXor = revEndian ? 0x3 : 0x0;
	return p[0] == (unsigned char) signature[0 ^ iXor] && p[1] == (unsigned char) signature[1 ^ iXor]
		&& p[2] == (unsigned char) signature[2 ^ iXor] && p[3] == (unsigned char) signature[3 ^ iXor];
}

static int JFileIsValid(const unsigned char *buffer, unsigned int size) {
	if (size < 0x20) return 0; // header size
	unsigned int jSize = *(const uint32_t *) (buffer + 0x08);
	unsigned int nBlock = *(const uint32_t *) (buffer + 0x0C);

	if (jSize < 0x20) return 0; // not big enough for header
	if (jSize > size) return 0; // exceeds file size

	unsigned int offs = 0x20;
	for (unsigned int i = 0; i < nBlock; i++) {
		if (offs >= jSize) return 0;      // out of bounds
		if ((jSize - offs) < 8) return 0; // not enough space for block header

		const unsigned char *block = buffer + offs;
		unsigned int blockSize = *(const uint32_t *) (block + 4);

		if (blockSize < 8) return 0;              // block too small
		if ((jSize - offs) < blockSize) return 0; // block too large

		offs += blockSize;
	}
	return 1;
}

static unsigned char *JFileGetBlockBySignature(const unsigned char *buffer, unsigned int size, const char *signature, int revEndian, unsigned int *pBlockSize) {
	unsigned int nBlock = *(const uint32_t *) (buffer + 0x0C);

	unsigned int offs = 0x20;
	for (unsigned int i = 0; i < nBlock; i++) {
		const unsigned char *block = buffer + offs;
		unsigned int blockSize = *(const uint32_t *) (block + 4);

		if (JFileCheckSignature(block, signature, revEndian)) {
			*pBlockSize = blockSize - 8;
			return (unsigned char *) (buffer + offs + 8);
		}

		offs += blockSize;
	}
	return NULL;
}

static int MesgIsValidBMG(const unsigned char *buffer, unsigned int size) {
	if (!JFileIsValid(buffer, size)) return 0;
	
	//get endianness of the file signatures
	int revEndian = 0;
	if (!JFileCheckSignature(buffer + 0x00, "MESG", 0)) {
		revEndian = 1;
		if (!JFileCheckSignature(buffer + 0x00, "MESG", 1)) return 0; // invalid signature
	}

	if (!JFileCheckSignature(buffer + 0x04, "bmg1", revEndian)) return 0; // file type

	unsigned int encoding = *(const uint8_t *) (buffer + 0x10);
	if (encoding > MESG_ENCODING_UTF8) return 0; // invalid encoding

	unsigned int inf1Size, dat1Size;
	const unsigned char *inf1 = JFileGetBlockBySignature(buffer, size, "INF1", revEndian, &inf1Size);
	const unsigned char *dat1 = JFileGetBlockBySignature(buffer, size, "DAT1", revEndian, &dat1Size);
	if (inf1 == NULL || dat1 == NULL) return 0;

	return 1;
}

int MesgIsValid(const unsigned char *buffer, unsigned int size) {
	if (MesgIsValidBMG(buffer, size)) return MESG_TYPE_BMG;
	return MESG_TYPE_INVALID;
}

static unsigned int MesgGetStringLength(const unsigned char *buf, int encoding) {
	switch (encoding) {
		case MESG_ENCODING_UNDEFINED:
		case MESG_ENCODING_ASCII:
		case MESG_ENCODING_UTF8:
		case MESG_ENCODING_SJIS:
			return MesgSingleByteLength(buf);
		case MESG_ENCODING_UTF16:
			return MesgDoubleByteLength(buf);
	}
	return 0;
}

static int MesgReadBMG(MesgFile *mesg, const unsigned char *buffer, unsigned int size) {
	MesgInit(mesg, MESG_TYPE_BMG);

	//check endianness
	int revEndian = 0;
	if (JFileCheckSignature(buffer, "MESG", 1)) revEndian = 1;

	mesg->encoding = *(const uint8_t *) (buffer + 0x10);

	unsigned int inf1Size, dat1Size, mid1Size;
	const unsigned char *inf1 = JFileGetBlockBySignature(buffer, size, "INF1", revEndian, &inf1Size);
	const unsigned char *dat1 = JFileGetBlockBySignature(buffer, size, "DAT1", revEndian, &dat1Size);
	const unsigned char *mid1 = JFileGetBlockBySignature(buffer, size, "MID1", revEndian, &mid1Size);

	unsigned int nString = *(const uint16_t *) (inf1 + 0x00);
	unsigned int entrySize = *(const uint16_t *) (inf1 + 0x02);
	unsigned int groupID = *(const uint16_t *) (inf1 + 0x04);
	unsigned int colorID = *(const uint8_t *) (inf1 + 0x06);

	//data fields supported:
	// u32 mesgOffset
	// u16 magnification
	// u16 width
	// u16 ID
	// u8  font
	// s8  space
	// u8  line

	//not sure if this is correct
	if ((nString * entrySize + 0x8) > inf1Size) {
		entrySize /= 8;
	}

	//JMSMesgEntry starts with 4byte mesgOffset, extra data follows
	unsigned int nExtra = entrySize - 4;
	mesg->msgExtra = nExtra;

	mesg->groupID = groupID;
	mesg->colorID = colorID;
	
	mesg->nMsg = nString;
	mesg->messages = (MesgEntry *) calloc(nString, sizeof(MesgEntry));
	for (unsigned int i = 0; i < nString; i++) {
		const unsigned char *entry = (inf1 + 0x08 + i * entrySize);
		uint32_t offs = *(const uint32_t *) (entry + 0x0);
		unsigned int len = MesgGetStringLength(dat1 + offs, mesg->encoding);

		MesgEntry *mesgEntry = &mesg->messages[i];
		mesgEntry->id = i;
		mesgEntry->message = (void *) calloc(len, 1);
		mesgEntry->extra = (void *) calloc(nExtra, 1);
		memcpy(mesgEntry->message, dat1 + offs, len);

		//extra
		memcpy(mesgEntry->extra, entry + 4, nExtra);

		//if MID1 exists, we assign IDs to messages.
		if (mid1 != NULL) {
			mesgEntry->id = *(const uint32_t *) (mid1 + 8 + i * 4);
		}
	}

	if (mid1 != NULL) mesg->includeIdMap = 1;

	return OBJ_STATUS_SUCCESS;
}

int MesgRead(MesgFile *mesg, const unsigned char *buffer, unsigned int size) {
	int type = MesgIsValid(buffer, size);
	switch (type) {
		case MESG_TYPE_BMG:
			return MesgReadBMG(mesg, buffer, size);
	}
	return OBJ_STATUS_INVALID;
}

int MesgReadFile(MesgFile *mesg, LPCWSTR path) {
	return ObjReadFile(path, &mesg->header, (OBJECT_READER) MesgRead);
}



static void JFileWriteSignature(BSTREAM *stream, const char *signature, const char *kind, const unsigned char *data, unsigned int len) {
	uint32_t placeholder = 0;
	bstreamWrite(stream, signature, 4);
	bstreamWrite(stream, kind, 4);
	bstreamWrite(stream, &placeholder, sizeof(placeholder));
	bstreamWrite(stream, &placeholder, sizeof(placeholder));
	bstreamWrite(stream, data, len);
	bstreamAlign(stream, 32);
}

static void JFileWriteBlock(BSTREAM *stream, const char *kind, const void *data, unsigned int size) {
	//header
	uint32_t sizeField = (size + 8 + 0x1F) & ~0x1F;
	bstreamWrite(stream, kind, 4);
	bstreamWrite(stream, &sizeField, sizeof(sizeField));
	bstreamWrite(stream, data, size);

	//if the size was aligned, pad to alignment
	bstreamAlign(stream, 32);
}


static int MesgWriteBMG(MesgFile *mesg, BSTREAM *stream) {
	unsigned char headerData = (unsigned char) mesg->encoding;
	JFileWriteSignature(stream, "MESG", "bmg1", &headerData, sizeof(headerData));

	//check the message order
	int inOrder = 1;
	for (unsigned int i = 1; i < mesg->nMsg; i++) {
		if (mesg->messages[i].id <= mesg->messages[i - 1].id) inOrder = 0;
	}

	//TODO: ID format
	int idFormat = 0; // lo=32, hi=0

	//create string buffer
	unsigned int entrySize = (sizeof(uint32_t) + mesg->msgExtra + 0x3) & ~0x3;
	unsigned int inf1Size = 0x8 + mesg->nMsg * entrySize;
	unsigned char *inf1 = (unsigned char *) calloc(inf1Size, 1);
	*(uint16_t *) (inf1 + 0x00) = mesg->nMsg;
	*(uint16_t *) (inf1 + 0x02) = 4 + mesg->msgExtra;
	*(uint16_t *) (inf1 + 0x04) = mesg->groupID;
	*(uint8_t *) (inf1 + 0x06) = mesg->colorID;
	*(uint8_t *) (inf1 + 0x07) = 0;

	//create ID buffer
	unsigned int mid1Size = 0x8 + 4 * mesg->nMsg;
	unsigned char *mid1 = (unsigned char *) calloc(mid1Size, 1);
	*(uint16_t *) (mid1 + 0x00) = mesg->nMsg;
	*(uint8_t *) (mid1 + 0x02) = inOrder ? 0x10 : 0x00;
	*(uint8_t *) (mid1 + 0x03) = idFormat;
	*(uint32_t *) (mid1 + 0x04) = 0;

	BSTREAM stmDat1;
	bstreamCreate(&stmDat1, NULL, 0);

	//put an empty string (MessageEditor does this)
	unsigned char empty[2] = { 0 };
	bstreamWrite(&stmDat1, empty, MesgGetStringLength(empty, mesg->encoding));

	//append strings in order (TODO: consider combining common suffixes?)
	for (unsigned int i = 0; i < mesg->nMsg; i++) {
		MesgEntry *ent = &mesg->messages[i];
		unsigned char *inf1Ent = inf1 + 0x8 + (i * entrySize);

		//put INF1
		uint32_t strOffset = stmDat1.size;
		*(uint32_t *) (inf1Ent + 0x0) = strOffset;
		memcpy(inf1Ent + 4, ent->extra, mesg->msgExtra);

		//put MID1
		*(uint32_t *) (mid1 + 0x8 + i * 4) = ent->id;

		unsigned int len = MesgGetStringLength(ent->message, mesg->encoding);
		bstreamWrite(&stmDat1, ent->message, len);
	}

	JFileWriteBlock(stream, "INF1", inf1, inf1Size);
	JFileWriteBlock(stream, "DAT1", stmDat1.buffer, stmDat1.size);

	unsigned int nBlock = 2;
	if (mesg->includeIdMap) {
		JFileWriteBlock(stream, "MID1", mid1, mid1Size);
		nBlock++;
	}

	uint32_t size32 = stream->size, nBlock32 = nBlock;
	bstreamSeek(stream, 0x8, 0);
	bstreamWrite(stream, &size32, sizeof(size32));
	bstreamWrite(stream, &nBlock32, sizeof(nBlock32));

	free(mid1);
	free(inf1);
	bstreamFree(&stmDat1);
	return OBJ_STATUS_SUCCESS;
}

int MesgWrite(MesgFile *mesg, BSTREAM *stream) {
	switch (mesg->header.format) {
		case MESG_TYPE_BMG:
			return MesgWriteBMG(mesg, stream);
	}
	return OBJ_STATUS_INVALID;
}

int MesgWriteFile(MesgFile *mesg, LPCWSTR path) {
	return ObjWriteFile(&mesg->header, path);
}
