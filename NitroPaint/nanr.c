#include <Windows.h>
#include <math.h>

#include "ncer.h"
#include "nanr.h"
#include "ncgr.h"
#include "nclr.h"
#include "nns.h"

#define FX32_ONE               4096
#define FX32_HALF              (FX32_ONE/2)
#define FX32_FROM_F32(x)       ((int)(((x)<0.0f)?((x)*FX32_ONE+0.5f):((x)*FX32_ONE-0.5f)))

#define RAD_0DEG               0.00000000000000000
#define RAD_22_5DEG            0.39269908169872415
#define RAD_45DEG              0.78539816339744831
#define RAD_90DEG              1.57079632679489662
#define RAD_180DEG             3.14159265358979323
#define RAD_360DEG             6.28318530717958648

LPCWSTR cellAnimationFormatNames[] = { L"Invalid", L"NANR", L"Ghost Trick", NULL };

static int AnmiOffsetComparator(const void *p1, const void *p2) {
	uint32_t ofs1 = *(uint32_t *) p1;
	uint32_t ofs2 = *(uint32_t *) p2;
	if (ofs1 < ofs2) return -1;
	if (ofs1 > ofs2) return 1;
	return 0;
}

static void AnmiConvertSequence(NANR_SEQUENCE *seq, int newType) {
	int type = seq->type & 0xFFFF;
	if (type == newType) return;

	for (int i = 0; i < seq->nFrames; i++) {
		FRAME_DATA *data = &seq->frames[i];
		void *orig = data->animationData;

		ANIM_DATA_SRT srt = { 0 };
		srt.sx = 4096; // FX32_CONST(1.0)
		srt.sy = 4096; // FX32_CONST(1.0)

		switch (type) {
			case NANR_SEQ_TYPE_INDEX_SRT:
			{
				memcpy(&srt, orig, sizeof(srt));
				break;
			}
			case NANR_SEQ_TYPE_INDEX_T:
			{
				ANIM_DATA_T *frm = (ANIM_DATA_T *) orig;
				srt.index = frm->index;
				srt.px = frm->px;
				srt.py = frm->py;
				break;
			}
			case NANR_SEQ_TYPE_INDEX:
			{
				ANIM_DATA *frm = (ANIM_DATA *) orig;
				srt.index = frm->index;
				break;
			}
		}

		//write
		const int elementSizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };
		void *newdata = malloc(elementSizes[newType]);

		switch (newType) {
			case NANR_SEQ_TYPE_INDEX:
			{
				ANIM_DATA *frm = (ANIM_DATA *) newdata;
				frm->index = srt.index;
				break;
			}
			case NANR_SEQ_TYPE_INDEX_T:
			{
				ANIM_DATA_T *frm = (ANIM_DATA_T *) newdata;
				frm->index = srt.index;
				frm->px = srt.px;
				frm->py = srt.py;
				frm->pad_ = 0xBEEF;
				break;
			}
			case NANR_SEQ_TYPE_INDEX_SRT:
			{
				memcpy(newdata, &srt, sizeof(srt));
				break;
			}
		}

		data->animationData = newdata;
		free(orig);
	}
	
	seq->type = (seq->type & ~0xFFFF) | newType;
}

static void AnmiConvertSequenceToSRT(NANR_SEQUENCE *seq) {
	AnmiConvertSequence(seq, NANR_SEQ_TYPE_INDEX_SRT);
}

