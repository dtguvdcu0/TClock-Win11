/*-------------------------------------------------------------------------
  net.c
  get network interface info
---------------------------------------------------------------------------*/

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <string.h>

#define TRAFFIC_THRESHOLD	256

void Net_start(void);
void Net_get(void);
void Net_end(void);
void Net_clear_Win10(void);
BOOL Net_getIP_Win10(void);
void Net_getRecvSend_Win10(double*, double*, double*, double*, double*, double*, double*, double*);
extern BOOL b_CompactMode;
extern BOOL IsMinimalMode(void);
extern BOOL net_start_profiles(void);
extern void net_stop_profiles(void);
extern BOOL net_refresh_profiles(void);
void Net_UpdateInterfaceTable(void);

static HMODULE hmodIPHLP = NULL;
static HGLOBAL buffer = NULL;
static ULONG g_netTableCapacity = 0;
static MIB_IFROW *ifr;
static MIB_IFTABLE *ift;
static int count;
static int sec = 5;


//net[21] was changed from double to unsigned int on 20191024, it may cause any unintentional reaction!!
double net[21] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; //Extended for Ether/WiFi/LTE indicator by TTTT
int ifIndex_Ether, ifIndex_WiFi, ifIndex_LTE, ifIndex_VPN;

typedef DWORD (WINAPI *pfnGetIfTable)(PMIB_IFTABLE,PULONG,BOOL);
typedef DWORD (WINAPI *pfnGetIfEntry)(PMIB_IFROW);
static pfnGetIfTable pGetIfTable;
static pfnGetIfEntry pGetIfEntry;

static volatile LONG g_netDirty = 1;
static ULONGLONG g_netLastRefresh = 0, g_netRetryAfter = 0;
static BOOL g_netHasRefresh = FALSE, g_netWatchReady = FALSE;
static HANDLE g_netAddressWatch = NULL, g_netInterfaceWatch = NULL, g_netRouteWatch = NULL;
typedef DWORD (WINAPI *NET_ADDRESS_NOTIFY)(ADDRESS_FAMILY, PUNICAST_IPADDRESS_CHANGE_CALLBACK, PVOID, BOOLEAN, HANDLE*);
typedef DWORD (WINAPI *NET_INTERFACE_NOTIFY)(ADDRESS_FAMILY, PIPINTERFACE_CHANGE_CALLBACK, PVOID, BOOLEAN, HANDLE*);
typedef DWORD (WINAPI *NET_ROUTE_NOTIFY)(ADDRESS_FAMILY, PIPFORWARD_CHANGE_CALLBACK, PVOID, BOOLEAN, HANDLE*);
typedef DWORD (WINAPI *NET_CANCEL_NOTIFY)(HANDLE);
static NET_CANCEL_NOTIFY g_netCancelNotify = NULL;

extern LONG GetMyRegLong(const char* section, const char* entry, LONG defval);

//extern BOOL b_UseDataPlanFunction;

BOOL flag_Ether, flag_WiFi, flag_LTE, flag_VPNCheck;
BOOL flag_VPN = FALSE;

int active_physical_adapter_Win10 = -1; //-1: No active adapter, 0: Ether, 1: WiFi, 2: LTE

extern char strVPN_Keywords[];
extern char strVPN_Excludes[];
extern char strEthernet_Keywords[];

// IP addresses added by TTTT
extern char ipLTE[];
extern char ipEther[];
extern char ipWiFi[];
extern char ipVPN[];

extern BOOL b_DebugLog;
void writeDebugLog_Win10(LPSTR s, int n);


#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))


/* Variables used by GetIpAddrTable */
PMIB_IPADDRTABLE pIPAddrTable;
DWORD dwSize = 32 * sizeof(MIB_IPADDRTABLE);
DWORD dwRetVal = 0;

double recv_old, send_old;

int number_ActiveNet;
int number_ActiveNet_old;

static BOOL HasKeyword(const char* text, const char* keyword)
{
	return (text != NULL && keyword != NULL && keyword[0] != '\0' && strstr(text, keyword) != NULL) ? TRUE : FALSE;
}

