#include <Windows.h>
#include <CommCtrl.h>

#include "resource.h"
#include "mesgeditor.h"

#define MESG_TAG_COLOR  0
#define MESG_TAG_SIZE   1
#define MESG_TAG_RUBY   2
#define MESG_TAG_FONT   3
#define MESG_TAG_RUBYEX 7

static void MesgEditorSetAssociatedFontEditor(MesgEditorData *data, NFTRVIEWERDATA *nftrViewerData);
static void MesgEditorAddFontEditor(MesgEditorData *data, NFTRVIEWERDATA *nftrViewerData);

static void StrAddString(BSTREAM *stream, const wchar_t *str) {
	bstreamWrite(stream, str, wcslen(str) * sizeof(wchar_t));
}

static void StrAddBytes(BSTREAM *stream, const unsigned char *mbs, unsigned int len) {
	bstreamWrite(stream, mbs, len);
}

static void StrAddChar(BSTREAM *stream, wchar_t c) {
	bstreamWrite(stream, &c, sizeof(c));
}

static unsigned int MesgEditorFeedChar(const unsigned char **ppos, unsigned char *out, int encoding) {
	const unsigned char *in = *ppos;

	unsigned int nCopy = 1;
	switch (encoding) {
		case MESG_ENCODING_UNDEFINED:
		case MESG_ENCODING_ASCII:
			nCopy = 1;
			break;
		case MESG_ENCODING_UTF16:
			nCopy = 2;
			break;
		case MESG_ENCODING_SJIS:
			if ((*in >= 0x81 && *in < 0xA0) || (*in >= 0xE0)) nCopy++;
			break;
		case MESG_ENCODING_UTF8:
			if ((in[0] & 0x80) == 0x00) nCopy = 1;
			else if ((in[0] & 0xE0) == 0xC0) nCopy = 2;
			else nCopy = 3;
			break;
	}

	memcpy(out, in, nCopy);
	out[nCopy] = '\0'; // terminator
	*ppos = in + nCopy;

	return nCopy;
}

static int MesgEditorEncodingToCodePage(int encoding) {
	switch (encoding) {
		default:
		case MESG_ENCODING_ASCII:
		case MESG_ENCODING_UNDEFINED:
			return 1252;
		case MESG_ENCODING_UTF8:
			return CP_UTF8;
		case MESG_ENCODING_SJIS:
			return 932;
		case MESG_ENCODING_UTF16:
			return 0;
	}
}

static wchar_t MesgEditorFeedCharToUnicode(const unsigned char **mesg, int encoding) {
	int inCP = MesgEditorEncodingToCodePage(encoding);

	unsigned char mbsTmp[4];
	unsigned int nByteMbs = MesgEditorFeedChar(mesg, mbsTmp, encoding);

	wchar_t tmp[2];
	if (encoding != MESG_ENCODING_UTF16) {
		MultiByteToWideChar(inCP, MB_PRECOMPOSED, mbsTmp, nByteMbs, tmp, 2);
	} else {
		tmp[0] = mbsTmp[0] | (mbsTmp[1] << 8);
	}

	//first char is decoded
	return tmp[0];
}

static unsigned int MesgEditorFeedCharFromUnicode(wchar_t c, int encoding, unsigned char *pMbs) {
	int outCP = MesgEditorEncodingToCodePage(encoding);

	if (encoding != MESG_ENCODING_UTF16) {
		if (c == L'\0') {
			//special case for null terminator
			*pMbs = '\0';
			return 1;
		} else {
			//convert from Unicode
			BOOL useDefChar;
			CHAR defChar = '?'; // TODO
			return WideCharToMultiByte(outCP, 0, &c, 1, pMbs, 4, &defChar, &useDefChar);
		}
	} else {
		pMbs[0] = (c >> 0) & 0xFF;
		pMbs[1] = (c >> 8) & 0xFF;
		return 2; // 2byte fixed
	}
}

static void MesgEditorWriteEscapedChar(BSTREAM *stream, wchar_t cc, int inTag, int escapeChars) {
	//if not escaping characters just write it directly
	if (!escapeChars) {
		StrAddChar(stream, cc);
		return;
	}

	switch (cc) {
		case L'\n': StrAddString(stream, L"\r\n"); break; // for Windows
		case L'\r': StrAddString(stream, L"\\r"); break;
		case L'\t': StrAddString(stream, L"\\t"); break;

		case L':':
		case L']': 
		{
			if (inTag) {
		case L'[':
		case L'\\':
				StrAddChar(stream, L'\\');
			}
		}
		default   : StrAddChar(stream, cc); break;
	}
}

static void MesgEditorExpandTag(BSTREAM *stream, unsigned int tagGroup, unsigned int tag, unsigned int len, const unsigned char *data, int encoding, int expandSystemTags) {
	wchar_t temp[32];

	unsigned int szChar = 1;
	if (encoding == MESG_ENCODING_UTF16) szChar = 2;

	//if expanding system tags, check for recognized tags from the message editor.
	if (expandSystemTags && tagGroup == 0xFF) {
		switch (tag) {
			case MESG_TAG_COLOR:
				if (len >= 1) {
					wsprintfW(temp, L"Color:%d", data[0]);
					StrAddString(stream, temp);
					return;
				}
				break;
			case MESG_TAG_SIZE:
				if (len >= 2) {
					wsprintfW(temp, L"Size:%d", data[0] | (data[1] << 8));
					StrAddString(stream, temp);
					return;
				}
				break;
			case MESG_TAG_RUBY:
				if (len >= 1) {
					unsigned int baselen = *(data++);
					unsigned int rubylen = len - 1;

					wsprintfW(temp, L"Ruby:%d:", baselen);
					StrAddString(stream, temp);

					const unsigned char *rubyEnd = data + rubylen;
					while ((data + szChar) <= rubyEnd) {
						wchar_t wc = MesgEditorFeedCharToUnicode(&data, encoding);
						if (wc == L'\0') break; // reach padding

						MesgEditorWriteEscapedChar(stream, wc, 1, 0);
					}
					return;
				}
				break;
			case MESG_TAG_FONT:
				if (len >= 1) {
					wsprintfW(temp, L"Font:%d", data[0]);
					StrAddString(stream, temp);
					return;
				}
				break;
			case MESG_TAG_RUBYEX:
			{
				unsigned int info = *(data++);
				unsigned int baselen = *(data++);
				unsigned int rubylen = len - 2;

				wsprintfW(temp, L"RubyEx:%02X:%d:", info, baselen);
				StrAddString(stream, temp);

				const unsigned char *rubyEnd = data + rubylen;
				while ((data + szChar) <= rubyEnd) {
					wchar_t wc = MesgEditorFeedCharToUnicode(&data, encoding);
					if (wc == L'\0') break; // reach padding

					MesgEditorWriteEscapedChar(stream, wc, 1, 0);
				}
				return;
			}
		}
	}
	
	//no applicable mnemonic
	wsprintfW(temp, L"%X:%X:", tagGroup, tag);
	StrAddString(stream, temp);

	//write tag data
	for (unsigned int i = 0; i < len; i++) {
		wsprintfW(temp, L"%02X", *(data++));
		StrAddString(stream, temp);
	}
}