static int AnmiGetCompressedSequenceType(NANR_SEQUENCE *seq) {
	//find the lowest required data storage to represent the sequence.
	int type = seq->type & 0xFFFF;
	if (type == NANR_SEQ_TYPE_INDEX) return NANR_SEQ_TYPE_INDEX; // best representation

	int hasTranslation = 0;
	for (int i = 0; i < seq->nFrames; i++) {
		FRAME_DATA *frm = &seq->frames[i];

		ANIM_DATA_SRT srt = { 0 };
		switch (type) {
			case NANR_SEQ_TYPE_INDEX_SRT:
			{
				memcpy(&srt, frm->animationData, sizeof(srt));
				break;
			}
			case NANR_SEQ_TYPE_INDEX_T:
			{
				ANIM_DATA_T *d = (ANIM_DATA_T *) frm->animationData;
				srt.sx = 4096;
				srt.sy = 4096;
				srt.rotZ = 0;
				srt.px = d->px;
				srt.py = d->py;
				break;
			}
		}

		//if scale or rotate not default, return SRT
		if (srt.sx != 4096) return NANR_SEQ_TYPE_INDEX_SRT;
		if (srt.sy != 4096) return NANR_SEQ_TYPE_INDEX_SRT;
		if (srt.rotZ != 0) return NANR_SEQ_TYPE_INDEX_SRT;

		if (srt.px != 0 || srt.py != 0) hasTranslation = 1;
	}

	//if we have translation, use Index+T. Oterwise use index.
	if (hasTranslation) return NANR_SEQ_TYPE_INDEX_T;
	return NANR_SEQ_TYPE_INDEX;
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

	const unsigned char *abnk = NnsG2dFindBlockBySignature(buffer, size, "ABNK", NNS_SIG_LE, NULL);
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
	nanr->header.dispose = AnmFree;
	nanr->header.writer = (OBJECT_WRITER) AnmWrite;
}

int AnmReadNanr(NANR *nanr, const unsigned char *buffer, unsigned int size) {
	AnmInit(nanr, NANR_TYPE_NANR);

	const unsigned char *abnk = NnsG2dFindBlockBySignature(buffer, size, "ABNK", NNS_SIG_LE, NULL);

	int nSequences = *(uint16_t *) (abnk + 0);
	int nTotalFrames = *(uint16_t *) (abnk + 0x2);
	int sequenceArrayOffset = *(uint32_t *) (abnk + 0x4);
	int frameArrayOffset = *(uint32_t *) (abnk + 0x8);
	int animationOffset = *(uint32_t *) (abnk + 0xC);

	NANR_SEQUENCE *sequenceArray = (NANR_SEQUENCE *) (abnk + sequenceArrayOffset);
	NANR_SEQUENCE *sequences = (NANR_SEQUENCE *) calloc(nSequences, sizeof(NANR_SEQUENCE));
	memcpy(sequences, sequenceArray, nSequences * sizeof(NANR_SEQUENCE));
	const int elementSizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };

	for (int i = 0; i < nSequences; i++) {

		int framesOffs = frameArrayOffset + (int) sequences[i].frames;
		int animType = sequences[i].type;
		int element = animType & 0xFFFF;
		int elementSize = elementSizes[element];

		FRAME_DATA *frameDataSrc = (FRAME_DATA *) (abnk + framesOffs);
		FRAME_DATA *frameData = (FRAME_DATA *) calloc(sequences[i].nFrames, sizeof(FRAME_DATA));
		memcpy(frameData, frameDataSrc, sequences[i].nFrames * sizeof(FRAME_DATA));
		sequences[i].frames = frameData;

		for (int j = 0; j < sequences[i].nFrames; j++) {

			framesOffs = (int) frameData[j].animationData;
			void *frameSrc = (void *) (abnk + animationOffset + framesOffs);
			void *frame = calloc(1, elementSize);
			memcpy(frame, frameSrc, elementSize);
			frameData[j].animationData = (void *) frame;
		}
	}

	unsigned int lablSize = 0, uextSize = 0;
	const unsigned char *labl = NnsG2dFindBlockBySignature(buffer, size, "LABL", NNS_SIG_LE, &lablSize);
	const unsigned char *uext = NnsG2dFindBlockBySignature(buffer, size, "UEXT", NNS_SIG_LE, &uextSize);

	if (labl != NULL) {
		nanr->lablSize = lablSize;
		nanr->labl = malloc(nanr->lablSize);
		memcpy(nanr->labl, labl, nanr->lablSize);
	}
	if (uext != NULL) {
		nanr->uextSize = uextSize;
		nanr->uext = malloc(nanr->uextSize);
		memcpy(nanr->uext, uext, nanr->uextSize);
	}

	nanr->sequences = sequences;
	nanr->nSequences = nSequences;

	//convert sequence data to Index+SRT
	for (int i = 0; i < nanr->nSequences; i++) {
		AnmiConvertSequenceToSRT(&nanr->sequences[i]);
	}

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