static BOOL HasKeywordCsv(const char* text, const char* csv)
{
	const char* token;
	const char* end;
	const char* start;
	int len;
	char keyword[128];

	if (text == NULL || csv == NULL || csv[0] == '\0') return FALSE;

	token = csv;
	while (*token != '\0') {
		end = token;
		while (*end != '\0' && *end != ',') end++;

		start = token;
		while (start < end && (*start == ' ' || *start == '\t')) start++;
		while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;

		len = (int)(end - start);
		if (len > 0) {
			if (len >= (int)sizeof(keyword)) len = (int)sizeof(keyword) - 1;
			CopyMemory(keyword, start, len);
			keyword[len] = '\0';
			if (strstr(text, keyword) != NULL) return TRUE;
		}

		token = (*end == ',') ? (end + 1) : end;
	}

	return FALSE;
}


typedef DWORD(WINAPI *pfnGetIpAddrTable)(PMIB_IPADDRTABLE, PULONG, BOOL);

static pfnGetIpAddrTable pGetIpAddrTable;

char  strNetworkAdapterDescriptor[MAXLEN_IFDESCR];

extern const int kMegabytesPerGigabyte;
int bytesInGigaByte;

static BOOL IsUnsetIP(const char* s)
{
	if (s == NULL) return TRUE;
	return (s[0] == '-' && s[1] == '-' && s[2] == '-');
}

static void FormatIPv4Address(DWORD ipv4, char* outBuf, size_t outBufLen)
{
	sprintf_s(outBuf, outBufLen, "%3d.%3d.%3d.%3d",
		(int)((LOWORD(ipv4) & 0x00FF)),
		(int)((LOWORD(ipv4) & 0xFF00) >> 8),
		(int)((HIWORD(ipv4) & 0x00FF)),
		(int)((HIWORD(ipv4) & 0xFF00) >> 8));
}

static BOOL IsUsableIPv4(DWORD ipv4)
{
	BYTE o1 = (BYTE)(ipv4 & 0xFF);
	BYTE o2 = (BYTE)((ipv4 >> 8) & 0xFF);
	if (ipv4 == 0) return FALSE;
	// Skip loopback and APIPA.
	if (o1 == 127) return FALSE;
	if (o1 == 169 && o2 == 254) return FALSE;
	return TRUE;
}



BOOL Net_getIP_Win10(void)
{
	char addresses[4][32] = { "--- --- --- ---", "--- --- --- ---", "--- --- --- ---", "--- --- --- ---" };
	ULONG size = dwSize;
	DWORD result, i;
	if (!ift || !hmodIPHLP || !pGetIpAddrTable) return FALSE;
	if (!pIPAddrTable) pIPAddrTable = (MIB_IPADDRTABLE*)MALLOC(dwSize);
	if (!pIPAddrTable) return FALSE;
	result = pGetIpAddrTable(pIPAddrTable, &size, FALSE);
	if (result == ERROR_INSUFFICIENT_BUFFER) {
		MIB_IPADDRTABLE* resized = (MIB_IPADDRTABLE*)MALLOC(size);
		if (!resized) return FALSE;
		FREE(pIPAddrTable); pIPAddrTable = resized; dwSize = size;
		result = pGetIpAddrTable(pIPAddrTable, &size, FALSE);
	}
	if (result != NO_ERROR) {
		if (b_DebugLog) writeDebugLog_Win10("[net.c] GetIpAddrTable failed =", (int)result);
		return FALSE; /* Preserve the last valid address snapshot on transient errors. */
	}
	for (i = 0; i < pIPAddrTable->dwNumEntries; ++i) {
		DWORD index = pIPAddrTable->table[i].dwIndex;
		DWORD address = pIPAddrTable->table[i].dwAddr;
		int slot = index == (DWORD)ifIndex_Ether ? 0 : index == (DWORD)ifIndex_WiFi ? 1 :
			index == (DWORD)ifIndex_LTE ? 2 : index == (DWORD)ifIndex_VPN ? 3 : -1;
		if (IsUsableIPv4(address) && slot >= 0) FormatIPv4Address(address, addresses[slot], sizeof(addresses[slot]));
	}
	/* Compatibility fallback uses the same interface snapshot, without per-address queries. */
	for (i = 0; i < pIPAddrTable->dwNumEntries; ++i) {
		DWORD index = pIPAddrTable->table[i].dwIndex;
		DWORD address = pIPAddrTable->table[i].dwAddr, row;
		int slot = -1;
		if (!IsUsableIPv4(address)) continue;
		for (row = 0; row < ift->dwNumEntries; ++row) {
			const MIB_IFROW* entry = &ift->table[row];
			if (entry->dwIndex != index || entry->dwOperStatus != IF_OPER_STATUS_OPERATIONAL) continue;
			if (entry->dwType == IF_TYPE_WWANPP || entry->dwType == IF_TYPE_WWANPP2) slot = 2;
			else if (entry->dwType == IF_TYPE_IEEE80211) slot = 1;
			else if (entry->dwType == MIB_IF_TYPE_ETHERNET) slot = 0;
			break;
		}
		if (slot >= 0 && IsUnsetIP(addresses[slot])) FormatIPv4Address(address, addresses[slot], sizeof(addresses[slot]));
	}
	/* Existing formatter boundary stores IPv4 display strings, never paths or commands. */
	strcpy_s(ipEther, 32, addresses[0]); strcpy_s(ipWiFi, 32, addresses[1]);
	strcpy_s(ipLTE, 32, addresses[2]); strcpy_s(ipVPN, 32, addresses[3]);
	return TRUE;
}