static wchar_t *MesgEditorMultiByteToWideChar(const unsigned char *mesg, int encoding, int inlinePreview, int decodeSystemTags, int escapeChars) {
	//count of bytes remaining
	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);

	//parse one character at a time
	while (1) {
		const unsigned char *charStart = mesg;
		wchar_t cc = MesgEditorFeedCharToUnicode(&mesg, encoding);
		
		//if the decoded code point is the sub character, we decode a tag.
		switch (cc) {
			case L'\x1A':
			{
				unsigned int esclen = *(mesg++);
				esclen -= (mesg - charStart) + 3; // tag overhead

				unsigned int tagGroup = *(mesg++);
				unsigned int tag = *(mesg++);
				tag |= *(mesg++) << 8;

				StrAddChar(&stream, L'[');

				MesgEditorExpandTag(&stream, tagGroup, tag, esclen, mesg, encoding, decodeSystemTags);
				mesg += esclen;

				//end tag
				StrAddChar(&stream, L']');
				break;
			}
			default:
			{
				if (!inlinePreview) {
					MesgEditorWriteEscapedChar(&stream, cc, 0, escapeChars);
				} else {
					//preview will turn whitespace into space character for list preview
					if (cc <= ' ' && cc > L'\0') MesgEditorWriteEscapedChar(&stream, L' ', 0, escapeChars);
					else MesgEditorWriteEscapedChar(&stream, cc, 0, escapeChars);
				}
				break;
			}
		}

		//null terminator
		if (cc == L'\0') break;
	}

	return (wchar_t *) stream.buffer;
}

static wchar_t *MesgEditorMessageToText(const void *mesg, int encoding, int inlinePreview, int decodeSystemTags, int escapeChars) {
	return MesgEditorMultiByteToWideChar(mesg, encoding, inlinePreview, decodeSystemTags, escapeChars);
}

static wchar_t MesgEditorDecodeFeedChar(const wchar_t **ppos, int *pEscaped) {
	const wchar_t *str = *ppos;
	wchar_t c = *(str++);

	*pEscaped = 0;
	if (c == L'\0'/* || c == L':' || c == L']'*/) {
		//end of string: do not advance poitner!
		return c;
	}

	if (c == L'\r') {
		//since we transform \n to \r\n, if c=='\r', if the next character is '\n', decode as '\n'. Otherwise, as '\r'.
		wchar_t next = *str;
		if (next == L'\n') {
			str++;
			c = L'\n';
		}
	} else if (c == L'\\') {
		//else if we're a backslash, escape based on the coming characters
		wchar_t next = *(str++);
		switch (next) {
			case L'n': c = L'\n'; break;
			case L'r': c = L'\r'; break;
			case L't': c = L'\t'; break;
			default: c = next; break; // otherwise, take it to escape the next character directly
		}

		*pEscaped = 1;
	}

	*ppos = str;
	return c;
}

static int IsHexDigit(wchar_t c) {
	if (c >= L'0' && c <= L'9') return 1;
	if (c >= L'A' && c <= L'F') return 1;
	if (c >= L'a' && c <= L'f') return 1;
	return 0;
}

static unsigned int DecodeHexDigit(wchar_t c) {
	if (c >= L'0' && c <= L'9') return (c - L'0') + 0x0;
	if (c >= L'A' && c <= L'F') return (c - L'A') + 0xA;
	if (c >= L'a' && c <= L'f') return (c - L'a') + 0xA;
	return 0;
}

static unsigned int DecodeHex(const wchar_t *p) {
	unsigned int n = 0;
	while (*p) {
		wchar_t c = *(p++);
		if (!IsHexDigit(c)) break;

		n <<= 4;
		n += DecodeHexDigit(c);
	}
	return n;
}

static unsigned int MesgEditorFeedTagSegment(const wchar_t **str, wchar_t *pSegment, unsigned int maxSegment) {
	unsigned int tagnamebuflen = 0;

	//scan to colon, right bracket, or end of string
	while (1) {
		int escaped;
		wchar_t c = MesgEditorDecodeFeedChar(str, &escaped);
		if (!escaped && (c == L':' || c == L']') || c == L'\0') break;

		if (tagnamebuflen < (maxSegment - 1)) {
			pSegment[tagnamebuflen++] = c;
		}
	}

	pSegment[tagnamebuflen++] = L'\0';
	return tagnamebuflen;
}

static int MesgEditorGetSysTagID(const wchar_t *tagname, unsigned int *pTagGroup, unsigned int *pTagID) {
	struct { const wchar_t *name; uint8_t group; uint16_t id; } tagnames[] = {
		{ L"Color",  0xFF, MESG_TAG_COLOR  },
		{ L"Font",   0xFF, MESG_TAG_FONT   },
		{ L"Size",   0xFF, MESG_TAG_SIZE   },
		{ L"Ruby",   0xFF, MESG_TAG_RUBY   },
		{ L"RubyEx", 0xFF, MESG_TAG_RUBYEX }
	};

	for (unsigned int i = 0; i < sizeof(tagnames) / sizeof(tagnames[0]); i++) {
		if (_wcsicmp(tagnames[i].name, tagname) == 0) {
			*pTagGroup = tagnames[i].group;
			*pTagID = tagnames[i].id;
			return 1;
		}
	}
	return 0;
}