static int AnmiCountFrames(NANR *nanr) {
	int nFrames = 0;
	for (int i = 0; i < nanr->nSequences; i++) {
		nFrames += nanr->sequences[i].nFrames;
	}
	return nFrames;
}

//ensure frame is in the stream, and return the offset to it
static unsigned int AnmiNanrWriteFrame(BSTREAM *stream, const void *data, int element) {
	const unsigned int sizes[] = { sizeof(ANIM_DATA), sizeof(ANIM_DATA_SRT), sizeof(ANIM_DATA_T) };
	unsigned int size = sizes[element];

	//search for element
	int found = 0;
	unsigned int i, foundOffset = 0;
	switch (element) {
		case NANR_SEQ_TYPE_INDEX:
		case NANR_SEQ_TYPE_INDEX_SRT:
			//both Index and Index+SRT, compare whole animation dat
			for (i = 0; i <= stream->size - size; i += 2) { //2-byte alignment
				if (memcmp(stream->buffer + i, data, size) == 0) {
					found = 1;
					foundOffset = i;
					break;
				}
			}
			break;
		case NANR_SEQ_TYPE_INDEX_T:
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
	if (element != NANR_SEQ_TYPE_INDEX && (stream->pos & 3)) {
		//align stream before writing data out
		bstreamWrite(stream, &pad0, 4 - (stream->pos & 3));
	}

	//TODO: search
	unsigned int ofs = stream->pos;
	bstreamWrite(stream, data, size);
	return ofs;
}

int AnmWrite(NANR *nanr, BSTREAM *stream) {
	int status = 0;

	NnsStream nnsStream;
	NnsStreamCreate(&nnsStream, "NANR", 1, 0, NNS_TYPE_G2D, NNS_SIG_LE);
	uint32_t sequencesSize = nanr->nSequences * sizeof(NANR_SEQUENCE);

	int nFrames = AnmiCountFrames(nanr);
	int animFramesSize = nFrames * sizeof(FRAME_DATA);

	//convert frame data to minimal representation
	int *seqTypes = (int *) calloc(nanr->nSequences, sizeof(int));
	for (int i = 0; i < nanr->nSequences; i++) {
		seqTypes[i] = nanr->sequences[i].type & 0xFFFF;

		int newtype = AnmiGetCompressedSequenceType(&nanr->sequences[i]);
		AnmiConvertSequence(&nanr->sequences[i], newtype);
	}

	NnsStreamStartBlock(&nnsStream, "ABNK");
	unsigned char abnkHeader[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	*(uint16_t *) (abnkHeader + 0x00) = nanr->nSequences;
	*(uint16_t *) (abnkHeader + 0x02) = nFrames;
	*(uint32_t *) (abnkHeader + 0x04) = sizeof(abnkHeader);
	*(uint32_t *) (abnkHeader + 0x08) = sizeof(abnkHeader) + sequencesSize;
	*(uint32_t *) (abnkHeader + 0x0C) = sizeof(abnkHeader) + sequencesSize + animFramesSize;
	NnsStreamWrite(&nnsStream, abnkHeader, sizeof(abnkHeader));

	BSTREAM *blockStream = NnsStreamGetBlockStream(&nnsStream);
	int seqOffset = blockStream->pos;
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE seq;
		memcpy(&seq, nanr->sequences + i, sizeof(NANR_SEQUENCE));
		seq.frames = (FRAME_DATA *) (i * sizeof(FRAME_DATA));
		NnsStreamWrite(&nnsStream, &seq, sizeof(seq));
	}

	BSTREAM frameStream;
	bstreamCreate(&frameStream, NULL, 0);

	int frameDataOffset = blockStream->pos;
	for (int i = 0; i < nanr->nSequences; i++) {
		NANR_SEQUENCE *sequence = nanr->sequences + i;
		FRAME_DATA *frames = sequence->frames;

		//get minimal representation and convert
		int element = sequence->type & 0xFFFF;

		//write frame data offset
		int pos = blockStream->pos;
		int thisSequenceOffset = pos - frameDataOffset;
		bstreamSeek(blockStream, seqOffset + i * sizeof(NANR_SEQUENCE) + 0xC, 0); //frame offset in this sequence
		bstreamWrite(blockStream, &thisSequenceOffset, sizeof(thisSequenceOffset));
		bstreamSeek(blockStream, pos, 0);

		for (int j = 0; j < sequence->nFrames; j++) {
			int frameOfs = AnmiNanrWriteFrame(&frameStream, frames[j].animationData, element);

			FRAME_DATA frame;
			memcpy(&frame, frames + j, sizeof(FRAME_DATA));
			frame.animationData = (void *) frameOfs;
			bstreamWrite(blockStream, &frame, sizeof(frame));
		}
	}

	//restore sequence types
	for (int i = 0; i < nanr->nSequences; i++) {
		AnmiConvertSequence(&nanr->sequences[i], seqTypes[i]);
	}
	free(seqTypes);

	//need alignment?
	bstreamAlign(blockStream, 4);

	NnsStreamWrite(&nnsStream, frameStream.buffer, frameStream.size);
	NnsStreamEndBlock(&nnsStream);
	bstreamFree(&frameStream);

	if (nanr->labl != NULL) {
		NnsStreamStartBlock(&nnsStream, "LABL");
		NnsStreamWrite(&nnsStream, nanr->labl, nanr->lablSize);
		NnsStreamEndBlock(&nnsStream);
	}

	if (nanr->uext != NULL) {
		NnsStreamStartBlock(&nnsStream, "UEXT");
		NnsStreamWrite(&nnsStream, nanr->uext, nanr->uextSize);
		NnsStreamEndBlock(&nnsStream);
	}
	NnsStreamFinalize(&nnsStream);
	NnsStreamFlushOut(&nnsStream, stream);
	NnsStreamFree(&nnsStream);

	return status;
}

int AnmWriteFile(NANR *nanr, LPWSTR name) {
	return ObjWriteFile(name, (OBJECT_HEADER *) nanr, (OBJECT_WRITER) AnmWrite);
}

void AnmFree(OBJECT_HEADER *obj) {
	NANR *nanr = (NANR *) obj;
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
			if (nanr->seqVramTransfers[i] != NULL) free(nanr->seqVramTransfers[i]);
		}
		free(nanr->seqVramTransfers);
	}
	free(nanr->sequences);
}



