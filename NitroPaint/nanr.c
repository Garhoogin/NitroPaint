#include <Windows.h>

#include "ncer.h"
#include "nanr.h"
#include "ncgr.h"
#include "nclr.h"
#include "nns.h"

LPCWSTR cellAnimationFormatNames[] = { L"Invalid", L"NANR", L"Ghost Trick" };

static int AnmiOffsetComparator(const void *p1, const void *p2) {
	uint32_t ofs1 = *(uint32_t *) p1;
	uint32_t ofs2 = *(uint32_t *) p2;
	if (ofs1 < ofs2) return -1;
	if (ofs1 > ofs2) return 1;
	return 0;
}

int AnmIsValidGhostTrick(const unsigned char *buffer, unsigned int size) {
	//must be >0 and a multiple of 8
	if (size < 8 || (size & 7)) return 0;

	//scan offset:size pairs. Offsets must be a multiple of 4, and sizes must be nonzero
	int nEntries = size / 8;
	for (int i = 0; i < nEntries; i++) {
		uint32_t offset = *(uint32_t *) (buffer + i * 8 + 0);
		uint32_t seglen = *(uint32_t *) (buffer + i * 8 + 4);

		if (seglen == 0) return 0;
		if (offset & 3) return 0;
		if (seglen & 3) return 0;
	}

	//ensure no overlapping segments, aside from those that overlap entirely, and that all segments are accounted for
	uint32_t (*copy)[2] = (uint32_t (*)[2]) calloc(nEntries, sizeof(uint32_t [2]));
	memcpy(copy, buffer, nEntries * sizeof(*copy));
	qsort(copy, nEntries, sizeof(*copy), AnmiOffsetComparator);

	uint32_t lastOffset = 0, lastSize = 0;
	int valid = 1;
	for (int i = 0; i < nEntries; i++) {
		uint32_t offset = copy[i][0];
		uint32_t seglen = copy[i][1];
		if (i == 0) {
			//lowest offset: must be 0
			if (offset > 0) {
				valid = 0;
				break;
			}
			lastOffset = offset;
			lastSize = seglen;
			continue;
		}

		//not the first segment. If the offset equals last offset, size must match too.
		if (offset == lastOffset) {
			if (seglen != lastSize) {
				valid = 0;
				break;
			}
		}

		//else, check that the offset equals the last offset plus the last size.
		if (offset != lastOffset) {
			if (offset != lastOffset + lastSize) {
				valid = 0;
				break;
			}
		}

		//set last
		lastOffset = offset;
		lastSize = seglen;
	}

	free(copy);
	return valid;
}

int AnmIsValidNanr(const unsigned char *buffer, unsigned int size) {
	if (!NnsG2dIsValid(buffer, size)) return 0;
	if (memcmp(buffer, "RNAN", 4) != 0) return 0;

	const unsigned char *abnk = NnsG2dGetSectionByMagic(buffer, size, 'ABNK');
	if (abnk == NULL) abnk = NnsG2dGetSectionByMagic(buffer, size, 'KNBA');
	if (abnk == NULL) return 0;
	return 1;
}

int AnmIdentify(const unsigned char *buffer, unsigned int size) {
	if (AnmIsValidNanr(buffer, size)) return NANR_TYPE_NANR;
	if (AnmIsValidGhostTrick(buffer, size)) return NANR_TYPE_GHOSTTRICK;
	return NANR_TYPE_INVALID;
}

void AnmInit(NANR *nanr, int format) {
	nanr->header.size = sizeof(NANR);
	ObjInit(&nanr->header, FILE_TYPE_NANR, format);
}