static unsigned int MesgEditorDecodeTag(const wchar_t **ppos, int encoding, unsigned int *pTagGroup, unsigned int *pTagId, unsigned char *p) {
	const wchar_t *str = *ppos;

	//decode: GG:TTTT:....
	//or      Tag:....
	wchar_t tagDataBuf[(255 - 5) * 2 + 1]; // max tag data: 255 bytes (minus 5 bytes: \x1A, length, group, ID) * 2 (hex) + 1 (terminator)

	//read first segment (tag group or tag name)
	MesgEditorFeedTagSegment(&str, tagDataBuf, sizeof(tagDataBuf) / sizeof(wchar_t));

	//try to match the tag name, else decode as hex
	int asSystem = 0;
	unsigned int tagGroup, tagID;
	if (MesgEditorGetSysTagID(tagDataBuf, &tagGroup, &tagID)) {
		asSystem = 1;
	} else {
		//not a system tag name, decode as group:ID
		tagGroup = DecodeHex(tagDataBuf) & 0xFF;

		//read tag ID
		MesgEditorFeedTagSegment(&str, tagDataBuf, sizeof(tagDataBuf) / sizeof(wchar_t));
		tagID = DecodeHex(tagDataBuf) & 0xFFFF;
	}

	//read tag data
	MesgEditorFeedTagSegment(&str, tagDataBuf, sizeof(tagDataBuf) / sizeof(wchar_t));
	
	//if we decode as system, parse according to the tag. Else, decode as hex digit pairs.
	unsigned int outpos = 0;
	if (asSystem) {
		//decode based on the type of tag
		switch (tagID) {
			case MESG_TAG_COLOR: // Color
			{
				//decode color #
				unsigned int color = _wtol(tagDataBuf);
				p[outpos++] = color;
				break;
			}
			case MESG_TAG_SIZE: // Size
			{
				//decode size
				unsigned int size = _wtol(tagDataBuf);
				p[outpos++] = (size >> 0) & 0xFF;
				p[outpos++] = (size >> 8) & 0xFF;
				break;
			}
			case MESG_TAG_RUBYEX: // RubyEx
			{
				unsigned int rubyInfo = DecodeHex(tagDataBuf) & 0xFF;
				p[outpos++] = rubyInfo;

				//fall-through
			}
			case MESG_TAG_RUBY: // Ruby
			{
				unsigned int rubylen = _wtol(tagDataBuf);
				p[outpos++] = rubylen;

				MesgEditorFeedTagSegment(&str, tagDataBuf, sizeof(tagDataBuf) / sizeof(wchar_t));

				//encode character string
				wchar_t *rubyp = tagDataBuf;
				while (*rubyp) {
					//feed to target encoding
					outpos += MesgEditorFeedCharFromUnicode(*(rubyp++), encoding, p + outpos);
				}
				break;
			}
			case MESG_TAG_FONT: // Font
			{
				unsigned int font = _wtol(tagDataBuf);
				p[outpos++] = font;
				break;
			}
		}
	} else {
		//feed digit pairs
		wchar_t *tagdatap = tagDataBuf;
		while (1) {
			wchar_t c1 = L'0', c2 = L'0';
			c1 = *(tagdatap++);
			if (c1 == L'\0') break;

			c2 = *(tagdatap++);

			//put digit pair
			p[outpos++] = (DecodeHexDigit(c1) << 4) | (DecodeHexDigit(c2) << 0);

			if (c2 == L'\0') break;
		}
	}

	*pTagGroup = tagGroup;
	*pTagId = tagID;
	*ppos = str;
	return outpos;
}

static void *MesgEditorCompileMessage(const wchar_t *str, int encoding) {
	//string builder
	BSTREAM stream;
	bstreamCreate(&stream, NULL, 0);

	unsigned char mbsTmp[4];
	unsigned char tagdata[256];

	//process string and decode
	while (1) {
		int escaped;
		wchar_t c = MesgEditorDecodeFeedChar(&str, &escaped);

		if (!escaped && c == L'[') {
			//process tag
			unsigned int tagGroup, tagId;
			unsigned int taglen = MesgEditorDecodeTag(&str, encoding, &tagGroup, &tagId, tagdata);

			//put escape
			unsigned int charSize = 0, padSize = 0;
			if (encoding == MESG_ENCODING_UTF16) {
				wchar_t esc = '\x1A';
				bstreamWrite(&stream, &esc, sizeof(esc));
				charSize = 2;
				padSize = 0;

				//padding size
				if (taglen & 1) padSize = 2 - (taglen & 1);
			} else {
				char esc = '\x1A';
				bstreamWrite(&stream, &esc, sizeof(esc));
				charSize = 1;
				padSize = 0;
			}

			unsigned char padding[] = { 0 };
			unsigned char tagHdr[] = { 0x00, 0x00, 0x00, 0x00 };
			tagHdr[0] = charSize + sizeof(tagHdr) + taglen + padSize;
			tagHdr[1] = tagGroup;
			tagHdr[2] = (tagId >> 0) & 0xFF;
			tagHdr[3] = (tagId >> 8) & 0xFF;

			//put data
			bstreamWrite(&stream, tagHdr, sizeof(tagHdr));
			bstreamWrite(&stream, tagdata, taglen);
			bstreamWrite(&stream, padding, padSize);
		} else {
			//write character direct
			unsigned int mbslen = MesgEditorFeedCharFromUnicode(c, encoding, mbsTmp);
			StrAddBytes(&stream, mbsTmp, mbslen);
		}

		if (c == L'\0') break;
	}

	return stream.buffer;
}