// ----- animation routines

static int FloatToInt(double x) {
	return (int) (x + (x < 0.0f ? -0.5f : 0.5f));
}

int AnmGetAnimFrame(
	NANR          *nanr,
	int            iSeq,
	int            iFrm,
	ANIM_DATA_SRT *pFrame,
	int           *pDuration
) {
	if (pFrame != NULL) memset(pFrame, 0, sizeof(ANIM_DATA_SRT));
	if (pDuration != NULL) *pDuration = 0;
	if (iSeq < 0 || iSeq >= nanr->nSequences) return 0;

	NANR_SEQUENCE *seq = &nanr->sequences[iSeq];
	if (iFrm < 0 || iFrm >= seq->nFrames) return 0;

	FRAME_DATA *frame = &seq->frames[iFrm];
	void *frameData = frame->animationData;

	if (pDuration != NULL) *pDuration = frame->nFrames;

	if (pFrame != NULL) {
		switch (seq->type & 0xFFFF) {
			case NANR_SEQ_TYPE_INDEX:
			{
				//convert Index to Index+SRT
				ANIM_DATA *dat = (ANIM_DATA *) frameData;
				pFrame->index = dat->index;
				pFrame->px = 0;
				pFrame->py = 0;
				pFrame->sx = FX32_ONE; // identity scale
				pFrame->sy = FX32_ONE; // identity scale
				pFrame->rotZ = 0;      // no rotation
				break;
			}
			case NANR_SEQ_TYPE_INDEX_T:
			{
				//convert Index+T to Index+SRT
				ANIM_DATA_T *dat = (ANIM_DATA_T *) frameData;
				pFrame->index = dat->index;
				pFrame->px = dat->px;
				pFrame->py = dat->py;
				pFrame->sx = FX32_ONE; // identity scale
				pFrame->sy = FX32_ONE; // identity scale
				pFrame->rotZ = 0;      // no rotation
				break;
			}
			case NANR_SEQ_TYPE_INDEX_SRT:
			{
				//copy Index+SRT
				ANIM_DATA_SRT *dat = (ANIM_DATA_SRT *) frameData;
				memcpy(pFrame, dat, sizeof(ANIM_DATA_SRT));
				break;
			}
			default:
				return 0;
		}
	}

	return 1;
}