int AnmReadNanr(NANR *nanr, const unsigned char *buffer, unsigned int size) {
	const unsigned char *abnk = buffer + 0x10;
	int nSections = *(unsigned short *) (buffer + 0xE);
	int nSequences = *(unsigned short *) (abnk + 8);
	int nTotalFrames = *(unsigned short *) (abnk + 0xA);
	int sequenceArrayOffset = *(int *) (abnk + 0xC) + 0x18;
	int frameArrayOffset = *(int *) (abnk + 0x10) + 0x18;
	int animationOffset = *(int *) (abnk + 0x14) + 0x18;

	NANR_SEQUENCE *sequenceArray = (NANR_SEQUENCE *) (buffer + sequenceArrayOffset);
	NANR_SEQUENCE *sequences = (NANR_SEQUENCE *) calloc(nSequences, sizeof(NANR_SEQUENCE));
	memcpy(sequences, sequenceArray, nSequences * sizeof(NANR_SEQUENCE));
	int elementSizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };

	for (int i = 0; i < nSequences; i++) {

		int framesOffs = frameArrayOffset + (int) sequences[i].frames;
		int animType = sequences[i].type;
		int element = animType & 0xFFFF;
		int elementSize = elementSizes[element];

		FRAME_DATA *frameDataSrc = (FRAME_DATA *) (buffer + framesOffs);
		FRAME_DATA *frameData = (FRAME_DATA *) calloc(sequences[i].nFrames, sizeof(FRAME_DATA));
		memcpy(frameData, frameDataSrc, sequences[i].nFrames * sizeof(FRAME_DATA));
		sequences[i].frames = frameData;

		for (int j = 0; j < sequences[i].nFrames; j++) {

			framesOffs = (int) frameData[j].animationData;
			void *frameSrc = (void *) (buffer + animationOffset + framesOffs);
			void *frame = calloc(1, elementSize);
			memcpy(frame, frameSrc, elementSize);
			frameData[j].animationData = (void *) frame;
		}
	}

	nanr->header.compression = COMPRESSION_NONE;
	nanr->header.format = NANR_TYPE_NANR;
	nanr->header.size = sizeof(*nanr);
	nanr->header.type = FILE_TYPE_NANR;

	DWORD lablSize = 0, uextSize = 0;
	void *labl = NULL, *uext = NULL;
	if (nSections > 1) {
		const unsigned char *ptr = buffer + 0x10;
		ptr += *(DWORD *) (ptr + 4);
		nSections--;

		for (int i = 0; i < nSections; i++) {
			DWORD magic = *(DWORD *) ptr;
			DWORD size = *(DWORD *) (ptr + 4) - 8;
			ptr += 8;
			if (magic == 'LABL' || magic == 'LBAL') {
				lablSize = size;
				labl = malloc(size);
				memcpy(labl, ptr, size);
			} else if (magic == 'UEXT' || magic == 'TXEU') {
				uextSize = size;
				uext = malloc(size);
				memcpy(uext, ptr, size);
			}
			ptr += size;
		}
	}

	nanr->sequences = sequences;
	nanr->nSequences = nSequences;
	nanr->labl = labl;
	nanr->lablSize = lablSize;
	nanr->uext = uext;
	nanr->uextSize = uextSize;

	return 0;
}

int AnmReadGhostTrick(NANR *nanr, const unsigned char *buffer, unsigned int size) {
	//Ghost Trick files contain only one sequence. Read it here.
	AnmInit(nanr, NANR_TYPE_GHOSTTRICK);
	nanr->nSequences = 1;
	nanr->sequences = (NANR_SEQUENCE *) calloc(1, sizeof(NANR_SEQUENCE));
	
	//get frames
	int nFrames = size / 8;

	NANR_SEQUENCE *seq = &nanr->sequences[0];
	seq->nFrames = nFrames;
	seq->frames = (FRAME_DATA *) calloc(nFrames, sizeof(FRAME_DATA));
	seq->startFrameIndex = 0;
	seq->mode = 1; //forward
	seq->type = 0 | (1 << 16); //Cell anim Index

	//read frames
	for (int i = 0; i < nFrames; i++) {
		ANIM_DATA *animData = (ANIM_DATA *) calloc(1, sizeof(ANIM_DATA));
		animData->index = i;
		seq->frames[i].nFrames = 1;
		seq->frames[i].animationData = animData;
	}

	//convert transfers into transfer indices
	uint32_t (*copy)[2] = (uint32_t (*)[2]) calloc(nFrames, sizeof(uint32_t [2]));
	memcpy(copy, buffer, nFrames * sizeof(*copy));
	qsort(copy, nFrames, sizeof(*copy), AnmiOffsetComparator);
	for (int i = 0; i < nFrames; i++) {
		if (i == 0) copy[i][1] = 0;
		else {
			if (copy[i][0] == copy[i - 1][0]) copy[i][1] = copy[i - 1][1];
			else copy[i][1] = copy[i - 1][1] + 1;
		}
	}

	//VRAM transfer operations
	nanr->seqVramTransfers = (int **) calloc(1, sizeof(int *));
	nanr->seqVramTransfers[0] = (int *) calloc(nFrames, sizeof(int));
	for (int i = 0; i < nFrames; i++) {
		nanr->seqVramTransfers[0][i] = copy[i][1];
	}

	free(copy);
	return 0;
}