static void MesgEditorListSuppressRedraw(MesgEditorData *data) {
	if (data->suppressListRedraw++ == 0) {
		SendMessage(data->hWndList, WM_SETREDRAW, 0, 0);
	}
}

static void MesgEditorListRestoreRedraw(MesgEditorData *data) {
	if (--data->suppressListRedraw == 0) {
		SendMessage(data->hWndList, WM_SETREDRAW, 1, 0);
		InvalidateRect(data->hWndList, NULL, FALSE);
	}
}

static void MesgEditorUpdatePreview(MesgEditorData *data) {
	InvalidateRect(data->hWnd, NULL, FALSE);
}

static void MesgEditorUpdateMessageList(MesgEditorData *data, unsigned int i) {
	if (i >= data->mesg->nMsg) return;

	wchar_t textbuf[16];
	wsprintfW(textbuf, L"%d", data->mesg->messages[i].id);

	wchar_t *strUnicode = MesgEditorMessageToText(data->mesg->messages[i].message, data->mesg->encoding, 1, data->decodeSystemTags, 1);
	AddListViewItem(data->hWndList, textbuf, i, 1);
	AddListViewItem(data->hWndList, strUnicode, i, 2);
	free(strUnicode);
}

static void MesgEditorUpdateMessageListAll(MesgEditorData *data) {
	MesgEditorListSuppressRedraw(data);
	for (unsigned int i = 0; i < data->mesg->nMsg; i++) {
		MesgEditorUpdateMessageList(data, i);
	}
	MesgEditorListRestoreRedraw(data);
	MesgEditorUpdatePreview(data);
}

static void MesgEditorUpdateText(MesgEditorData *data) {
	if (data->curMsg >= data->mesg->nMsg) return;

	void *mesg = data->mesg->messages[data->curMsg].message;
	wchar_t *unicode = MesgEditorMessageToText(mesg, data->mesg->encoding, 0, data->decodeSystemTags, 1);
	UiEditSetText(data->hWndEdit, unicode);
	free(unicode);

	MesgEditorUpdatePreview(data);
}

static void MesgEditorSetDecodeSystemTags(MesgEditorData *data, int enable) {
	if (enable == data->decodeSystemTags) return; // already set

	data->decodeSystemTags = enable;

	MesgEditorUpdateMessageListAll(data);
	MesgEditorUpdateText(data);
}

