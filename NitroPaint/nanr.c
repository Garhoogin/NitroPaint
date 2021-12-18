#include <Windows.h>

#include "ncer.h"
#include "nanr.h"
#include "ncgr.h"
#include "nclr.h"

LPCWSTR cellAnimationFormatNames[] = { L"Invalid", L"NANR" };

int nanrIsValid(LPBYTE lpFile, int size) {
	if (size < 16) return 0;
	DWORD dwMagic = *(DWORD *) lpFile;
	if (dwMagic != 'NANR' && dwMagic != 'RNAN') return 0;
	return 1;
}

int nanrRead(NANR *nanr, LPBYTE buffer, int size) {
	if (lz77IsCompressed(buffer, size)) {
		int uncompressedSize;
		char *bf = lz77decompress(buffer, size, &uncompressedSize);
		int r = nanrRead(nanr, bf, uncompressedSize);
		free(bf);
		nanr->header.compression = COMPRESSION_LZ77;
		return r;
	}
	if (!nanrIsValid(buffer, size)) return 1;

	LPBYTE abnk = buffer + 0x10;
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
		LPBYTE ptr = buffer + 0x10;
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

int nanrReadFile(NANR *nanr, LPWSTR path) {
	return fileRead(path, (OBJECT_HEADER *) nanr, (OBJECT_READER) nanrRead);
}

DWORD nanrGetAbnkSize(NANR *nanr, int *pAnimFramesSize, int *pFrameDataSize) {
	int nSequences = nanr->nSequences;
	int sequencesSize = nSequences * sizeof(NANR_SEQUENCE);
	int animFramesSize = 0;
	int frameDataSize = 0;
	for (int i = 0; i < nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		int element = sequence->type & 0xFFFF;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };

		for (int j = 0; j < sequence->nFrames; j++) {
			animFramesSize += sizeof(FRAME_DATA);
			frameDataSize += sizes[element];
		}
	}

	if (pAnimFramesSize != NULL) *pAnimFramesSize = animFramesSize;
	if (pFrameDataSize != NULL) *pFrameDataSize = frameDataSize;

	return sequencesSize + animFramesSize + frameDataSize;
}

int nanrCountFrames(NANR *nanr) {
	int nFrames = 0;
	for (int i = 0; i < nanr->nSequences; i++) {
		nFrames += nanr->sequences[i].nFrames;
	}
	return nFrames;
}

int nanrWrite(NANR *nanr, BSTREAM *stream) {
	int status = 0;

	BYTE nanrHeader[] = { 'R', 'N', 'A', 'N', 0xFF, 0xFE, 0, 1, 0, 0, 0, 0, 0x10, 0, 0, 0 };
	WORD nSections = 1 + (nanr->labl != NULL) + (nanr->uext != NULL);
	DWORD animFramesSize, frameDataSize;
	DWORD abnkSize = 24 + nanrGetAbnkSize(nanr, &animFramesSize, &frameDataSize);
	DWORD sequencesSize = nanr->nSequences * sizeof(NANR_SEQUENCE);

	DWORD fileSize = 0x18 + abnkSize + (nanr->labl != NULL ? (8 + nanr->lablSize) : 0) +
		(nanr->uext != NULL ? (8 + nanr->uextSize) : 0);
	*(DWORD *) (nanrHeader + 8) = fileSize;
	*(WORD *) (nanrHeader + 0xE) = nSections;
	bstreamWrite(stream, nanrHeader, sizeof(nanrHeader));


	BYTE abnkHeader[] = { 'K', 'N', 'B', 'A', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(DWORD *) (abnkHeader + 4) = abnkSize + 8;
	*(WORD *) (abnkHeader + 8) = nanr->nSequences;
	*(WORD *) (abnkHeader + 0xA) = nanrCountFrames(nanr);
	*(DWORD *) (abnkHeader + 0xC) = 0x18;
	*(DWORD *) (abnkHeader + 0x10) = 0x18 + sequencesSize;
	*(DWORD *) (abnkHeader + 0x14) = 0x18 + sequencesSize + animFramesSize;
	bstreamWrite(stream, abnkHeader, sizeof(abnkHeader));

	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE seq;
		memcpy(&seq, nanr->sequences + i, sizeof(NANR_SEQUENCE));
		seq.frames = (FRAME_DATA *) (i * sizeof(FRAME_DATA));
		bstreamWrite(stream, &seq, sizeof(seq));
	}

	DWORD currentAnimationDataPos = 0;
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		FRAME_DATA *frames = sequence->frames;

		int element = sequence->type & 0xFFFF;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };
		for (int j = 0; j < sequence->nFrames; j++) {
			FRAME_DATA frame;
			memcpy(&frame, frames + j, sizeof(FRAME_DATA));
			frame.animationData = (void *) currentAnimationDataPos;
			currentAnimationDataPos += sizes[element];
			bstreamWrite(stream, &frame, sizeof(frame));
		}
	}

	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		FRAME_DATA *frames = sequence->frames;

		int element = sequence->type & 0xFFFF;
		int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_SRT) };
		for (int j = 0; j < sequence->nFrames; j++) {
			bstreamWrite(stream, frames[j].animationData, sizes[element]);
		}
	}


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

	return status;
}

int nanrWriteFile(NANR *nanr, LPWSTR name) {
	return fileWrite(name, (OBJECT_HEADER *) nanr, (OBJECT_WRITER) nanrWrite);
}

void nanrFree(NANR *nanr) {
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		for (int j = 0; j < sequence->nFrames; j++) {
			FRAME_DATA *f = sequence->frames + j;
			free(f->animationData);
		}
		free(sequence->frames);
	}
	free(nanr->sequences);
}