void AnmCalcTransformMatrix(
	double  centerX,     // center point X of transformation
	double  centerY,     // center point Y of transformation
	double  scaleX,      // 1. scale X
	double  scaleY,      // 1. scale Y
	double  rotZ,        // 2. rotation (radians)
	double  transX,      // 3. translation X
	double  transY,      // 3. translation Y
	double *pMtx,        // -> output transformation matrix
	double *pTrans       // -> output translation vector
) {
	double mtx[2][2];
	double trans[2];

	if (rotZ == 0.0f) {
		//no rotation
		mtx[0][0] = scaleX;
		mtx[0][1] = 0.0f;
		mtx[1][0] = 0.0f;
		mtx[1][1] = scaleY;

		trans[0] = centerX * (1.0f - scaleX) + transX;
		trans[1] = centerY * (1.0f - scaleY) + transY;
	} else {
		//with rotation
		double sinR = sin(rotZ);
		double cosR = cos(rotZ);

		mtx[0][0] = scaleX * cosR;
		mtx[0][1] = -scaleY * sinR;
		mtx[1][0] = scaleX * sinR;
		mtx[1][1] = scaleY * cosR;

		trans[0] = centerX * (1.0f - scaleX * cosR) + scaleY * centerY * sinR + transX;
		trans[1] = centerY * (1.0f - scaleY * cosR) - scaleX * centerX * sinR + transY;
	}

	//output matrix
	memcpy(pMtx, mtx, sizeof(mtx));
	memcpy(pTrans, trans, sizeof(trans));
}

void AnmRenderSequenceFrame(COLOR32 *dest, NANR *nanr, NCER *ncer, NCGR *ncgr, NCLR *nclr, int iSeq, int iFrm, int x, int y, int forceAffine, int forceDoubleSize) {
	if (nanr == NULL || ncer == NULL) return;

	//get frame data
	ANIM_DATA_SRT frm;
	if (!AnmGetAnimFrame(nanr, iSeq, iFrm, &frm, NULL)) return;
	if (frm.index < 0 || frm.index >= ncer->nCells) return;

	//get referenced cell
	NCER_CELL *cell = &ncer->cells[frm.index];
	double sx = frm.sx / 4096.0;
	double sy = frm.sy / 4096.0;
	double rot = (frm.rotZ / 65536.0) * RAD_360DEG;

	double mtx[2][2] = { { 1.0, 0.0 }, { 0.0, 1.0 } }, trans[2] = { 0 };
	AnmCalcTransformMatrix(0.0, 0.0, sx, sy, rot, (double) frm.px, (double) frm.py, &mtx[0][0], trans);
	CellRender(dest, NULL, ncer, ncgr, nclr, frm.index, cell,
		FloatToInt(trans[0]) + x, FloatToInt(trans[1]) + y,
		mtx[0][0], mtx[0][1], mtx[1][0], mtx[1][1],
		forceAffine, forceDoubleSize);
}