static void MesgEditorSetSelection(MesgEditorData *data, unsigned int i) {
	if (i > data->mesg->nMsg) return;

	data->curMsg = i;
	MesgEditorListSuppressRedraw(data);
	ListView_SetItemState(data->hWndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_SetItemState(data->hWndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	MesgEditorListRestoreRedraw(data);

	MesgEditorUpdateText(data);
}

static void MesgEditorOnDestroyFontEditor(EDITOR_DATA *ed, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;
	NFTRVIEWERDATA *nftrViewerData = (NFTRVIEWERDATA *) ed;

	//get index of current font
	size_t curFontIdx = ST_INDEX_NOT_FOUND;
	if (data->fontEditor != NULL) {
		StListIndexOf(&data->fontEditors, &data->fontEditor);
	}

	//remove from the list
	size_t idx = StListIndexOf(&data->fontEditors, &nftrViewerData);
	if (idx <= ST_INDEX_MAX) {
		StListRemove(&data->fontEditors, idx);
		SendMessage(data->hWndFontList, CB_DELETESTRING, idx, 0);
	}

	if (data->fontEditor == nftrViewerData) {
		data->fontEditor = NULL; // disassociate
	}

	//set current editor
	if (data->fontEditors.length > 0) {
		if (curFontIdx == ST_INDEX_NOT_FOUND) curFontIdx = 0;
		if (idx < curFontIdx) curFontIdx--;

		data->fontEditor = *(NFTRVIEWERDATA **) StListGetPtr(&data->fontEditors, curFontIdx);
		UiCbSetCurSel(data->hWndFontList, curFontIdx);
	}

	MesgEditorUpdatePreview(data);
}
static void MesgEditorOnCreateFontEditor(EDITOR_DATA *ed, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;
	NFTRVIEWERDATA *nftrViewerData = (NFTRVIEWERDATA *) ed;

	//add to font editor list
	MesgEditorAddFontEditor(data, nftrViewerData);
}

static void MesgEditorRemoveFontEditorCallbacks(MesgEditorData *data) {
	//remove create callbacks
	EditorRemoveCreateCallback(data->editorMgr, FILE_TYPE_FONT, MesgEditorOnCreateFontEditor, data);

	//remove all destroy callbacks
	for (size_t i = 0; i < data->fontEditors.length; i++) {
		NFTRVIEWERDATA *nftrViewerData = *(NFTRVIEWERDATA **) StListGetPtr(&data->fontEditors, i);
		EditorRemoveDestroyCallback((EDITOR_DATA *) nftrViewerData, MesgEditorOnDestroyFontEditor, data);
	}
}

static void MesgEditorSetAssociatedFontEditor(MesgEditorData *data, NFTRVIEWERDATA *nftrViewerData) {
	if (data->fontEditor == nftrViewerData) return;

	size_t idx = StListIndexOf(&data->fontEditors, &nftrViewerData);

	data->fontEditor = nftrViewerData;
	if (idx <= ST_INDEX_MAX) UiCbSetCurSel(data->hWndFontList, idx);
	MesgEditorUpdatePreview(data);
}

static void MesgEditorAddFontEditor(MesgEditorData *data, NFTRVIEWERDATA *nftrViewerData) {
	StListAdd(&data->fontEditors, &nftrViewerData);

	//add to dropdown
	LPCWSTR name = ObjGetFileNameFromPath(nftrViewerData->szOpenFile);
	if (name[0] == L'\0') name = L"Untitled";
	UiCbAddString(data->hWndFontList, name);

	//add destroy callback
	EditorRegisterDestroyCallback((EDITOR_DATA *) nftrViewerData, MesgEditorOnDestroyFontEditor, data);

	//if it was the only font, set the target.
	if (data->fontEditors.length == 1) {
		MesgEditorSetAssociatedFontEditor(data, nftrViewerData);
	}
}



static void MesgEditorOnCheckDecodeSystemTags(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	int checked = GetCheckboxChecked(hWndCtl);
	MesgEditorSetDecodeSystemTags((MesgEditorData *) param, checked);
}

static void MesgEditorOnChangeText(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;
	if (data->updatingEdit || data->curMsg >= data->mesg->nMsg) return;

	wchar_t *src = UiEditGetText(data->hWndEdit);

	// update
	void *msg = MesgEditorCompileMessage(src, data->mesg->encoding);
	free(src);
	
	//replace message
	unsigned int curMsg = data->curMsg;
	MesgEntry *entry = &data->mesg->messages[curMsg];
	free(entry->message);
	entry->message = msg;

	//update preview
	MesgEditorUpdatePreview(data);
	MesgEditorUpdateMessageList(data, curMsg);
}

static void MesgEditorOnChangeFont(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;

	size_t sel = UiCbGetCurSel(data->hWndFontList);
	if (sel < data->fontEditors.length) {
		MesgEditorSetAssociatedFontEditor(data, *(NFTRVIEWERDATA **) StListGetPtr(&data->fontEditors, sel));
	}
}

static void MesgEditorOnClickEditData(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;

	unsigned int curMsg = data->curMsg;
	if (curMsg >= data->mesg->nMsg) return;

	MesgEntry *entry = &data->mesg->messages[curMsg];

	//data to hex
	unsigned int dataSize = data->mesg->msgExtra;
	if (dataSize == 0) return;

	unsigned int bufSize = dataSize * 3 - 1;
	wchar_t *buf = (wchar_t *) calloc(bufSize + 1, sizeof(wchar_t));

	for (unsigned int i = 0; i < dataSize; i++) {
		wsprintfW(buf + 3 * i, L"%02X", ((unsigned char *) entry->extra)[i]);
		if ((i + 1) < dataSize) buf[3 * i + 2] = L' ';
	}

	if (PromptUserText(hWnd, L"Message Data", L"Enter message data:", buf, bufSize + 1)) {
		//decode hex
		memset(entry->extra, 0, dataSize);
		
		int nHexPair = 0, outpos = 0;
		wchar_t hexPair[3] = { 0 };
		for (unsigned int i = 0; i < bufSize; i++) {
			if (IsHexDigit(buf[i])) {
				hexPair[nHexPair++] = buf[i];
				if (nHexPair == 2) {
					((unsigned char *) entry->extra)[outpos++] = DecodeHex(hexPair);
					nHexPair = 0;
				}
			}
		}
	}

	free(buf);
}

static void MesgEditorOnClickEditDataSize(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;

	wchar_t buf[32];
	unsigned int dataSize = data->mesg->msgExtra;
	wsprintfW(buf, L"%d", dataSize);

	if (PromptUserText(hWnd, L"Message Data Size", L"Enter message data size:", buf, sizeof(buf) / sizeof(buf[0]))) {
		unsigned int newSize = _wtol(buf);
		if (newSize == data->mesg->msgExtra) return;

		//modify messages
		for (unsigned int i = 0; i < data->mesg->nMsg; i++) {
			MesgEntry *ent = &data->mesg->messages[i];
			ent->extra = (void *) realloc(ent->extra, newSize);

			if (newSize > dataSize) {
				//zero out expanded memory
				memset(((unsigned char *) ent->extra) + dataSize, 0, newSize - dataSize);
			}
		}

		data->mesg->msgExtra = newSize;

		EnableWindow(data->hWndEditData, data->mesg->msgExtra > 0);
	}
}

static void MesgEditorOnSetEncoding(HWND hWnd, HWND hWndCtl, int notif, void *param) {
	MesgEditorData *data = (MesgEditorData *) param;

	int curval = data->mesg->encoding;
	int newval = UiCbGetCurSel(hWndCtl);
	if (newval == curval) return;
	
	//if we would lose information... ASCII, SJIS and undefined coding do not encode the full Unicode space
	if (newval == MESG_ENCODING_ASCII || newval == MESG_ENCODING_SJIS || newval == MESG_ENCODING_UNDEFINED) {
		//coming from Unicode (or Shift-JIS), given that the new encoding is different, implies possible loss
		if (curval == MESG_ENCODING_SJIS || curval == MESG_ENCODING_UTF16 || curval == MESG_ENCODING_UTF8) {
			int id = MessageBox(hWnd, L"Changing the encoding may cause a loss of information. Continue?", L"Warning", MB_ICONWARNING | MB_YESNO);
			if (id == IDNO) {
				UiCbSetCurSel(hWndCtl, curval);
				return; // do not proceed
			}
		}
	}

	//change the encoding
	unsigned int szChar = 1, szCharSrc = 1;
	if (newval == MESG_ENCODING_UTF16) szChar = 2;
	if (curval == MESG_ENCODING_UTF16) szCharSrc = 2;
	for (unsigned int i = 0; i < data->mesg->nMsg; i++) {

		//converting string from source to destination encoding
		unsigned char *pos = data->mesg->messages[i].message;
		BSTREAM out;
		bstreamCreate(&out, NULL, 0);

		while (1) {
			wchar_t wc = MesgEditorFeedCharToUnicode(&pos, data->mesg->encoding);

			unsigned char mbsTmp[4];
			unsigned int nMbs = MesgEditorFeedCharFromUnicode(wc, newval, mbsTmp);
			StrAddBytes(&out, mbsTmp, nMbs);

			if (wc == L'\x1A') {
				unsigned int taglen = *(pos++);
				taglen -= szCharSrc;
				
				unsigned char taglenbyte = taglen + szChar;
				taglenbyte = (taglenbyte + szChar - 1) / szChar * szChar; // round up to multiple of char size

				unsigned int taglenTotal = taglenbyte, taglenWrite = taglen + szChar;
				StrAddBytes(&out, &taglenbyte, 1);
				StrAddBytes(&out, pos, taglen - 1);
				pos += taglen - 1;

				if (taglenTotal > taglenWrite) {
					unsigned char pad = 0;
					StrAddBytes(&out, &pad, 1);
					taglenWrite++;
				}
			}

			if (wc == L'\0') break; // end
		}

		unsigned int outSize;
		unsigned char *newmsg = bstreamToByteArray(&out, &outSize);

		free(data->mesg->messages[i].message);
		data->mesg->messages[i].message = newmsg;
	}

	data->mesg->encoding = newval;

	//update
	MesgEditorUpdateMessageListAll(data);
	MesgEditorUpdateText(data);
	MesgEditorUpdatePreview(data);
}


static void MesgEditorOnCreate(MesgEditorData *data) {
	data->decodeSystemTags = 1;

	FbCreate(&data->fbPreview, data->hWnd, 1, 1);

	data->hWndList = CreateListView(data->hWnd, 0, 0, 360, 300);
	AddListViewColumn(data->hWndList, L"#", 0, 40, SCA_LEFT);
	AddListViewColumn(data->hWndList, L"ID", 1, 40, SCA_LEFT);
	AddListViewColumn(data->hWndList, L"Message", 2, 280, SCA_LEFT);
	SetWindowLong(data->hWndList, GWL_STYLE, (GetWindowLong(data->hWndList, GWL_STYLE) & ~LVS_EDITLABELS) | LVS_SHOWSELALWAYS | LVS_SINGLESEL);

	LPCWSTR encodings[] = { L"Undefined", L"ANSI", L"UTF-16", L"Shift JIS", L"UTF-8" };

	data->hWndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
		350, 0, 300, 100, data->hWnd, NULL, NULL, NULL);
	data->hWndDecodeSystemTags = CreateCheckbox(data->hWnd, L"Decode system tags", 355, 0, 150, 22, data->decodeSystemTags);
	data->hWndEditData = CreateButton(data->hWnd, L"Edit Data", 0, 0, 0, 0, FALSE);
	data->hWndEditDataSize = CreateButton(data->hWnd, L"Edit Data Size", 0, 0, 0, 0, FALSE);
	data->hWndFontLabel = CreateStatic(data->hWnd, L"Font:", 0, 0, 0, 0);
	data->hWndEncoding = CreateCombobox(data->hWnd, encodings, 5, 0, 0, 0, 0, 0);
	data->hWndFontList = CreateCombobox(data->hWnd, NULL, 0, 0, 0, 0, 0, 0);

	UiCtlMgrInit(&data->mgr, data);
	UiCtlMgrAddCommand(&data->mgr, data->hWndDecodeSystemTags, BN_CLICKED, MesgEditorOnCheckDecodeSystemTags);
	UiCtlMgrAddCommand(&data->mgr, data->hWndEdit, EN_CHANGE, MesgEditorOnChangeText);
	UiCtlMgrAddCommand(&data->mgr, data->hWndFontList, CBN_SELCHANGE, MesgEditorOnChangeFont);
	UiCtlMgrAddCommand(&data->mgr, data->hWndEditData, BN_CLICKED, MesgEditorOnClickEditData);
	UiCtlMgrAddCommand(&data->mgr, data->hWndEditDataSize, BN_CLICKED, MesgEditorOnClickEditDataSize);
	UiCtlMgrAddCommand(&data->mgr, data->hWndEncoding, CBN_SELCHANGE, MesgEditorOnSetEncoding);

	StListCreateInline(&data->fontEditors, NFTRVIEWERDATA *, NULL);

	//DEBUG: find font editor and set as associated
	StList fonts;
	StListCreateInline(&fonts, NFTRVIEWERDATA *, NULL);
	EditorGetAllByType(data->editorMgr->hWnd, FILE_TYPE_FONT, &fonts);

	for (size_t i = 0; i < fonts.length; i++) {
		NFTRVIEWERDATA *nftrViewerData = *(NFTRVIEWERDATA **) StListGetPtr(&fonts, i);
		MesgEditorAddFontEditor(data, nftrViewerData);
	}
	StListFree(&fonts);

	//register creation callback
	EditorRegisterCreateCallback(data->editorMgr, FILE_TYPE_FONT, MesgEditorOnCreateFontEditor, data);

	SetClassLong(data->hWnd, GCL_STYLE, GetClassLong(data->hWnd, GCL_STYLE) | CS_HREDRAW | CS_VREDRAW);
}

static void MesgEditorOnInitialize(MesgEditorData *data, MesgFile *mesg, LPCWSTR path) {
	if (path != NULL) {
		EditorSetFile(data->hWnd, path);
	}
	data->mesg = mesg;

	//populate list view
	MesgEditorListSuppressRedraw(data);
	for (unsigned int i = 0; i < mesg->nMsg; i++) {
		void *str = mesg->messages[i].message;

		WCHAR intbuf[16];
		wsprintfW(intbuf, L"%d", i);
		AddListViewItem(data->hWndList, intbuf, i, 0);
	}
	MesgEditorUpdateMessageListAll(data);
	MesgEditorListRestoreRedraw(data);

	//if no extra data, disable "Edit Data"
	EnableWindow(data->hWndEditData, data->mesg->msgExtra > 0);

	//set encoding
	UiCbSetCurSel(data->hWndEncoding, mesg->encoding);

	//set selection to string 0
	MesgEditorSetSelection(data, 0);
}

static void MesgEditorOnSize(MesgEditorData *data) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	float dpiScale = GetDpiScale();

	int ctlHeight = UI_SCALE_COORD(22, dpiScale);
	int listWidth = UI_SCALE_COORD(360, dpiScale);
	int padSize = UI_SCALE_COORD(5, dpiScale);
	int shortWidth = UI_SCALE_COORD(40, dpiScale);
	int ctlWidth = UI_SCALE_COORD(100, dpiScale);
	int wideWidth = UI_SCALE_COORD(150, dpiScale);

	MoveWindow(data->hWndList, 0, 0, listWidth, rcClient.bottom, TRUE);
	MoveWindow(data->hWndEdit, listWidth, ctlHeight, rcClient.right - listWidth, rcClient.bottom / 2 - ctlHeight, TRUE);
	MoveWindow(data->hWndDecodeSystemTags, listWidth + padSize, 0, wideWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndEncoding, rcClient.right - ctlWidth * 3, 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndEditDataSize, rcClient.right - ctlWidth * 2, 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndEditData, rcClient.right - ctlWidth, 0, ctlWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndFontLabel, listWidth + padSize, rcClient.bottom / 2, shortWidth, ctlHeight, TRUE);
	MoveWindow(data->hWndFontList, listWidth + padSize + shortWidth, rcClient.bottom / 2, wideWidth, ctlHeight, TRUE);
}

