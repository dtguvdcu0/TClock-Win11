#include "tcdll.h"
#include "minmode.h"

BOOL b_MinimalMode = FALSE;
BOOL b_MinSysInfo = FALSE;
BOOL b_MinBattery = FALSE;
BOOL b_MinNetwork = FALSE;

static void min_put_long_if_missing(const char* section, const char* entry, LONG value)
{
	if (GetMyRegLong(section, entry, -1) != -1) return;
	SetMyRegLong(section, entry, value);
}

static void min_put_str_if_missing(const char* section, const char* entry, const char* value)
{
	char buf[256];

	GetMyRegStr(section, entry, buf, (int)sizeof(buf), "");
	if (buf[0] != '\0') return;
	SetMyRegStr(section, entry, value);
}

static void min_upgrade_v1(void)
{
	char fmt[256];
	LONG version;
	LONG normalLog;

	version = GetMyRegLong("Status_DoNotEdit", "MinimalDefaultsVersion", 0);
	if (version >= 1) return;

	normalLog = GetMyRegLong(NULL, "NormalLog", -1);
	if (normalLog == -1 || normalLog == 1) {
		SetMyRegLong(NULL, "NormalLog", FALSE);
	}

	GetMyRegStr("Format", "Format", fmt, (int)sizeof(fmt), "");
	if (fmt[0] == '\0' ||
		strcmp(fmt, "yy/mm/dd ddd hh:nn:ss") == 0 ||
		strcmp(fmt, "yyyy/mm/dd ddd tt hh:nn:ss") == 0) {
		SetMyRegStr("Format", "Format", "yyyy/mm/dd(ddd) hh:nn:ss");
	}

	SetMyRegLong("Status_DoNotEdit", "MinimalDefaultsVersion", 1);
}

BOOL WINAPI IsMinimalMode(void)
{
	return b_MinimalMode;
}

void min_ensure_defaults(void)
{
	if (!GetMyRegLong("ETC", "MinimalMode", FALSE)) return;

	min_upgrade_v1();

	min_put_long_if_missing("Minimal", "EnableSysInfo", FALSE);
	min_put_long_if_missing("Minimal", "EnableBattery", FALSE);
	min_put_long_if_missing("Minimal", "EnableNetwork", FALSE);

	min_put_long_if_missing(NULL, "NormalLog", FALSE);

	min_put_long_if_missing("Format", "Custom", TRUE);
	min_put_str_if_missing("Format", "Format", "yyyy/mm/dd(ddd) hh:nn:ss");

	min_put_long_if_missing("Color_Font", "UseBackColor", TRUE);
	min_put_long_if_missing("Color_Font", "AutoBackMatchTaskbar", FALSE);
	min_put_long_if_missing("Color_Font", "AutoBackAlpha", 255);
	min_put_long_if_missing("Color_Font", "AutoBackRefreshSec", 1);
	min_put_str_if_missing("Color_Font", "Font", "MS UI Gothic");
	min_put_long_if_missing("Color_Font", "FontSize", 12);
	min_put_long_if_missing("Color_Font", "ForeColor", 0);
	min_put_long_if_missing("Color_Font", "BackColor", 15790320);
	min_put_long_if_missing("Color_Font", "BackColor2", 15790320);
	min_put_long_if_missing("Color_Font", "UseAllColor", FALSE);
	min_put_long_if_missing("Color_Font", "TextPos", 0);
	min_put_long_if_missing("Color_Font", "VertPos", 0);
	min_put_long_if_missing("Color_Font", "LineHeight", 0);
}

void min_read(void)
{
	BOOL configMinimalMode = GetMyRegLong("ETC", "MinimalMode", FALSE) ? TRUE : FALSE;

	b_MinimalMode = configMinimalMode;
	if (!b_MinimalMode) {
		b_MinSysInfo = FALSE;
		b_MinBattery = FALSE;
		b_MinNetwork = FALSE;
		return;
	}

	b_MinSysInfo = GetMyRegLong("Minimal", "EnableSysInfo", FALSE) ? TRUE : FALSE;
	b_MinBattery = GetMyRegLong("Minimal", "EnableBattery", FALSE) ? TRUE : FALSE;
	b_MinNetwork = GetMyRegLong("Minimal", "EnableNetwork", FALSE) ? TRUE : FALSE;
}

DWORD min_sysmask(DWORD dwInfoFormat)
{
	DWORD mask;

	if (!b_MinimalMode || !b_MinSysInfo) return 0;

	mask = dwInfoFormat & (FORMAT_BATTERY | FORMAT_MEMORY | FORMAT_NET | FORMAT_HDD | FORMAT_CPU | FORMAT_VOL | FORMAT_GPU | FORMAT_TEMP);
	if (!b_MinBattery) {
		mask &= ~FORMAT_BATTERY;
	}
	if (!b_MinNetwork) {
		mask &= ~FORMAT_NET;
	}
	return mask;
}

DWORD min_backendmask(DWORD sysMask)
{
	DWORD backendMask;

	if (!b_MinimalMode || sysMask == 0) return 0;

	backendMask = 0;
	if ((sysMask & FORMAT_BATTERY) != 0) backendMask |= MINBACK_BATTERY;
	if ((sysMask & FORMAT_CPU) != 0) backendMask |= MINBACK_PERMON;
	if ((sysMask & FORMAT_GPU) != 0) backendMask |= MINBACK_GPU;
	if ((sysMask & FORMAT_TEMP) != 0) backendMask |= MINBACK_TEMP;
	if ((sysMask & FORMAT_NET) != 0) backendMask |= MINBACK_NET;
	if ((sysMask & FORMAT_HDD) != 0) backendMask |= MINBACK_DISK;
	return backendMask;
}