void Net_clear_Win10(void)
{
	net[2] = 0;
	net[3] = 0;

	//Added for LTE indicator by TTTT (3 lines)
	net[12] = 0;
	//Added for Ether / WiFi indicator by TTTT (6 lines)
	net[15] = 0;
	net[18] = 0;

	flag_Ether = flag_WiFi = flag_LTE = flag_VPNCheck = FALSE;
	flag_VPN = FALSE;

	active_physical_adapter_Win10 = -1;

	ifIndex_Ether = ifIndex_WiFi = ifIndex_LTE = ifIndex_VPN = -1;

	/* Address caches are updated independently by net_poll_connection. */
}

void net_mark_changed(void)
{
	/* OS callbacks never query profiles, touch UI, or acquire the clock thread's resources. */
	InterlockedExchange(&g_netDirty, 1);
}

static void CALLBACK net_notify_address(PVOID context, PMIB_UNICASTIPADDRESS_ROW row, MIB_NOTIFICATION_TYPE type)
{
	UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(row); UNREFERENCED_PARAMETER(type);
	net_mark_changed();
}

static void CALLBACK net_notify_interface(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
	UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(row); UNREFERENCED_PARAMETER(type);
	net_mark_changed();
}

static void CALLBACK net_notify_route(PVOID context, PMIB_IPFORWARD_ROW2 row, MIB_NOTIFICATION_TYPE type)
{
	UNREFERENCED_PARAMETER(context); UNREFERENCED_PARAMETER(row); UNREFERENCED_PARAMETER(type);
	net_mark_changed();
}

static void net_start_watch(void)
{
	NET_ADDRESS_NOTIFY addressNotify = (NET_ADDRESS_NOTIFY)GetProcAddress(hmodIPHLP, "NotifyUnicastIpAddressChange");
	NET_INTERFACE_NOTIFY interfaceNotify = (NET_INTERFACE_NOTIFY)GetProcAddress(hmodIPHLP, "NotifyIpInterfaceChange");
	NET_ROUTE_NOTIFY routeNotify = (NET_ROUTE_NOTIFY)GetProcAddress(hmodIPHLP, "NotifyRouteChange2");
	BOOL profilesReady = TRUE;
	g_netCancelNotify = (NET_CANCEL_NOTIFY)GetProcAddress(hmodIPHLP, "CancelMibChangeNotify2");
	if (g_netCancelNotify) {
		if (addressNotify) addressNotify(AF_UNSPEC, net_notify_address, NULL, FALSE, &g_netAddressWatch);
		if (interfaceNotify) interfaceNotify(AF_UNSPEC, net_notify_interface, NULL, FALSE, &g_netInterfaceWatch);
		if (routeNotify) routeNotify(AF_UNSPEC, net_notify_route, NULL, FALSE, &g_netRouteWatch);
	}
	if (!b_CompactMode && !IsMinimalMode()) profilesReady = net_start_profiles();
	g_netWatchReady = g_netAddressWatch && g_netInterfaceWatch && g_netRouteWatch && profilesReady;
	g_netHasRefresh = FALSE;
	g_netRetryAfter = 0;
	net_mark_changed();
	if (b_DebugLog) writeDebugLog_Win10("[net.c] Change notification coverage ready =", g_netWatchReady);
}

