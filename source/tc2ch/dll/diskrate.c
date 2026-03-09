/*-------------------------------------------------------------------------
  diskrate.c
  get logical drive transfer rate counters
---------------------------------------------------------------------------*/

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include "tcdll.h"

#define MAX_DISKRATE_DRIVE 26

double diskRateRead[MAX_DISKRATE_DRIVE] = { 0 };
double diskRateWrite[MAX_DISKRATE_DRIVE] = { 0 };
double diskRateTotal[MAX_DISKRATE_DRIVE] = { 0 };

static PDH_HCOUNTER hDiskRateRead[MAX_DISKRATE_DRIVE] = { NULL };
static PDH_HCOUNTER hDiskRateWrite[MAX_DISKRATE_DRIVE] = { NULL };
static PDH_HCOUNTER hDiskRateTotal[MAX_DISKRATE_DRIVE] = { NULL };
static BOOL bDiskRateRegistered[MAX_DISKRATE_DRIVE] = { FALSE };
static int countDiskRateRegistered = 0;

static HMODULE hmodPDH = NULL;
static PDH_HQUERY hQueryDiskRate = NULL;

typedef PDH_STATUS(WINAPI* pfnPdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS(WINAPI* pfnPdhAddCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS(WINAPI* pfnPdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS(WINAPI* pfnPdhGetFormattedCounterValue)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
typedef PDH_STATUS(WINAPI* pfnPdhRemoveCounter)(PDH_HCOUNTER);
typedef PDH_STATUS(WINAPI* pfnPdhCloseQuery)(PDH_HQUERY);

static pfnPdhOpenQueryW pPdhOpenQueryW = NULL;
static pfnPdhAddCounterW pPdhAddCounterW = NULL;
static pfnPdhCollectQueryData pPdhCollectQueryData = NULL;
static pfnPdhGetFormattedCounterValue pPdhGetFormattedCounterValue = NULL;
static pfnPdhRemoveCounter pPdhRemoveCounter = NULL;
static pfnPdhCloseQuery pPdhCloseQuery = NULL;

extern int actdvl[];
extern BOOL b_DebugLog;

static void DiskRate_clear_slot(int index)
{
	if (index < 0 || index >= MAX_DISKRATE_DRIVE) return;
	diskRateRead[index] = 0.0;
	diskRateWrite[index] = 0.0;
	diskRateTotal[index] = 0.0;
	hDiskRateRead[index] = NULL;
	hDiskRateWrite[index] = NULL;
	hDiskRateTotal[index] = NULL;
	bDiskRateRegistered[index] = FALSE;
}

static void DiskRate_clear_all(void)
{
	int i;
	for (i = 0; i < MAX_DISKRATE_DRIVE; i++) {
		DiskRate_clear_slot(i);
	}
	countDiskRateRegistered = 0;
}

static BOOL DiskRate_try_register(int index)
{
	wchar_t counterName[64];
	PDH_HCOUNTER hRead = NULL;
	PDH_HCOUNTER hWrite = NULL;
	PDH_HCOUNTER hTotal = NULL;
	wchar_t driveLetter;

	if (index < 0 || index >= MAX_DISKRATE_DRIVE) return FALSE;
	if (bDiskRateRegistered[index]) return TRUE;
	if (!hQueryDiskRate || !pPdhAddCounterW) return FALSE;
	if (actdvl[index] != 1) return FALSE;

	driveLetter = (wchar_t)(L'A' + index);

	wsprintfW(counterName, L"\\LogicalDisk(%c:)\\Disk Read Bytes/sec", driveLetter);
	if (pPdhAddCounterW(hQueryDiskRate, counterName, 0, &hRead) != ERROR_SUCCESS) return FALSE;

	wsprintfW(counterName, L"\\LogicalDisk(%c:)\\Disk Write Bytes/sec", driveLetter);
	if (pPdhAddCounterW(hQueryDiskRate, counterName, 0, &hWrite) != ERROR_SUCCESS) {
		if (pPdhRemoveCounter && hRead) pPdhRemoveCounter(hRead);
		return FALSE;
	}

	wsprintfW(counterName, L"\\LogicalDisk(%c:)\\Disk Bytes/sec", driveLetter);
	if (pPdhAddCounterW(hQueryDiskRate, counterName, 0, &hTotal) != ERROR_SUCCESS) {
		if (pPdhRemoveCounter && hRead) pPdhRemoveCounter(hRead);
		if (pPdhRemoveCounter && hWrite) pPdhRemoveCounter(hWrite);
		return FALSE;
	}

	hDiskRateRead[index] = hRead;
	hDiskRateWrite[index] = hWrite;
	hDiskRateTotal[index] = hTotal;
	bDiskRateRegistered[index] = TRUE;
	countDiskRateRegistered++;
	if (b_DebugLog) writeDebugLog_Win10("[diskrate.c][DiskRate_try_register] registered drive index =", index);
	return TRUE;
}

static void DiskRate_read_counter(PDH_HCOUNTER counter, double* outValue)
{
	PDH_FMT_COUNTERVALUE fmtValue;

	if (!outValue) return;
	*outValue = 0.0;
	if (!counter || !pPdhGetFormattedCounterValue) return;

	if (pPdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &fmtValue) == ERROR_SUCCESS) {
		*outValue = fmtValue.doubleValue;
		if (*outValue < 0.0) *outValue = 0.0;
	}
}

void DiskRate_start(void)
{
	if (hQueryDiskRate) DiskRate_end();
	DiskRate_clear_all();

	if (!hmodPDH) {
		hmodPDH = LoadLibraryW(L"pdh.dll");
		if (hmodPDH == NULL) return;

		pPdhOpenQueryW = (pfnPdhOpenQueryW)GetProcAddress(hmodPDH, "PdhOpenQueryW");
		pPdhAddCounterW = (pfnPdhAddCounterW)GetProcAddress(hmodPDH, "PdhAddCounterW");
		pPdhCollectQueryData = (pfnPdhCollectQueryData)GetProcAddress(hmodPDH, "PdhCollectQueryData");
		pPdhGetFormattedCounterValue = (pfnPdhGetFormattedCounterValue)GetProcAddress(hmodPDH, "PdhGetFormattedCounterValue");
		pPdhRemoveCounter = (pfnPdhRemoveCounter)GetProcAddress(hmodPDH, "PdhRemoveCounter");
		pPdhCloseQuery = (pfnPdhCloseQuery)GetProcAddress(hmodPDH, "PdhCloseQuery");

		if (!pPdhOpenQueryW || !pPdhAddCounterW || !pPdhCollectQueryData ||
			!pPdhGetFormattedCounterValue || !pPdhRemoveCounter || !pPdhCloseQuery) {
			FreeLibrary(hmodPDH);
			hmodPDH = NULL;
			return;
		}
	}

	if (pPdhOpenQueryW(NULL, 0, &hQueryDiskRate) != ERROR_SUCCESS) {
		hQueryDiskRate = NULL;
		FreeLibrary(hmodPDH);
		hmodPDH = NULL;
	}
}

int DiskRate_get(void)
{
	int i;

	if (!hQueryDiskRate || !pPdhCollectQueryData) return -1;

	for (i = 0; i < MAX_DISKRATE_DRIVE; i++) {
		if (actdvl[i] == 1 && !bDiskRateRegistered[i]) {
			DiskRate_try_register(i);
		}
		else if (actdvl[i] != 1 && !bDiskRateRegistered[i]) {
			diskRateRead[i] = 0.0;
			diskRateWrite[i] = 0.0;
			diskRateTotal[i] = 0.0;
		}
	}

	if (countDiskRateRegistered <= 0) return 0;
	if (pPdhCollectQueryData(hQueryDiskRate) != ERROR_SUCCESS) return -1;

	for (i = 0; i < MAX_DISKRATE_DRIVE; i++) {
		if (!bDiskRateRegistered[i]) continue;
		DiskRate_read_counter(hDiskRateRead[i], &diskRateRead[i]);
		DiskRate_read_counter(hDiskRateWrite[i], &diskRateWrite[i]);
		DiskRate_read_counter(hDiskRateTotal[i], &diskRateTotal[i]);
	}

	return 0;
}

void DiskRate_end(void)
{
	DiskRate_clear_all();

	if (hQueryDiskRate && pPdhCloseQuery) {
		pPdhCloseQuery(hQueryDiskRate);
	}
	hQueryDiskRate = NULL;

	if (hmodPDH) {
		FreeLibrary(hmodPDH);
		hmodPDH = NULL;
	}
	pPdhOpenQueryW = NULL;
	pPdhAddCounterW = NULL;
	pPdhCollectQueryData = NULL;
	pPdhGetFormattedCounterValue = NULL;
	pPdhRemoveCounter = NULL;
	pPdhCloseQuery = NULL;
}