static void MesgEditorDeleteMessage(MesgEditorData *data) {
	if (data->curMsg >= data->mesg->nMsg) return;

	//delete from message
	MesgEntry *ent = &data->mesg->messages[data->curMsg];
	free(ent->message);
	free(ent->extra);
	memmove(ent, ent + 1, (data->mesg->nMsg - data->curMsg - 1) * sizeof(MesgEntry));
	data->mesg->nMsg--;

	//delete list view entry
	ListView_DeleteItem(data->hWndList, data->curMsg);

	//update selection
	if (data->mesg->nMsg > 0) {
		if (data->curMsg >= data->mesg->nMsg) {
			MesgEditorSetSelection(data, data->curMsg - 1);
		} else {
			MesgEditorSetSelection(data, data->curMsg);
		}
	} else {
		//empty the edit
		UiEditSetText(data->hWndEdit, L"");
	}
}

static void MesgEditorOnNotify(MesgEditorData *data, LPNMHDR hdr) {
	if (data == NULL || hdr->hwndFrom != data->hWndList) return;

	switch (hdr->code) {
		case LVN_ITEMCHANGED:
		{
			LPNMLISTVIEW nm = (LPNMLISTVIEW) hdr;

			if ((nm->uNewState | nm->uOldState) & LVIS_SELECTED) {
				int iItem = nm->iItem;

				//selection state changed
				if (nm->uNewState & LVIS_SELECTED) {
					//selection changed (+)
					data->curMsg = iItem;
					MesgEditorUpdateText(data);
				} else {
				}
			}

			break;
		}
		case NM_RCLICK:
		{
			LPNMITEMACTIVATE lpnma = (LPNMITEMACTIVATE) hdr;
			int item = lpnma->iItem;
			
			HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 10);
			if (item == -1) {
				//disable delete
				EnableMenuItem(hPopup, ID_MESGMENU_DELETE, MF_DISABLED);
			}

			POINT mouse;
			GetCursorPos(&mouse);
			TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWnd, NULL);
			DeleteObject(hPopup);
			break;
		}
		case LVN_KEYDOWN:
		{
			LPNMLVKEYDOWN lpnmk = (LPNMLVKEYDOWN) hdr;
			switch (lpnmk->wVKey) {
				case VK_DELETE:
				{
					MesgEditorDeleteMessage(data);
					break;
				}
			}
			break;
		}
	}
}