static void net_stop_watch(void)
{
	HANDLE* handles[] = { &g_netAddressWatch, &g_netInterfaceWatch, &g_netRouteWatch };
	int i;
	/* Cancellation waits for callbacks; no callback depends on this thread. */
	net_stop_profiles();
	for (i = 0; i < (int)_countof(handles); ++i) {
		if (*handles[i] && g_netCancelNotify) {
			DWORD result = g_netCancelNotify(*handles[i]);
			if (result != NO_ERROR && b_DebugLog) writeDebugLog_Win10("[net.c] Cancel notification error =", (int)result);
			*handles[i] = NULL;
		}
	}
	g_netCancelNotify = NULL;
	g_netWatchReady = FALSE;
	g_netHasRefresh = FALSE;
	InterlockedExchange(&g_netDirty, 0);
}

BOOL net_poll_connection(BOOL profiles)
{
	ULONGLONG now = GetTickCount64();
	ULONGLONG elapsed = now - g_netLastRefresh;
	ULONGLONG interval = g_netWatchReady ? 30000 : 5000;
	double recv, send, recvWAN, sendWAN, recvWiFi, sendWiFi, recvEther, sendEther;
	double activityEther = net[18], activityWiFi = net[15], activityLTE = net[12];
	BOOL success;
	if (!hmodIPHLP || !pGetIfTable || now < g_netRetryAfter) return FALSE;
	if (g_netHasRefresh && elapsed < interval &&
		(!InterlockedCompareExchange(&g_netDirty, 0, 0) || elapsed < 2000)) return FALSE;
	/* Clear before querying so notifications arriving during collection are retained. */
	InterlockedExchange(&g_netDirty, 0);
	g_netLastRefresh = now;
	g_netHasRefresh = TRUE;
	Net_UpdateInterfaceTable();
	if (!ift) { g_netRetryAfter = now + 5000; net_mark_changed(); return FALSE; }
	net[12] = net[15] = net[18] = 0;
	ifIndex_Ether = ifIndex_WiFi = ifIndex_LTE = ifIndex_VPN = -1;
	Net_getRecvSend_Win10(&recv, &send, &recvWAN, &sendWAN, &recvWiFi, &sendWiFi, &recvEther, &sendEther);
	/* A metadata refresh must not reset traffic totals, baselines, or activity indicators. */
	if (net[18] && activityEther == 2) net[18] = 2;
	if (net[15] && activityWiFi == 2) net[15] = 2;
	if (net[12] && activityLTE == 2) net[12] = 2;
	active_physical_adapter_Win10 = flag_Ether ? 0 : flag_WiFi ? 1 : flag_LTE ? 2 : -1;
	success = Net_getIP_Win10();
	if (profiles && !net_refresh_profiles()) success = FALSE;
	g_netLastRefresh = GetTickCount64();
	g_netRetryAfter = success ? 0 : g_netLastRefresh + 5000;
	if (!success) net_mark_changed();
	if (b_DebugLog) writeDebugLog_Win10("[net.c] Connection snapshot query elapsed(ms) =", (int)(GetTickCount64() - now));
	return TRUE;
}

void Net_start(void)
{
	if (hmodIPHLP) Net_end();
	recv_old = send_old = 0;
	net[0] = net[1] = 0;
	bytesInGigaByte = 1024 * 1024 * kMegabytesPerGigabyte;
	number_ActiveNet = number_ActiveNet_old = 0;
	ifr = NULL; ift = NULL;
	hmodIPHLP = LoadLibraryW(L"iphlpapi.dll");
	if (!hmodIPHLP) return;
	pGetIfTable = (pfnGetIfTable)GetProcAddress(hmodIPHLP, "GetIfTable");
	pGetIfEntry = (pfnGetIfEntry)GetProcAddress(hmodIPHLP, "GetIfEntry");
	pGetIpAddrTable = (pfnGetIpAddrTable)GetProcAddress(hmodIPHLP, "GetIpAddrTable");
	if (!pGetIfTable || !pGetIfEntry) { Net_end(); return; }
	Net_UpdateInterfaceTable();
	if (!ift) { Net_end(); return; }
	sec = 1;
	Net_get();
	net_start_watch();
	net_poll_connection(!b_CompactMode && !IsMinimalMode());
}




