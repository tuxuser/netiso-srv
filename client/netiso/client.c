#include "common.h"
#include "client.h"
#include <winsockx.h>

#define SERVER_LISTEN_PORT		4323
#define CMD_MAGIC				0x49535652 // 'ISVR'
#define CMD_OK_MAGIC			0x6F6B4F4B // 'okOK'
#define DATA_TIMEOUT			15

extern char g_HostIp[];

enum {
	CMD_NOOP = 0,
	CMD_GEOMETRY,
	CMD_MEDIA_TYPE,
	CMD_READ,
	CMD_ISO_INDEX,
	CMD_ISO_MOUNT,
};

BOOL networkStarted = FALSE;
BOOL clientConnected = FALSE;
SOCKET sCli = INVALID_SOCKET;
// 4 byte magic, 1 byte command, 8 byte offset, 4 byte size
// 0 1 2 3        4           5 6 7 8 9 10 11 12  13 14 15 16
#pragma pack(push, 1)
typedef struct _CMD_STRUCT {
	DWORD magic; // 0x49535652 // 'ISVR'
	WORD command;
	WORD index;
	QWORD offset;
	DWORD size;
} CMD_STRUCT, *PCMD_STRUCT;
C_ASSERT(sizeof(CMD_STRUCT) == 0x14);
#pragma pack(pop)

BOOL setSockSecurity(SOCKET ss)
{
	BOOL btemp = TRUE;
	if(NetDll_setsockopt(XNCALLER_SYSAPP, ss, SOL_SOCKET, SO_MARKINSECURE, (PCSTR)&btemp, sizeof(BOOL) ) != 0 )//PATCHED!
	{
		sdbgPrint("Failed to set debug send socket SO_MARKINSECURE, error %d\n", WSAGetLastError());
		return FALSE;
	}
	if(NetDll_setsockopt(XNCALLER_SYSAPP, ss, SOL_SOCKET, SO_PRIVATE, (PCSTR)&btemp, sizeof(BOOL) ) != 0 )//PATCHED!
	{
		sdbgPrint("Failed to set debug send socket SO_PRIVATE, error %d\n", WSAGetLastError());
		return FALSE;
	}
	return TRUE;
}

BOOL clientNetStart(void)
{
	XNetStartupParams xnp;
	memset(&xnp, 0, sizeof(XNetStartupParams));
	xnp.cfgSizeOfStruct = sizeof(XNetStartupParams);
// 	xnp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;

	if(NetDll_XNetStartup(XNCALLER_SYSAPP, &xnp) == 0)
	{
		WSADATA wsaData;
		if(NetDll_WSAStartupEx(XNCALLER_SYSAPP, 2, &wsaData, 0x20352400) == 0)
		{
			int i;
			XNetConfigStatus xns;
			for(i = 0; i < 0x32; i++)
			{
				if(NetDll_XnpGetConfigStatus(XNCALLER_SYSAPP, &xns) != 0) // failed outright
					return FALSE;
				if((xns.dwFlags&1) == 1) // success!
					return TRUE;
				Sleep(64);
			}
			sdbgPrint("NetDll_XnpGetConfigStatus seems to have failed!\n");
		}
		else
			sdbgPrint("NetDll_WSAStartupEx failed!\n");
	}
	else
		sdbgPrint("NetDll_XNetStartup failed!\n");
	return FALSE;
}

BOOL clientConnect(IN_ADDR host)
{
	SOCKADDR_IN siCli;
	clientConnected = FALSE;
	sCli = NetDll_socket(XNCALLER_SYSAPP, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sCli == INVALID_SOCKET)
	{
		sdbgPrint("could not create client socket, error %d\n", WSAGetLastError());
		return FALSE;
	}
	setSockSecurity(sCli);
	siCli.sin_family = AF_INET;
	siCli.sin_port = htons(SERVER_LISTEN_PORT);
	siCli.sin_addr = host;

	if(NetDll_connect(XNCALLER_SYSAPP, sCli, (SOCKADDR*)&siCli, sizeof(SOCKADDR_IN)) < 0)
	{
		sdbgPrint("could not connect, error %d\n", WSAGetLastError());
		NetDll_closesocket(XNCALLER_SYSAPP, sCli);
		return FALSE;
	}
	clientConnected = TRUE;
	return TRUE;
}