static void MesgEditorOnDestroy(MesgEditorData *data) {
	UiCtlMgrFree(&data->mgr);
	FbDestroy(&data->fbPreview);
	MesgEditorRemoveFontEditorCallbacks(data);
	StListFree(&data->fontEditors);
}

static void MesgEditorNewMessage(MesgEditorData *data) {
	//get the smallest unused ID
	int idMax = 0;
	for (unsigned int i = 0; i < data->mesg->nMsg; i++) {
		if (data->mesg->messages[i].id > idMax) idMax = data->mesg->messages[i].id;
	}
	unsigned int id = idMax + 1;

	//ask message ID?
	if (data->mesg->includeIdMap) {
		wchar_t buf[32];
		wsprintfW(buf, L"%d", id);

		if (PromptUserText(data->hWnd, L"New Message", L"Enter the new message ID:", buf, sizeof(buf) / sizeof(buf[0]))) {
			id = _wtol(buf);
		} else {
			return;
		}
	}

	//put new message
	data->mesg->messages = (MesgEntry *) realloc(data->mesg->messages, (data->mesg->nMsg + 1) * sizeof(MesgEntry));
	data->mesg->nMsg++;

	MesgEntry *newent = &data->mesg->messages[data->mesg->nMsg - 1];
	newent->message = (char *) calloc(2, 1); // empty string in any encoding
	newent->id = id;
	newent->extra = calloc(data->mesg->msgExtra, 1); // empty extra

	//put new 
	wchar_t intbuf[16];
	wsprintfW(intbuf, L"%d", data->mesg->nMsg - 1);
	AddListViewItem(data->hWndList, intbuf, data->mesg->nMsg - 1, 0);
	MesgEditorUpdateMessageList(data, data->mesg->nMsg - 1);
	MesgEditorSetSelection(data, data->mesg->nMsg - 1);
	MesgEditorUpdatePreview(data);
	SetFocus(data->hWndEdit);
}

static void MesgEditorRenderCurrentMessagePreview(MesgEditorData *data, COLOR32 *px, int width, int height) {
	if (data->curMsg >= data->mesg->nMsg) return;

	NFTRVIEWERDATA *nftrViewerData = data->fontEditor;
	if (data->fontEditor == NULL || data->fontEditor->nftr == NULL) return;

	NFTR *nftr = nftrViewerData->nftr;

	int spaceX = nftrViewerData->spaceX, spaceY = nftrViewerData->spaceY;
	COLOR *pltt = nftrViewerData->palette;

	MesgEntry *message = &data->mesg->messages[data->curMsg];
	wchar_t *str = MesgEditorMessageToText(message->message, data->mesg->encoding, 0, 0, 0);
	NftrRenderString(nftr, px, width, height, str, spaceX, spaceY, pltt);
	free(str);
}