BOOL Net_getAdapterDescriptor(GUID targetGUID)
{
	if (b_DebugLog) writeDebugLog_Win10("[net.c] Net_getAdapterDescriptor called", 999);

	strNetworkAdapterDescriptor[0] = '\0';
	if (ift && hmodIPHLP)
	{
		int i, j;
		WCHAR tempwszName[MAX_INTERFACE_NAME_LEN];
		count = ift->dwNumEntries;
		char char_tempwszName[64];
		char strTempGUID[64];
		wsprintf(strTempGUID, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"
			, targetGUID.Data1, targetGUID.Data2, targetGUID.Data3
			, targetGUID.Data4[0], targetGUID.Data4[1], targetGUID.Data4[2], targetGUID.Data4[3]
			, targetGUID.Data4[4], targetGUID.Data4[5], targetGUID.Data4[6], targetGUID.Data4[7]);

		if (b_DebugLog) writeDebugLog_Win10(strTempGUID, 999);

		for (i = 0; i < count; i++)
		{
			ifr = (ift->table) + i;
			pGetIfEntry(ifr);

			wcscpy_s(tempwszName, MAX_INTERFACE_NAME_LEN, ifr->wszName);

			for (j = 0; j < 36; j++)
			{
				char_tempwszName[j] = (char)tempwszName[j + 15];
				//if (strNetworkAdapterDescriptor[j] == '}')
				//{
				//	strNetworkAdapterDescriptor[j] = '\0';
				//	break;
				//}
			}
			char_tempwszName[36] = '\0';

			//if (b_DebugLog)
			//{
			//	writeDebugLog_Win10(char_tempwszName, i);
			//}

			if (!strncmp(char_tempwszName, strTempGUID, 36))
			{
				strncpy_s(strNetworkAdapterDescriptor, MAXLEN_IFDESCR, (const char*)ifr->bDescr, _TRUNCATE);
				if (b_DebugLog)
				{
					char tempStr[MAXLEN_IFDESCR + 64];
					wsprintf(tempStr, "Network Adapter: %s", strNetworkAdapterDescriptor);
					writeDebugLog_Win10(tempStr, 999);
				}
				return 1;
			}
		}
	}
	return 0;
}


