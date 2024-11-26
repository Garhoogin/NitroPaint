#include <string.h>

#include "setosa.h"

void SetStreamCreate(SetStream *stream) {
	stream->nBlocks = 0;
	bstreamCreate(&stream->headerStream, NULL, 0);
	bstreamCreate(&stream->blockStream, NULL, 0);

	//prepare header
	unsigned char header[8] = { 'R', 'S', 'R', 'C', 0, 0, 0, 0 };
	bstreamWrite(&stream->headerStream, header, sizeof(header));
}

void SetStreamStartBlock(SetStream *stream, const char *sig) {
	//append block starting position
	uint32_t pos = stream->blockStream.size;
	bstreamWrite(&stream->headerStream, &pos, sizeof(pos));

	bstreamCreate(&stream->currentStream, NULL, 0);
	bstreamWrite(&stream->currentStream, sig, 4);
	stream->nBlocks++;
}

void SetStreamWrite(SetStream *stream, const void *data, unsigned int size) {
	bstreamWrite(&stream->currentStream, (void *) data, size);
}

void SetStreamEndBlock(SetStream *stream) {
	bstreamAlign(&stream->currentStream, 4);
	bstreamWrite(&stream->blockStream, stream->currentStream.buffer, stream->currentStream.size);
	bstreamFree(&stream->currentStream);
}

void SetStreamFinalize(SetStream *stream) {
	//finalize: write number of blocks to header
	*(uint32_t *) (stream->headerStream.buffer + 4) = stream->nBlocks;
	for (unsigned int i = 0; i < stream->nBlocks; i++) {
		((uint32_t *) (stream->headerStream.buffer + 8))[i] += stream->headerStream.size;
	}
}

void SetStreamFlushOut(SetStream *stream, BSTREAM *out) {
	bstreamWrite(out, stream->headerStream.buffer, stream->headerStream.size);
	bstreamWrite(out, stream->blockStream.buffer, stream->blockStream.size);
}

void SetStreamFree(SetStream *stream) {
	bstreamFree(&stream->headerStream);
	bstreamFree(&stream->blockStream);
}




static uint16_t SetResHash(const char *name) {
	uint16_t hash = 0;
	unsigned int len = strlen(name);

	while (*name) {
		hash ^= *name;
		hash = (hash >> 5) | (hash << 11);
		hash += (hash >> 8);
		name++;
	}

	return hash ^ len;
}

void SetResDirCreate(SetResDirectory *dir, int hasNames) {
	dir->hasNames = hasNames;
	dir->nObjects = 0;
	bstreamCreate(&dir->dirStream, NULL, 0);
	bstreamCreate(&dir->objStream, NULL, 0);
	bstreamCreate(&dir->nameStream, NULL, 0);

	//initialize directory structure
	uint16_t dirHeader[2] = { 0 };
	bstreamWrite(&dir->dirStream, dirHeader, sizeof(dirHeader));
}

void SetResDirAdd(SetResDirectory *dir, const char *name, const void *data, unsigned int dataSize) {
	//add to string blob
	uint32_t offsName = dir->nameStream.size;
	if (dir->hasNames) {
		int len = strlen(name);
		bstreamWrite(&dir->nameStream, name, len);
	}

	//add to directory entries
	unsigned char entry[0xC] = { 0 };
	if (dir->hasNames) {
		*(uint32_t *) (entry + 0x0) = offsName;
		*(uint16_t *) (entry + 0x4) = strlen(name);
		*(uint16_t *) (entry + 0x6) = SetResHash(name);
	} else {
		*(uint32_t *) (entry + 0x0) = dir->nObjects;
		*(uint16_t *) (entry + 0x4) = 0;
		*(uint16_t *) (entry + 0x6) = dir->nObjects;
	}
	*(uint32_t *) (entry + 0x8) = dir->objStream.size;
	bstreamWrite(&dir->dirStream, entry, sizeof(entry));

	//add to object blob
	bstreamWrite(&dir->objStream, (void *) data, dataSize);
	bstreamAlign(&dir->objStream, 4);

	dir->nObjects++;
}

static int SetResDirComparator(const void *e1, const void *e2) {
	const unsigned char *entry1 = (const unsigned char *) e1;
	const unsigned char *entry2 = (const unsigned char *) e2;
	uint16_t hash1 = *(uint16_t *) (entry1 + 0x6);
	uint16_t hash2 = *(uint16_t *) (entry2 + 0x6);
	if (hash1 < hash2) return -1;
	if (hash1 > hash2) return 1;
	return 0;
}

void SetResDirFinalize(SetResDirectory *dir) {
	//align data blocks
	bstreamAlign(&dir->objStream, 4);
	bstreamAlign(&dir->nameStream, 4);

	//sort directory entries
	if (dir->hasNames) {
		qsort(dir->dirStream.buffer, dir->nObjects, 0xC, SetResDirComparator);
	}

	//fix up offsets
	for (int i = 0; i < dir->nObjects; i++) {
		unsigned char *dirData = dir->dirStream.buffer + 4 + i * 0xC;
		if (dir->hasNames) {
			*(uint32_t *) (dirData + 0x0) += dir->dirStream.size + dir->objStream.size;
		}
		*(uint32_t *) (dirData + 0x8) += dir->dirStream.size;
	}

	//write directory header fields
	*(uint16_t *) (dir->dirStream.buffer + 0x0) = dir->nObjects;
	*(uint16_t *) (dir->dirStream.buffer + 0x2) = (!dir->hasNames << 0);
}

void SetResDirFlushOut(SetResDirectory *dir, SetStream *out) {
	//write directory header
	uint16_t header[2] = { 0 };
	header[0] = dir->nObjects;
	header[1] = dir->hasNames ? 0 : 1;

	//write entries
	bstreamWrite(&out->currentStream, dir->dirStream.buffer, dir->dirStream.size);

	//write data
	bstreamWrite(&out->currentStream, dir->objStream.buffer, dir->objStream.size);

	//write names
	bstreamWrite(&out->currentStream, dir->nameStream.buffer, dir->nameStream.size);
	bstreamAlign(&out->currentStream, 4);
}

void SetResDirFree(SetResDirectory *dir) {
	bstreamFree(&dir->dirStream);
	bstreamFree(&dir->objStream);
	bstreamFree(&dir->nameStream);
}
