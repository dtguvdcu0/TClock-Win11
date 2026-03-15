#ifndef TCMINMODE_H
#define TCMINMODE_H

#include <windows.h>

extern BOOL b_MinimalMode;
extern BOOL b_MinSysInfo;
extern BOOL b_MinBattery;
extern BOOL b_MinNetwork;

#define MINBACK_BATTERY  0x0001
#define MINBACK_PERMON   0x0002
#define MINBACK_GPU      0x0004
#define MINBACK_TEMP     0x0008
#define MINBACK_NET      0x0010
#define MINBACK_DISK     0x0020

BOOL WINAPI IsMinimalMode(void);
void min_ensure_defaults(void);
void min_read(void);
DWORD min_sysmask(DWORD dwInfoFormat);
DWORD min_backendmask(DWORD sysMask);

#endif
