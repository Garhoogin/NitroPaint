#include "mesg.h"

static int MesgIsValidBMG(const unsigned char *buffer, unsigned int size);

static void MesgFree(ObjHeader *header);

static void MesgRegisterFormat(int format, const wchar_t *name, ObjIdFlag flag, ObjIdProc proc) {
	ObjRegisterFormat(FILE_TYPE_MESG, format, name, flag, proc);
}

void MesgRegisterFormats(void) {
	ObjRegisterType(FILE_TYPE_MESG, sizeof(MesgFile), L"Message", (ObjReader) MesgRead, (ObjWriter) MesgWrite, NULL, MesgFree);
	MesgRegisterFormat(MESG_TYPE_BMG, L"BMG", OBJ_ID_HEADER | OBJ_ID_SIGNATURE | OBJ_ID_CHUNKED | OBJ_ID_OFFSETS | OBJ_ID_VALIDATED, MesgIsValidBMG);
}


#define JMSG_INVALID 0  // invalid message file
#define JMSG_LE      1  // message file little endian
#define JMSG_BE      2  // message file big endidan

static void MesgFree(ObjHeader *header) {
	MesgFile *mesg = (MesgFile *) header;

	for (unsigned int i = 0; i < mesg->nMsg; i++) {
		free(mesg->messages[i].message);
		free(mesg->messages[i].extra);
	}
	free(mesg->messages);
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


static uint32_t JFileReadEndian32(const unsigned char *p, int endian) {
	switch (endian) {
		case JMSG_LE:
			return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
		case JMSG_BE:
			return (p[3] << 0) | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
		default:
			return 0;
	}
}

static uint16_t JFileReadEndian16(const unsigned char *p, int endian) {
	switch (endian) {
		case JMSG_LE:
			return (p[0] << 0) | (p[1] << 8);
		case JMSG_BE:
			return (p[1] << 0) | (p[0] << 8);
		default:
			return 0;
	}
}

static int JFileCheckSignature(const unsigned char *p, const char *signature, int revEndian) {
	//xor source address with 3 for reverse endian check
	unsigned int iXor = revEndian ? 0x3 : 0x0;
	return p[0] == (unsigned char) signature[0 ^ iXor] && p[1] == (unsigned char) signature[1 ^ iXor]
		&& p[2] == (unsigned char) signature[2 ^ iXor] && p[3] == (unsigned char) signature[3 ^ iXor];
}

static int JFileIsValidEndian(const unsigned char *buffer, unsigned int size, int endian) {
	if (size < 0x20) return 0; // header size
	unsigned int jSize = JFileReadEndian32(buffer + 0x08, endian);
	unsigned int nBlock = JFileReadEndian32(buffer + 0x0C, endian);

	if (jSize < 0x20) return 0; // not big enough for header
	if (jSize > size) return 0; // exceeds file size

	unsigned int offs = 0x20;
	for (unsigned int i = 0; i < nBlock; i++) {
		if (offs >= jSize) return 0;      // out of bounds
		if ((jSize - offs) < 8) return 0; // not enough space for block header

		const unsigned char *block = buffer + offs;
		unsigned int blockSize = JFileReadEndian32(block + 4, endian);

		if (blockSize < 8) return 0;              // block too small
		if ((jSize - offs) < blockSize) return 0; // block too large

		offs += blockSize;
	}
	return 1;
}

static int JFileIsValid(const unsigned char *buffer, unsigned int size) {
	if (JFileIsValidEndian(buffer, size, JMSG_LE)) return JMSG_LE;
	if (JFileIsValidEndian(buffer, size, JMSG_BE)) return JMSG_BE;
	return JMSG_INVALID;
}

static unsigned char *JFileGetBlockBySignature(const unsigned char *buffer, unsigned int size, const char *signature, int revEndianBlocks, int endian, unsigned int *pBlockSize) {
	unsigned int nBlock = JFileReadEndian32(buffer + 0x0C, endian);

	unsigned int offs = 0x20;
	for (unsigned int i = 0; i < nBlock; i++) {
		const unsigned char *block = buffer + offs;
		unsigned int blockSize = JFileReadEndian32(block + 4, endian);

		if (JFileCheckSignature(block, signature, revEndianBlocks)) {
			*pBlockSize = blockSize - 8;
			return (unsigned char *) (buffer + offs + 8);
		}

		offs += blockSize;
	}
	return NULL;
}

static int MesgIsValidBMG(const unsigned char *buffer, unsigned int size) {
	int type = JFileIsValid(buffer, size);
	if (type == JMSG_INVALID) return 0;
	
	//get endianness of the file signatures
	int revEndianSignatures = 0;
	if (!JFileCheckSignature(buffer + 0x00, "MESG", 0)) {
		revEndianSignatures = 1;
		if (!JFileCheckSignature(buffer + 0x00, "MESG", 1)) return 0; // invalid signature
	}

	if (!JFileCheckSignature(buffer + 0x04, "bmg1", revEndianSignatures)) return 0; // file type

	unsigned int encoding = *(const uint8_t *) (buffer + 0x10);
	if (encoding > MESG_ENCODING_UTF8) return 0; // invalid encoding

	unsigned int inf1Size, dat1Size;
	const unsigned char *inf1 = JFileGetBlockBySignature(buffer, size, "INF1", revEndianSignatures, type, &inf1Size);
	const unsigned char *dat1 = JFileGetBlockBySignature(buffer, size, "DAT1", revEndianSignatures, type, &dat1Size);
	if (inf1 == NULL || dat1 == NULL) return 0;

	return 1;
}

int MesgIsValid(const unsigned char *buffer, unsigned int size) {
	int fmt;
	ObjIdentifyExByType(buffer, size, FILE_TYPE_MESG, &fmt);
	return fmt;
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
	//check endianness
	int endian = JFileIsValid(buffer, size);
	int revEndianSignatures = 0;
	if (JFileCheckSignature(buffer, "MESG", 1)) revEndianSignatures = 1;

	mesg->encoding = *(const uint8_t *) (buffer + 0x10);

	unsigned int inf1Size, dat1Size, mid1Size;
	const unsigned char *inf1 = JFileGetBlockBySignature(buffer, size, "INF1", revEndianSignatures, endian, &inf1Size);
	const unsigned char *dat1 = JFileGetBlockBySignature(buffer, size, "DAT1", revEndianSignatures, endian, &dat1Size);
	const unsigned char *mid1 = JFileGetBlockBySignature(buffer, size, "MID1", revEndianSignatures, endian, &mid1Size);

	unsigned int nString = JFileReadEndian16(inf1 + 0x00, endian);
	unsigned int entrySize = JFileReadEndian16(inf1 + 0x02, endian);
	unsigned int groupID = JFileReadEndian16(inf1 + 0x04, endian);
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
	mesg->endian = endian;

	mesg->groupID = groupID;
	mesg->colorID = colorID;
	
	mesg->nMsg = nString;
	mesg->messages = (MesgEntry *) calloc(nString, sizeof(MesgEntry));
	for (unsigned int i = 0; i < nString; i++) {
		const unsigned char *entry = (inf1 + 0x08 + i * entrySize);
		uint32_t offs = JFileReadEndian32(entry + 0x0, endian);
		unsigned int len = MesgGetStringLength(dat1 + offs, mesg->encoding);

		MesgEntry *mesgEntry = &mesg->messages[i];
		mesgEntry->id = i;
		mesgEntry->message = (void *) calloc(len, 1);
		mesgEntry->extra = (void *) calloc(nExtra, 1);
		memcpy(mesgEntry->message, dat1 + offs, len);

		//if the file is in big endian, ensure the message is read in the correct byte order.
		if (endian == JMSG_BE && mesg->encoding == MESG_ENCODING_UTF16) {
			uint16_t *pmsg = mesgEntry->message;
			for (unsigned int i = 0; i < len; i += 2) {
				//TODO: process tags
				pmsg[i] = JFileReadEndian16((const unsigned char *) &pmsg[i], endian);
			}
		}

		//extra
		memcpy(mesgEntry->extra, entry + 4, nExtra);

		//if MID1 exists, we assign IDs to messages.
		if (mid1 != NULL) {
			mesgEntry->id = JFileReadEndian32(mid1 + 8 + i * 4, endian);
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



static void JFileWriteEndian32(unsigned char *p, uint32_t x, int endian) {
	switch (endian) {
		case JMSG_LE:
			p[0] = (x >>  0) & 0xFF;
			p[1] = (x >>  8) & 0xFF;
			p[2] = (x >> 16) & 0xFF;
			p[3] = (x >> 24) & 0xFF;
			break;
		case JMSG_BE:
			p[3] = (x >>  0) & 0xFF;
			p[2] = (x >>  8) & 0xFF;
			p[1] = (x >> 16) & 0xFF;
			p[0] = (x >> 24) & 0xFF;
			break;
	}
}

static void JFileWriteEndian16(unsigned char *p, uint16_t x, int endian) {
	switch (endian) {
		case JMSG_LE:
			p[0] = (x >> 0) & 0xFF;
			p[1] = (x >> 8) & 0xFF;
			break;
		case JMSG_BE:
			p[1] = (x >> 0) & 0xFF;
			p[0] = (x >> 8) & 0xFF;
			break;
	}
}

static void JFileWriteEndianStream(BSTREAM *stream, uint32_t x, int nBit, int endian) {
	unsigned char buf[4];
	switch (nBit) {
		case 32: JFileWriteEndian32(buf, x, endian); break;
		case 16: JFileWriteEndian16(buf, x, endian); break;
		case 8: buf[0] = (unsigned char) x; break;
	}
	bstreamWrite(stream, buf, nBit / 8);
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

static void JFileWriteBlock(BSTREAM *stream, const char *kind, const void *data, unsigned int size, int endian) {
	//header
	uint32_t sizeField = (size + 8 + 0x1F) & ~0x1F;
	bstreamWrite(stream, kind, 4);
	JFileWriteEndianStream(stream, sizeField, 32, endian);
	bstreamWrite(stream, data, size);

	//if the size was aligned, pad to alignment
	bstreamAlign(stream, 32);
}



typedef struct MesgSortEntry_ {
	MesgEntry *entry;
	int encoding;
	int i;
} MesgSortEntry;

static unsigned int MesgWriteString(BSTREAM *stmMesg, const void *mesg, int encoding) {
	unsigned int length = MesgGetStringLength(mesg, encoding);

	//search for string in the stream already
	unsigned int pitch = 1;
	if (encoding == MESG_ENCODING_UTF16) pitch = 2;

	for (unsigned int i = 0; i < stmMesg->size && (stmMesg->size - i) >= length; i += pitch) {
		//find
		if (memcmp(stmMesg->buffer + i, mesg, length) == 0) return i;
	}
	
	//put
	unsigned int pos = stmMesg->size;
	bstreamWrite(stmMesg, mesg, length);
	return pos;
}

static int MesgLengthComparator(const void *e1, const void *e2) {
	const MesgSortEntry *m1 = (const MesgSortEntry *) e1;
	const MesgSortEntry *m2 = (const MesgSortEntry *) e2;
	unsigned int l1 = MesgGetStringLength(m1->entry->message, m1->encoding);
	unsigned int l2 = MesgGetStringLength(m2->entry->message, m2->encoding);

	//sort descending by length
	if (l1 < l2) return  1;
	if (l1 > l2) return -1;

	//sort ascending by index
	if (m1->i < m2->i) return -1;
	if (m1->i > m2->i) return  1;
	return 0;
}

static void MesgWriteStrings(MesgFile *mesg, unsigned int entrySize, unsigned char *inf1, unsigned char *mid1, BSTREAM *stmDat1, int endian) {
	//sort strings by length
	MesgSortEntry *sorted = (MesgSortEntry *) calloc(mesg->nMsg, sizeof(MesgSortEntry));
	for (unsigned int i = 0; i < mesg->nMsg; i++) {
		sorted[i].i = i;
		sorted[i].encoding = mesg->encoding;
		sorted[i].entry = &mesg->messages[i];
	}
	qsort(sorted, mesg->nMsg, sizeof(MesgSortEntry), MesgLengthComparator);

	for (unsigned int i_ = 0; i_ < mesg->nMsg; i_++) {
		MesgEntry *ent = sorted[i_].entry;
		unsigned int i = sorted[i_].i;
		unsigned char *inf1Ent = inf1 + 0x8 + (i * entrySize);

		//put DAT1
		uint32_t strOffset = MesgWriteString(stmDat1, ent->message, mesg->encoding);

		//put INF1
		JFileWriteEndian32(inf1Ent + 0x0, strOffset, endian);
		memcpy(inf1Ent + 4, ent->extra, mesg->msgExtra);

		//put MID1
		JFileWriteEndian32(mid1 + 0x8 + i * 4, ent->id, endian);
	}

	free(sorted);
}


static int MesgWriteBMG(MesgFile *mesg, BSTREAM *stream) {
	int endian = mesg->endian;
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
	JFileWriteEndian16(inf1 + 0x00, mesg->nMsg, endian);
	JFileWriteEndian16(inf1 + 0x02, 4 + mesg->msgExtra, endian);
	JFileWriteEndian16(inf1 + 0x04, mesg->groupID, endian);
	*(uint8_t *) (inf1 + 0x06) = mesg->colorID;
	*(uint8_t *) (inf1 + 0x07) = 0;

	//create ID buffer
	unsigned int mid1Size = 0x8 + 4 * mesg->nMsg;
	unsigned char *mid1 = (unsigned char *) calloc(mid1Size, 1);
	JFileWriteEndian16(mid1 + 0x00, mesg->nMsg, endian);
	*(uint8_t *) (mid1 + 0x02) = inOrder ? 0x10 : 0x00;
	*(uint8_t *) (mid1 + 0x03) = idFormat;
	*(uint32_t *) (mid1 + 0x04) = 0;

	BSTREAM stmDat1;
	bstreamCreate(&stmDat1, NULL, 0);

	//put an empty string (MessageEditor does this)
	//unsigned char empty[2] = { 0 };
	//bstreamWrite(&stmDat1, empty, MesgGetStringLength(empty, mesg->encoding));

	//append strings in order (TODO: consider combining common suffixes?)
	MesgWriteStrings(mesg, entrySize, inf1, mid1, &stmDat1, endian);

	JFileWriteBlock(stream, "INF1", inf1, inf1Size, endian);
	JFileWriteBlock(stream, "DAT1", stmDat1.buffer, stmDat1.size, endian);

	unsigned int nBlock = 2;
	if (mesg->includeIdMap) {
		JFileWriteBlock(stream, "MID1", mid1, mid1Size, endian);
		nBlock++;
	}

	uint32_t size32 = stream->size, nBlock32 = nBlock;
	bstreamSeek(stream, 0x8, 0);
	JFileWriteEndianStream(stream, size32, 32, endian);
	JFileWriteEndianStream(stream, nBlock32, 32, endian);

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