int AnmRead(NANR *nanr, const unsigned char *buffer, unsigned int size) {
	int type = AnmIdentify(buffer, size);
	switch (type) {
		case NANR_TYPE_NANR:
			return AnmReadNanr(nanr, buffer, size);
		case NANR_TYPE_GHOSTTRICK:
			return AnmReadGhostTrick(nanr, buffer, size);
	}
	return 1;
}

int AnmReadFile(NANR *nanr, LPCWSTR path) {
	return ObjReadFile(path, (OBJECT_HEADER *) nanr, (OBJECT_READER) AnmRead);
}

int nanrCountFrames(NANR *nanr) {
	int nFrames = 0;
	for (int i = 0; i < nanr->nSequences; i++) {
		nFrames += nanr->sequences[i].nFrames;
	}
	return nFrames;
}

//ensure frame is in the stream, and return the offset to it
int nanrWriteFrame(BSTREAM *stream, void *data, int element) {
	int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };
	int size = sizes[element];

	//search for element
	int found = 0, foundOffset = 0, i = 0;
	switch (element) {
		case 0:
		case 1:
			//both Index and Index+SRT, compare whole animation dat
			for (i = 0; i <= stream->size - size; i += 2) { //2-byte alignment
				if (memcmp(stream->buffer + i, data, size) == 0) {
					found = 1;
					foundOffset = i;
					break;
				}
			}
			break;
		case 2:
		{
			//Index+T: compare all but padding
			ANIM_DATA_T *d1 = (ANIM_DATA_T *) data;
			for (i = 0; i <= stream->size - size; i += 4) { //4-byte alignment
				ANIM_DATA_T *d2 = (ANIM_DATA_T *) (stream->buffer + i);
				if (d1->index == d2->index && d1->px == d2->px && d1->py == d2->py) {
					found = 1;
					foundOffset = i;
					break;
				}
			}
			break;
		}
	}

	//return found offset
	if (found) {
		return foundOffset;
	}

	uint32_t pad0 = 0;
	if (element != 0 && (stream->pos & 3)) {
		//align
		bstreamWrite(stream, &pad0, 4 - (stream->pos & 3));
	}

	//TODO: search
	int ofs = stream->pos;
	bstreamWrite(stream, data, size);
	return ofs;
}

