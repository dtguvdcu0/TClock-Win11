/*-------------------------------------------------------------------------
  hdd.c
  HDD の空き容量を取得する
---------------------------------------------------------------------------*/

#include <windows.h>
#include <iprtrmib.h>
#include "format_collect.h"

void Hdd_get(void);
void GetDiskSpace(int nDrive, ULONGLONG *nFree, ULONGLONG *nAll);
void GetDiskSpaceFromPath(const char *szDrive, ULONGLONG *nFree, ULONGLONG *nAll);
double diskFree[72];
double diskAll[72];

extern int actdvl[];

static void hdd_collect(BOOL legacy)
{
    UINT i;ULONGLONG provider=fcl_space_drives();
    extern char strAdditionalMountPath;
    for (i=0;i<36;i++) {
        BOOL selected=(provider & (1ULL << i))!=0;
        ULONGLONG freeDisk=0,allDisk=0;
        if (!legacy && !selected) continue;
        if (selected) {
            ULARGE_INTEGER available,total,freeBytes;
            WCHAR root[4]={L'A',L':',L'\\',0};
            LPCWSTR path=i<26 ? root : fcl_mount_path(i-26);
            if (i<26) root[0]=(WCHAR)(L'A'+i);
            if (path && path[0] && GetDiskFreeSpaceExW(path,&available,&total,&freeBytes)) {
                freeDisk=freeBytes.QuadPart;allDisk=total.QuadPart;
            }
        } else if (actdvl[i]==1) {
            if (i<26) GetDiskSpace((int)i,&freeDisk,&allDisk);
            else GetDiskSpaceFromPath(&strAdditionalMountPath+64*(i-26),&freeDisk,&allDisk);
        }
        diskFree[i]=(double)(LONGLONG)(freeDisk/1048576);
        diskFree[i+36]=diskFree[i]/1024;
        diskAll[i]=(double)(LONGLONG)(allDisk/1048576);
        diskAll[i+36]=diskAll[i]/1024;
    }
}
void Hdd_get(void) {hdd_collect(TRUE);}
void Hdd_sweep(void) {hdd_collect(FALSE);}

void GetDiskSpace(int nDrive, ULONGLONG *nFree, ULONGLONG *nAll)
{
	char    szDrive[32];
	ULARGE_INTEGER useByte, allByte, freeByte;

	wsprintf(szDrive, "%c:\\", nDrive + 'A');
	if (GetDiskFreeSpaceEx(szDrive, &useByte, &allByte, &freeByte) != 0)
	{
		*nFree = freeByte.QuadPart;
		*nAll = allByte.QuadPart;
	}
		//	return(allByte.QuadPart);

	return;
}


void GetDiskSpaceFromPath(const char *szDrive, ULONGLONG *nFree, ULONGLONG *nAll)
{
	ULARGE_INTEGER useByte, allByte, freeByte;


	if (GetDiskFreeSpaceEx(szDrive, &useByte, &allByte, &freeByte) != 0)
	{
		*nFree = freeByte.QuadPart;
		*nAll = allByte.QuadPart;
	};

//	return(allByte.QuadPart);
	return;
}