void Net_getRecvSend_Win10(double* recv, double* send, double* recvWAN, double* sendWAN, double* recvWiFi, double* sendWiFi, double* recvEther, double* sendEther)
{

	flag_Ether = flag_WiFi = flag_LTE = flag_VPNCheck = FALSE;
	flag_VPN = FALSE;
	number_ActiveNet = 0;



	if (ift && hmodIPHLP)
	{
		int i;

		*recv = *send = 0;
		*recvWAN = *sendWAN = 0;	//Added for LTE indicator by TTTT
		*recvWiFi = *sendWiFi = 0;	//Added for WiFi indicator by TTTT
		*recvEther = *sendEther = 0;	//Added for Ethernet indicator by TTTT

		count = ift->dwNumEntries;


		for (i = 0; i < count; i++)
		{
			ifr = (ift->table) + i;
			/* GetIfTable already supplied this row's status and counters. */
			//Added for LTE indicator by TTTT
			if (!flag_LTE)
			{
				if (((ifr->dwType == IF_TYPE_WWANPP ||
					ifr->dwType == IF_TYPE_WWANPP2)) &&
					(ifr->dwOperStatus == IF_OPER_STATUS_OPERATIONAL))
				{
					flag_LTE = TRUE;
					number_ActiveNet++;

//					pGetIfEntry(ifr);
					*recv += ifr->dwInOctets;
					*send += ifr->dwOutOctets;

					net[12] = 1;
					*recvWAN += ifr->dwInOctets;
					*sendWAN += ifr->dwOutOctets;

					ifIndex_LTE = (int)ifr->dwIndex;

				}
			}

			//Added for WiFi indicator by TTTT
			if (!flag_WiFi)
			{
				if ((ifr->dwType == IF_TYPE_IEEE80211) &&
					(ifr->dwOperStatus == IF_OPER_STATUS_OPERATIONAL))
				{
					flag_WiFi = TRUE;
					number_ActiveNet++;


//					pGetIfEntry(ifr);
					*recv += ifr->dwInOctets;
					*send += ifr->dwOutOctets;

					net[15] = 1;
					*recvWiFi += ifr->dwInOctets;
					*sendWiFi += ifr->dwOutOctets;

					ifIndex_WiFi = (int)ifr->dwIndex;


				}
			}


			//Added for Ethenet indicator by TTTT
			if (!flag_Ether)
			{
				const char* ifDescr = (const char*)ifr->bDescr;
				if ((ifr->dwType == MIB_IF_TYPE_ETHERNET) &&
					(	(strstr(ifDescr, "Ethernet") != NULL)
						|| (strstr(ifDescr, "Realtek") != NULL)
						|| (strstr(ifDescr, "BUFFALO") != NULL)
						|| (strstr(ifDescr, "Buffalo") != NULL)
						|| (strstr(ifDescr, "I-O DATA") != NULL)
						|| (strstr(ifDescr, "Qualcomm") != NULL)
						|| HasKeywordCsv(ifDescr, strEthernet_Keywords)
						) &&
					(strstr(ifDescr, "Virtual") == NULL) &&
					(strstr(ifDescr, "Scheduler") == NULL) &&
					(ifr->dwOperStatus == IF_OPER_STATUS_OPERATIONAL))
				{
					flag_Ether = TRUE;
					number_ActiveNet++;


//					pGetIfEntry(ifr);
					*recv += ifr->dwInOctets;
					*send += ifr->dwOutOctets;

					net[18] = 1;
					*recvEther += ifr->dwInOctets;
					*sendEther += ifr->dwOutOctets;

					ifIndex_Ether = (int)ifr->dwIndex;


				}
			}

			//Added for VPN接続 indicator by TTTT
			if (!flag_VPNCheck)
			{
				const char* ifDescr = (const char*)ifr->bDescr;
//				if ((ifr->dwType == MIB_IF_TYPE_ETHERNET) &&
				//if (((ifr->dwType == MIB_IF_TYPE_ETHERNET) 
				//		|| (ifr->dwType == MIB_IF_TYPE_PPP))
				//	&&
				if (
					HasKeywordCsv(ifDescr, strVPN_Keywords) &&
					(!HasKeywordCsv(ifDescr, strVPN_Excludes)) &&
						(ifr->dwOperStatus == IF_OPER_STATUS_OPERATIONAL))
				{
					flag_VPNCheck = TRUE;
					number_ActiveNet++;

					flag_VPN = TRUE;

					ifIndex_VPN = (int)ifr->dwIndex;


				}
			}

		}
	}
	else
	{
		*recv = *send = 0;
		*recvWAN = *sendWAN = 0;	//Added for LTE indicator by TTTT
		*recvWiFi = *sendWiFi = 0;	//Added for WiFi indicator by TTTT
		*recvEther = *sendEther = 0;	//Added for Ethernet indicator by TTTT
	}

}