int AnmWrite(NANR *nanr, BSTREAM *stream) {
	int status = 0;

	int fileStart = stream->pos;
	BYTE nanrHeader[] = { 'R', 'N', 'A', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 0, 0 };
	WORD nSections = 1 + (nanr->labl != NULL) + (nanr->uext != NULL);
	DWORD sequencesSize = nanr->nSequences * sizeof(NANR_SEQUENCE);

	*(uint16_t *) (nanrHeader + 0xE) = nSections;
	bstreamWrite(stream, nanrHeader, sizeof(nanrHeader));

	uint32_t align0 = 0; //0-padding for alignment

	int abnkStart = stream->pos;
	int nFrames = nanrCountFrames(nanr);
	int animFramesSize = nFrames * sizeof(FRAME_DATA);
	unsigned char abnkHeader[] = { 'K', 'N', 'B', 'A', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint32_t *) (abnkHeader + 4) = 0;
	*(uint16_t *) (abnkHeader + 8) = nanr->nSequences;
	*(uint16_t *) (abnkHeader + 0xA) = nFrames;
	*(uint32_t *) (abnkHeader + 0xC) = 0x18;
	*(uint32_t *) (abnkHeader + 0x10) = 0x18 + sequencesSize;
	*(uint32_t *) (abnkHeader + 0x14) = 0x18 + sequencesSize + animFramesSize;
	bstreamWrite(stream, abnkHeader, sizeof(abnkHeader));

	int seqOffset = stream->pos;
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE seq;
		memcpy(&seq, nanr->sequences + i, sizeof(NANR_SEQUENCE));
		seq.frames = (FRAME_DATA *) (i * sizeof(FRAME_DATA));
		bstreamWrite(stream, &seq, sizeof(seq));
	}

	BSTREAM frameStream;
	bstreamCreate(&frameStream, NULL, 0);

	int frameDataOffset = stream->pos;
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		FRAME_DATA *frames = sequence->frames;

		int element = sequence->type & 0xFFFF;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };

		//write frame data offset
		int pos = stream->pos;
		int thisSequenceOffset = pos - frameDataOffset;
		bstreamSeek(stream, seqOffset + i * sizeof(NANR_SEQUENCE) + 0xC, 0); //frame offset in this sequence
		bstreamWrite(stream, &thisSequenceOffset, sizeof(thisSequenceOffset));
		bstreamSeek(stream, pos, 0);

		for (int j = 0; j < sequence->nFrames; j++) {
			int frameOfs = nanrWriteFrame(&frameStream, frames[j].animationData, element);

			FRAME_DATA frame;
			memcpy(&frame, frames + j, sizeof(FRAME_DATA));
			frame.animationData = (void *) frameOfs;
			bstreamWrite(stream, &frame, sizeof(frame));
		}
	}

	//need alignment?
	if (frameStream.pos & 3) {
		int nPad = 4 - (frameStream.pos & 3);
		bstreamWrite(&frameStream, &align0, nPad);
	}

	bstreamWrite(stream, frameStream.buffer, frameStream.size);
	bstreamFree(&frameStream);

	int abnkEnd = stream->pos;
	int abnkSize = abnkEnd - abnkStart;

	bstreamSeek(stream, abnkStart + 4, 0);
	bstreamWrite(stream, &abnkSize, sizeof(abnkSize));
	bstreamSeek(stream, abnkEnd, 0);

	if (nanr->labl != NULL) {
		BYTE lablHeader[] = { 'L', 'B', 'A', 'L', 0, 0, 0, 0 };
		*(DWORD *) (lablHeader + 4) = 8 + nanr->lablSize;
		bstreamWrite(stream, lablHeader, sizeof(lablHeader));
		bstreamWrite(stream, nanr->labl, nanr->lablSize);
	}

	if (nanr->uext != NULL) {
		BYTE uextHeader[] = { 'T', 'X', 'E', 'U', 0, 0, 0, 0 };
		*(DWORD *) (uextHeader + 4) = 8 + nanr->uextSize;
		bstreamWrite(stream, uextHeader, sizeof(uextHeader));
		bstreamWrite(stream, nanr->uext, nanr->uextSize);
	}

	int fileEnd = stream->pos;
	int fileSize = fileEnd - fileStart;
	bstreamSeek(stream, fileStart + 8, 0);
	bstreamWrite(stream, &fileSize, sizeof(fileSize));
	bstreamSeek(stream, fileEnd, 0);

	return status;
}

int AnmWriteFile(NANR *nanr, LPWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) nanr, (OBJECT_WRITER) AnmWrite);
}

void AnmFree(NANR *nanr) {
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		for (int j = 0; j < sequence->nFrames; j++) {
			FRAME_DATA *f = sequence->frames + j;
			free(f->animationData);
		}
		free(sequence->frames);
	}
	if (nanr->seqVramTransfers != NULL) {
		for (int i = 0; i < nanr->nSequences; i++) {
			if (nanr->seqVramTransfers[i] != NULL) free(nanr->sequences);
		}
		free(nanr->seqVramTransfers);
	}
	free(nanr->sequences);
}