int clientShutdown(void)
{
	if(clientConnected)
	{
		NetDll_closesocket(XNCALLER_SYSAPP, sCli);
		clientConnected = FALSE;
	}
	if(networkStarted)
		return -1;
	return -2;
}

// returns 1 on existing connection, 0 on new connection, -1 on no connection, -2 on no network
int clientStartup(void)
{
	if(networkStarted == FALSE)
		networkStarted = clientNetStart();
	if(networkStarted)
	{
		if(clientConnected == FALSE)
		{
			IN_ADDR hIa;
			// try to connect to server
			hIa.S_un.S_addr = inet_addr(g_HostIp);
			if(clientConnect(hIa))
			{
				sdbgPrint("connected to host %s\n", g_HostIp);
				return 0; // new connection
			}
			sdbgPrint("could not connect to host %s\n", g_HostIp);
			return -1; // no connection
		}
		return 1; // existing connection
	}
	return -2; // no network
}

int SocketReceiveData(void* data, int dwBytesToRead)
{
	int bRead = 0, ret;
	char *psz = (char*)data;
	TIMEVAL tv;
	fd_set fds;

	tv.tv_sec = DATA_TIMEOUT;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(sCli,&fds);
	ret = NetDll_select(XNCALLER_SYSAPP, 0, &fds, 0, 0, &tv);
	if(ret == SOCKET_ERROR) // disconnect
	{
// 		sdbgPrint("timeout waiting for data, %d\n", WSAGetLastError());
		clientConnected = FALSE;
		NetDll_closesocket(XNCALLER_SYSAPP, sCli);
		return -1; // Timeout
	}
	else if(ret == 0) // Timeout
	{
		return -1;
	}
	while(bRead < (int)dwBytesToRead)
	{
		ret = NetDll_recv(XNCALLER_SYSAPP, sCli, &psz[bRead], (dwBytesToRead-bRead), 0);
// 		sdbgPrint("rec %x bytes\n", ret);
		if(ret == SOCKET_ERROR)
		{
// 			sdbgPrint("socket error receiving data, %d\n", WSAGetLastError());
			clientConnected = FALSE;
			NetDll_closesocket(XNCALLER_SYSAPP, sCli);
			return -1; // Network error
		}
		bRead += ret;
	}
// 	sdbgPrint("rec ttl %x bytes\n", bRead);
	return bRead;
}

BOOL clientReceiveBuf(void* buf, int len)
{
	int ret = SocketReceiveData(buf, len);
	if(ret == len)
		return TRUE;
	return FALSE;
}


BOOL clientSendData(void* buf, DWORD buflen)
{
	int ret = NetDll_send(XNCALLER_SYSAPP, sCli, (char*)buf, buflen, 0);
	if(ret == SOCKET_ERROR)
	{
		sdbgPrint("clientSendCommand failed with error %d\n", WSAGetLastError());
		clientConnected = FALSE;
		NetDll_closesocket(XNCALLER_SYSAPP, sCli);
		return FALSE;
	}
	return TRUE;
}

BOOL clientSendCommand(void* buf)
{
	return clientSendData(buf, sizeof(CMD_STRUCT));
}

// this is what is called when sense is called
BOOL clientGetGeometry(BYTE* geometryBuf)
{
	CMD_STRUCT cmd;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_GEOMETRY;
	cmd.size = sizeof(DISK_GEOMETRY);
	cmd.offset = 0ULL;
	if(clientSendCommand(&cmd))
	{
		if(clientReceiveBuf(geometryBuf, sizeof(DISK_GEOMETRY)))
			return TRUE;
	}
	return FALSE;
}