void Net_get(void)
{

	extern BOOL b_DebugLog;
	//if (b_DebugLog) writeDebugLog_Win10("[net.c] Net_get called", 999);


	if (ift && hmodIPHLP)
	{
		if (sec <= 0) sec = 5;

		Net_clear_Win10();


		double recv, send;
		double recvWAN, sendWAN; //Added for LTE indicator by TTTT
		double recvWiFi, sendWiFi; //Added for WiFi indicator by TTTT
		double recvEther, sendEther; //Added for Ether indicator by TTTT

		Net_UpdateInterfaceTable(); //Added to conform dyanamic change of interface table by TTTT 20190723
		if (!ift) return;

		Net_getRecvSend_Win10(&recv, &send, &recvWAN, &sendWAN, &recvWiFi, &sendWiFi, &recvEther, &sendEther); //Added for Ether/WiFi/LTE indicator by TTTT

		net[2] = (recv - recv_old) / sec;

		net[3] = (send - send_old) / sec;

		if (number_ActiveNet != number_ActiveNet_old)
		{
			net[2] = 0;
			net[3] = 0;
		}

		if ((net[2] < 0) || (net[2] > bytesInGigaByte)) net[2] = 0;
		if ((net[3] < 0) || (net[3] > bytesInGigaByte)) net[3] = 0;

		number_ActiveNet_old = number_ActiveNet;

		recv_old = recv;
		send_old = send;


		// Traffic totals and rates use decimal SI units; raw counters remain bytes.
		net[6] = net[2] / 1000.0;
		net[7] = net[3] / 1000.0;

		net[10] = net[2] / 1000000.0;
		net[11] = net[3] / 1000000.0;

		net[0] += (net[2] * sec);
		net[1] += (net[3] * sec);

		net[4] = net[0] / 1000.0;
		net[5] = net[1] / 1000.0;

		net[8] = net[0] / 1000000.0;
		net[9] = net[1] / 1000000.0;






		//Added for LTE indicator by TTTT (3 lines)
		if ((recvWAN - net[13]) > TRAFFIC_THRESHOLD) net[12] = 2;
		if ((sendWAN - net[14]) > TRAFFIC_THRESHOLD) net[12] = 2;
		net[13] = recvWAN;
		net[14] = sendWAN;
		//Added for WiFi indicator by TTTT (3 lines)
		if ((recvWiFi - net[16]) > TRAFFIC_THRESHOLD) net[15] = 2;
		if ((sendWiFi - net[17]) > TRAFFIC_THRESHOLD) net[15] = 2;
		net[16] = recvWiFi;
		net[17] = sendWiFi;

		if (((recvEther - net[19]) > TRAFFIC_THRESHOLD) || ((sendEther - net[20]) > TRAFFIC_THRESHOLD))
		{
			if (net[18] == 1) net[18] = 2;
		}

		net[19] = recvEther;
		net[20] = sendEther;

		active_physical_adapter_Win10 = -1;
		if (net[18] > 0) active_physical_adapter_Win10 = 0;
		else if (net[15] > 0)active_physical_adapter_Win10 = 1;
		else if (net[12] > 0)active_physical_adapter_Win10 = 2;

		/* Local addresses are refreshed on notifications, independently of traffic sampling. */

	}
	
}

void Net_end(void)
{
	net_stop_watch();
	if(hmodIPHLP)
	{
		FreeLibrary(hmodIPHLP);
	}
	if(buffer)
	{
		GlobalFree(buffer);
	}

	hmodIPHLP = NULL; buffer = NULL;
	ifr = NULL; ift = NULL; g_netTableCapacity = 0;

	if (pIPAddrTable != NULL)
	{
		FREE(pIPAddrTable);
		pIPAddrTable = NULL;
	}
}

//void Net_restart(void)
//{
//	DWORD bufsize;
//	DWORD iftable;
//	int i;
//
//
//	GlobalFree(buffer); buffer=NULL;
//	if(pGetIfTable == NULL || pGetIfEntry == NULL)return;
//	bufsize = 0;
//	iftable = pGetIfTable((PMIB_IFTABLE)buffer, &bufsize, TRUE);
//	if(iftable != ERROR_INSUFFICIENT_BUFFER)
//	{
//		FreeLibrary(hmodIPHLP); hmodIPHLP = NULL;
//		return;
//	}
//	buffer = GlobalAlloc(GPTR, bufsize);
//	if(buffer == NULL)return;
//	iftable = pGetIfTable((PMIB_IFTABLE)buffer, &bufsize, TRUE);
//	if(iftable != NO_ERROR)
//	{
//		GlobalFree(buffer); buffer = NULL;
//		return;
//	}
//	ift = (MIB_IFTABLE *)buffer;
//	count = ift->dwNumEntries;
//
//	Net_get();
//
//	sec = 1;
//}


void Net_UpdateInterfaceTable(void)
{
	ULONG size = g_netTableCapacity;
	DWORD result;
	if (!hmodIPHLP || !pGetIfTable) return;
	ifr = NULL; ift = NULL;
	result = pGetIfTable((PMIB_IFTABLE)buffer, &size, TRUE);
	if (result == ERROR_INSUFFICIENT_BUFFER) {
		HGLOBAL resized = GlobalAlloc(GPTR, size);
		if (!resized) return;
		if (buffer) GlobalFree(buffer);
		buffer = resized; g_netTableCapacity = size;
		result = pGetIfTable((PMIB_IFTABLE)buffer, &size, TRUE);
	}
	if (result != NO_ERROR) return;
	ift = (MIB_IFTABLE*)buffer;
	count = ift->dwNumEntries;
}
