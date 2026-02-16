/*-------------------------------------------
  font.c
  create a font of the clock
         KAZUBON 1999
---------------------------------------------*/
#include "tcdll.h"
static LONG g_depth_CreateMyFont = 0;
static const wchar_t kFontMsGothicW[] = L"\xFF2D\xFF33 \x30B4\x30B7\x30C3\x30AF";
static const wchar_t kFontMsGothicAsciiW[] = L"MS Gothic";

struct {
	int cp;
	BYTE charset;
} codepage_charset[] = {
	{ 932,  SHIFTJIS_CHARSET },
	{ 936,  GB2312_CHARSET },
	{ 949,  HANGEUL_CHARSET },
	{ 950,  CHINESEBIG5_CHARSET },
	{ 1250, EASTEUROPE_CHARSET },
	{ 1251, RUSSIAN_CHARSET },
	{ 1252, ANSI_CHARSET },
	{ 1253, GREEK_CHARSET },
	{ 1254, TURKISH_CHARSET },
	{ 1257, BALTIC_CHARSET },
	{ 0, 0}
};

int GetLocaleInfoCompat(WORD wLanguageID, LCTYPE LCType, char* dst, int n);

static BOOL DecodeToWideBestEffort(const char* src, wchar_t* dst, int dstCount)
{
	if (!src || !dst || dstCount <= 0) return FALSE;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, dstCount) > 0) return TRUE;
	return MultiByteToWideChar(CP_ACP, 0, src, -1, dst, dstCount) > 0;
}

static BOOL EqualsWideNoCase(const wchar_t* a, const wchar_t* b)
{
	if (!a || !b) return FALSE;
	return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static BOOL IsMsGothicRequest(const char* fontname)
{
	wchar_t wbuf[LF_FACESIZE];
	if (!DecodeToWideBestEffort(fontname, wbuf, LF_FACESIZE)) return FALSE;
	return EqualsWideNoCase(wbuf, kFontMsGothicW) || EqualsWideNoCase(wbuf, kFontMsGothicAsciiW);
}

static BOOL IsMsGothicActualFaceW(const wchar_t* actualFaceW)
{
	if (!actualFaceW || !actualFaceW[0]) return FALSE;
	return EqualsWideNoCase(actualFaceW, kFontMsGothicW) || EqualsWideNoCase(actualFaceW, kFontMsGothicAsciiW);
}

static void RecreateAsMsUiGothic(LOGFONT* lf, HFONT* phFont)
{
	if (!lf || !phFont) return;
	if (*phFont) {
		DeleteObject(*phFont);
		*phFont = NULL;
	}
	lstrcpyn(lf->lfFaceName, "MS UI Gothic", LF_FACESIZE);
	*phFont = CreateFontIndirect(lf);
}

/*------------------------------------------------
   callback function for EnumFontFamiliesEx,
   to find a designated font
--------------------------------------------------*/
BOOL CALLBACK EnumFontFamExProc(ENUMLOGFONTEX* pelf,
	NEWTEXTMETRICEX* lpntm, int FontType, LPARAM fontname)
{
	UNREFERENCED_PARAMETER(lpntm);
	UNREFERENCED_PARAMETER(FontType);
	if (!pelf || !fontname) return TRUE;
	if (lstrcmpi((LPSTR)fontname, pelf->elfLogFont.lfFaceName) == 0)
		return FALSE;
	return TRUE;
}

/*------------------------------------------------
   create a font of the clock
--------------------------------------------------*/
HFONT CreateMyFont(char* fontname, int fontsize,
	LONG weight, LONG italic)
{
	LOGFONT lf;
	POINT pt;
	HDC hdc;
	HFONT hOut = NULL;
	WORD langid;
	char s[11];
	char fontnameLocal[LF_FACESIZE];
	int cp;
	BYTE charset;
	int i;
	LONG depth;
	int fnlen;
	BOOL requestMsGothic;
	wchar_t actualFaceW[LF_FACESIZE];
	HDC hdcFace;
	HFONT hOldFace;

	depth = InterlockedIncrement(&g_depth_CreateMyFont);
	if (depth > 32) {
		InterlockedDecrement(&g_depth_CreateMyFont);
		return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	}
	if (!fontname) fontname = "";
	lstrcpyn(fontnameLocal, fontname, LF_FACESIZE);
	requestMsGothic = IsMsGothicRequest(fontnameLocal);
	fnlen = lstrlen(fontnameLocal);
	if (fnlen >= 2 && fontnameLocal[0] == '\"' && fontnameLocal[fnlen - 1] == '\"') {
		int inner = fnlen - 2;
		if (inner < 0) inner = 0;
		MoveMemory(fontnameLocal, fontnameLocal + 1, inner);
		fontnameLocal[inner] = '\0';
	}

	memset(&lf, 0, sizeof(LOGFONT));

	langid = (WORD)GetMyRegLong("Format", "Locale", (int)GetUserDefaultLangID());
	cp = CP_ACP;
	if(GetLocaleInfoCompat(langid, LOCALE_IDEFAULTANSICODEPAGE, s, 10) > 0)
	{
		char *p;
		p = s; cp = 0;
		while('0' <= *p && *p <= '9') cp = cp * 10 + *p++ - '0';
		if(!IsValidCodePage(cp)) cp = CP_ACP;
	}

	charset = 0;
	for(i = 0; codepage_charset[i].cp; i++)
	{
		if(cp == codepage_charset[i].cp)
		{
			charset = codepage_charset[i].charset; break;
		}
	}

	hdc = GetDC(NULL);

	/* Avoid enum-time font-driver crashes: create font directly from face name. */
	if(charset == 0) charset = DEFAULT_CHARSET;
	lf.lfCharSet = charset;

	pt.x = 0;
	pt.y = GetDeviceCaps(hdc, LOGPIXELSY) * fontsize / 72;
	DPtoLP(hdc, &pt, 1);
	lf.lfHeight = -pt.y;

	ReleaseDC(NULL, hdc);

	lf.lfWidth = lf.lfEscapement = lf.lfOrientation = 0;
	lf.lfWeight = weight;
	lf.lfItalic = (BYTE)italic;
	lf.lfUnderline = 0;
	lf.lfStrikeOut = 0;
	//lf.lfCharSet = ;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	lstrcpyn(lf.lfFaceName, fontnameLocal, LF_FACESIZE);
	hOut = CreateFontIndirect(&lf);
	if (hOut && requestMsGothic) {
		actualFaceW[0] = L'\0';
		hdcFace = GetDC(NULL);
		if (hdcFace) {
			hOldFace = (HFONT)SelectObject(hdcFace, hOut);
			GetTextFaceW(hdcFace, LF_FACESIZE, actualFaceW);
			if (hOldFace) SelectObject(hdcFace, hOldFace);
			ReleaseDC(NULL, hdcFace);
		}
		if (!IsMsGothicActualFaceW(actualFaceW)) {
			RecreateAsMsUiGothic(&lf, &hOut);
		}
	}
	if (!hOut) hOut = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	InterlockedDecrement(&g_depth_CreateMyFont);
	return hOut;
}