static void MesgEditorGetPreviewBounds(MesgEditorData *data, int *x, int *y, int *width, int *height) {
	RECT rcClient;
	GetClientRect(data->hWnd, &rcClient);

	float dpiScale = GetDpiScale();
	int ctlHeight = UI_SCALE_COORD(22, dpiScale);
	int listWidth = UI_SCALE_COORD(360, dpiScale);

	*x = listWidth;
	*y = rcClient.bottom / 2 + ctlHeight;
	*width = rcClient.right - listWidth;
	*height = rcClient.bottom - *y;
}

static void MesgEditorCopyPreview(MesgEditorData *data) {
	int x, y, width, height;
	MesgEditorGetPreviewBounds(data, &x, &y, &width, &height);

	//shrink by border
	width -= 2;
	height -= 2;
	if (width < 1) width = 1;
	if (height < 1) height = 1;

	//open clipboard
	OpenClipboard(data->hWnd);
	EmptyClipboard();

	//create bitmap
	COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	MesgEditorRenderCurrentMessagePreview(data, px, width, height);
	ClipCopyBitmapEx(px, width, height, 0, NULL, 0);
	free(px);

	CloseClipboard();
}

static void MesgEditorOnMenuCommand(MesgEditorData *data, int idMenu) {
	switch (idMenu) {
		case ID_FILE_SAVE:
			EditorSave(data->hWnd);
			break;
		case ID_FILE_SAVEAS:
			EditorSaveAs(data->hWnd);
			break;
		case ID_MESGMENU_DELETE:
			MesgEditorDeleteMessage(data);
			break;
		case ID_MESGMENU_NEWMESSAGE:
			MesgEditorNewMessage(data);
			break;
		case ID_MESGPREVMENU_COPY:
			MesgEditorCopyPreview(data);
			break;
	}
}

static void MesgEditorOnCommand(MesgEditorData *data, WPARAM wParam, LPARAM lParam) {
	UiCtlMgrOnCommand(&data->mgr, data->hWnd, wParam, lParam);
	if (lParam) {
		//
	} else if (HIWORD(wParam) == 0) {
		MesgEditorOnMenuCommand(data, LOWORD(wParam));
	} else if (HIWORD(wParam) == 1) {
		//
	}
}

static void MesgEditorOnPaint(MesgEditorData *data) {
	PAINTSTRUCT ps;
	HDC hDC = BeginPaint(data->hWnd, &ps);

	int prevX, prevY, prevWidth, prevHeight;
	MesgEditorGetPreviewBounds(data, &prevX, &prevY, &prevWidth, &prevHeight);

	HPEN hOutline = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
	HBRUSH hbrOld = SelectObject(hDC, GetStockObject(NULL_BRUSH));
	HPEN hOldPen = SelectObject(hDC, hOutline);
	Rectangle(hDC, prevX, prevY, prevX + prevWidth, prevY + prevHeight);
	SelectObject(hDC, hOldPen);
	SelectObject(hDC, hbrOld);
	DeleteObject(hOutline);

	FbSetSize(&data->fbPreview, prevWidth, prevHeight);

	//fill background
	FbFillRect(&data->fbPreview, 0, 0, prevWidth - 2, prevHeight - 2, 0xFFFFFFFF);

	//draw text
	MesgEditorRenderCurrentMessagePreview(data, data->fbPreview.px, data->fbPreview.width, data->fbPreview.height);

	FbDraw(&data->fbPreview, hDC, prevX + 1, prevY + 1, prevWidth - 2, prevHeight - 2, 0, 0);

	EndPaint(data->hWnd, &ps);
}

static void MesgEditorOnContextMenu(MesgEditorData *data) {
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(data->hWnd, &pt);

	int prevX, prevY, prevWidth, prevHeight;
	MesgEditorGetPreviewBounds(data, &prevX, &prevY, &prevWidth, &prevHeight);

	//mouse cursor in bounds of preview
	if (pt.x >= prevX && pt.y >= prevY && pt.x < (prevX + prevWidth) && pt.y < (prevY + prevHeight)) {
		HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 11);

		POINT mouse;
		GetCursorPos(&mouse);
		TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, data->hWnd, NULL);
		DeleteObject(hPopup);
	}

}

static LRESULT CALLBACK MesgEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	MesgEditorData *data = (MesgEditorData *) EditorGetData(hWnd);
	switch (msg) {
		case WM_CREATE:
			MesgEditorOnCreate(data);
			break;
		case WM_SIZE:
			MesgEditorOnSize(data);
			break;
		case NV_INITIALIZE:
			MesgEditorOnInitialize(data, (MesgFile *) lParam, (LPCWSTR) wParam);
			break;
		case WM_PAINT:
			MesgEditorOnPaint(data);
			break;
		case WM_COMMAND:
			MesgEditorOnCommand(data, wParam, lParam);
			break;
		case WM_CONTEXTMENU:
			MesgEditorOnContextMenu(data);
			break;
		case WM_NOTIFY:
			MesgEditorOnNotify(data, (LPNMHDR) lParam);
			break;
		case WM_DESTROY:
			MesgEditorOnDestroy(data);
			break;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

void MesgEditorRegisterClass(void) {
	MesgRegisterFormats();

	int features = 0;
	EditorRegister(L"MesgEditorClass", MesgEditorWndProc, L"Message Editor", sizeof(MesgEditorData), features);
}

static HWND CreateMesgEditorInternal(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path, MesgFile *mesg) {
	HWND h = EditorCreate(L"MesgEditorClass", x, y, width, height, hWndParent);
	SendMessage(h, NV_INITIALIZE, (WPARAM) path, (LPARAM) mesg);
	return h;
}

HWND CreateMesgEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	MesgFile *mesg = (MesgFile *) calloc(1, sizeof(MesgFile));
	if (MesgReadFile(mesg, path)) {
		free(mesg);
		MessageBox(hWndParent, L"Invalid file.", L"Invalid file", MB_ICONERROR);
		return NULL;
	}

	return CreateMesgEditorInternal(x, y, width, height, hWndParent, path, mesg);
}

HWND CreateMesgEditorImmediate(int x, int y, int width, int height, HWND hWndParent, MesgFile *mesg) {
	return CreateMesgEditorInternal(x, y, width, height, hWndParent, NULL, mesg);
}