// this is what is called when sense is called
BOOL clientGetDiskType(DWORD* typeBuf)
{
	CMD_STRUCT cmd;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_MEDIA_TYPE;
	cmd.size = sizeof(DWORD);
	cmd.offset = 0ULL;
	if(clientSendCommand(&cmd))
	{
		if(clientReceiveBuf(typeBuf, sizeof(DWORD)))
			return TRUE;
	}
	return FALSE;
}

BOOL clientGetPing(void)
{
	CMD_STRUCT cmd;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_NOOP;
	cmd.size = sizeof(DISK_GEOMETRY);
	cmd.offset = 0ULL;
// 	sdbgPrint("pinging client!\n");
	if(clientSendCommand(&cmd))
	{
		DWORD outBuf[2];
		if(clientReceiveBuf(outBuf, 8))
		{
			if((outBuf[0] == CMD_MAGIC)&&(outBuf[1] == CMD_OK_MAGIC))
			{
// 				sdbgPrint("client ping passed!\n");
				return TRUE;
			}
// 			sdbgPrint("received ping but... 0:%08x 1:%08x!\n", outBuf[0], outBuf[1]);
		}
	}
	sdbgPrint("client ping failed!\n");
	return FALSE;
}

DWORD clientReadDisk(DWORD sector, DWORD len, BYTE* outbuf)
{
	CMD_STRUCT cmd;
	QWORD offset = sector;
	offset <<= 11;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_READ;
	cmd.size = len;
	cmd.offset = offset;
// 	sdbgPrint("clientReadDisk sending command: offset 0x%I64x len: 0x%x\n", cmd.offset, cmd.size);
	if(clientSendCommand(&cmd))
	{
// 		sdbgPrint("clientReadDisk sent command, read resp\n");
		if(clientReceiveBuf(outbuf, len))
		{
// 			sdbgPrint("clientReadDisk read complete\n");
			return len;
		}
		else
			sdbgPrint("clientReadDisk receive buffer failed\n");
	}
	else
		sdbgPrint("clientReadDisk send command failed\n");
	return 0;
}

BOOL clientGetIndex(WORD index, DWORD maxOutLen, BYTE* outbuf)
{
	CMD_STRUCT cmd;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_ISO_INDEX;
	cmd.size = maxOutLen;
	cmd.index = index;
	// 	sdbgPrint("clientReadDisk sending command: offset 0x%I64x len: 0x%x\n", cmd.offset, cmd.size);
	if(clientSendCommand(&cmd))
	{
		// 		sdbgPrint("clientReadDisk sent command, read resp\n");
		if(clientReceiveBuf(outbuf, maxOutLen))
		{
			// 			sdbgPrint("clientReadDisk read complete\n");
			return TRUE;
		}
		else
			sdbgPrint("clientGetIndex receive buffer failed\n");
	}
	else
		sdbgPrint("clientGetIndex send command failed\n");
	return FALSE;
}

BOOL clientMountIso(DWORD nameLen, BYTE* nameBuf)
{
	CMD_STRUCT cmd;
	cmd.magic = CMD_MAGIC;
	cmd.command = CMD_ISO_MOUNT;
	cmd.size = nameLen;
	// 	sdbgPrint("clientReadDisk sending command: offset 0x%I64x len: 0x%x\n", cmd.offset, cmd.size);
	if(clientSendCommand(&cmd))
	{
		if(clientSendData(nameBuf, nameLen))
		{
			DWORD res;
			if(clientReceiveBuf(&res, 4))
			{
				return res&1;
			}
			else
				sdbgPrint("clientMountIso receive reply failed\n");
		}
		else
			sdbgPrint("clientMountIso send buffer failed\n");
	}
	else
		sdbgPrint("clientMountIso send command failed\n");
	return FALSE;
}
