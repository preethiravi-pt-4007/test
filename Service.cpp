#include <windows.h>
#include <iostream>
#include <atlstr.h>
#include <wtsapi32.h>
#include <Logger.cpp>
#include <map>

#define SERVICE_CONTROL_CUSTOM_MESSAGE 0x0085

using namespace std;

CLogger* LOGGER = CLogger::GetLogger("C:\\log\\Service.txt");

#define SERVICE_NAME  _T("Sample")

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = NULL;
map<CString, DWORD>   userlist;

CString GetUserNameWithSessionId(DWORD sessionId)
{
	LPTSTR szUserName = NULL;
	DWORD dwLen = 0;
	BOOL bStatus;
	CString username;

	bStatus = WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSUserName, &szUserName, &dwLen);

	if (bStatus)
	{
		username = CString(szUserName);
		WTSFreeMemory(szUserName);
	}
	else
	{
		LOGGER->Log("Error in Get_User_Name: %d", GetLastError());
	}
	return username;
}

map<CString, DWORD> GetActiveUsersList()
{
	DWORD count;
	PWTS_SESSION_INFOA pSessionInfo;
	map<CString, DWORD> activeUserList;

	if (WTSEnumerateSessionsA(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &count))
	{
		LOGGER->Log("List of active users: ");
		for (DWORD index = 0; index < count; index++)
		{
			if (pSessionInfo[index].State == WTSActive)
			{
				CString username = GetUserNameWithSessionId(pSessionInfo[index].SessionId);

				if (!username.IsEmpty())
				{
					activeUserList.insert({ username, pSessionInfo[index].SessionId });

					wstring ws = L"Username: ";
					ws = ws + username.GetBuffer();
					
					string name= string(ws.begin(), ws.end());

					LOGGER->Log(name);
					LOGGER->Log("SessionId: %d",pSessionInfo[index].SessionId);
				}
			}
		}
	}
	return activeUserList;
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
	//  Periodically check if the service has been requested to stop or not
	userlist = GetActiveUsersList();

	while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
	{
		//  Simulate some work by sleeping
		Sleep(3000);
	}
	return ERROR_SUCCESS;
}

BOOL IsRDPSession(DWORD sessionId)
{
	BOOL* isRDP = nullptr , res = TRUE;
	DWORD dataLen = 0;

	if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSIsRemoteSession, (LPWSTR*)&isRDP, &dataLen))
	{
		LOGGER->Log("WTSQuerySessionInformationW failed : %d ",GetLastError());
		res = FALSE;
	}

	WTSFreeMemory(isRDP);
	return res;

}

BOOL LaunchApplication(LPCWSTR path, DWORD sessionId)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	HANDLE htoken;
	DWORD pBytesReturned;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	if (WTSQueryUserToken(sessionId, &htoken))
	{
		CString username = GetUserNameWithSessionId(sessionId);

		if (!username.IsEmpty())
		{
			wstring ws = L"Username: ";
			ws = ws + username.GetBuffer();
			string name = string(ws.begin(), ws.end());
			LOGGER->Log(name);
		}

		si.wShowWindow = TRUE;

		if (CreateProcessAsUser(htoken, path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		{
			LOGGER->Log("Notepad opened successfully!!");
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return TRUE;
		}
		else
		{
			LOGGER->Log("CreateProcessAsUser failed. Error: %d", GetLastError());
		}
	}
	else
	{
		LOGGER->Log("Error in WTSQueryUserToken: %d", GetLastError());
		return FALSE;
	}

	CloseHandle(htoken);
}

VOID UserDefinedControl(DWORD sessionId)
{
	LOGGER->Log("SERVICE_CONTROL_CUSTOM_MESSAGE is requested");

	CString ucpTitle = _T("Request"), ucpMessage = _T("Do you want to open notepad");

	DWORD ucpResponse;

	if (sessionId == 0xFFFFFFFF)
	{
		LOGGER->Log("Error in getting Session id: %d ", GetLastError());
	}
	else
	{
		BOOL res = WTSSendMessage(NULL,
			sessionId,
			ucpTitle.GetBuffer(),
			wcslen(ucpTitle) * sizeof(WCHAR),
			ucpMessage.GetBuffer(),
			wcslen(ucpMessage) * sizeof(WCHAR),
			MB_ICONQUESTION | MB_YESNO | MB_TOPMOST,
			10,
			&ucpResponse,
			TRUE);

		if (res)
		{
			if (ucpResponse == 6)
			{
				if (LaunchApplication(L"C:\\Windows\\System32\\notepad.exe", sessionId))
				{
					LOGGER->Log("WTSSendMessage success in user defined control ");
				}
				else
				{
					LOGGER->Log("LaunchApplication failed in user defined control. Error: %d ", GetLastError());
				}
			}

		}
		else
		{
			LOGGER->Log("WTSSendMessage failed in user defined control. Error: %d ", GetLastError());
		}
	}
}

VOID Stop()
{
	LOGGER->Log("SERVICE_CONTROL_STOP is triggered");

	if (g_ServiceStatus.dwCurrentState == SERVICE_STOPPED)
	{
		LOGGER->Log("Service already been stopped");
		return;
	}
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 4;
	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		LOGGER->Log(" ServiceCtrlHandler: SetServiceStatus returned error in stop. Error: %d ", GetLastError());
	}
	// This will signal the worker thread to start shutting down
	SetEvent(g_ServiceStopEvent);
}

VOID WINAPI ServiceCtrlHandler(DWORD dwOpcode, DWORD dwEvtype, PVOID pEvtData, PVOID pContext)
{
	switch (dwOpcode)
	{
	case SERVICE_CONTROL_STOP:
	{
		Stop();
		break;
	}
	case SERVICE_CONTROL_CUSTOM_MESSAGE:
	{
		auto itr = userlist.find(CString("User1")) ;
		//auto itr = userlist.find(CString("User2"));
		//auto itr = userlist.find(CString("Administrator"));

		if ( itr != userlist.end() )
		{
			UserDefinedControl( itr->second );
		}
		else
		{
			LOGGER->Log("User not found: %d", GetLastError());
		}
		
		break;
	}
	case SERVICE_CONTROL_INTERROGATE:
	{
		LOGGER->Log("SERVICE_CONTROL_INTERROGATE is triggered");
		break;
	}
	case SERVICE_CONTROL_PRESHUTDOWN:
	{
		LOGGER->Log("SERVICE_CONTROL_PRESHUTDOWN is triggered");
		break;
	}
	case SERVICE_CONTROL_SHUTDOWN:
	{
		LOGGER->Log("SERVICE_CONTROL_SHUTDOWN is triggered");
		break;
	}
	case SERVICE_CONTROL_SESSIONCHANGE:
	{
		WTSSESSION_NOTIFICATION* pSessInfo = (WTSSESSION_NOTIFICATION*)pEvtData;
		userlist.clear();
		userlist = GetActiveUsersList();

		switch (dwEvtype)
		{
		case(WTS_SESSION_LOGON):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_SESSION_LOGON is triggered");
			break;
		}
		case(WTS_SESSION_LOCK):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_SESSION_LOCK is triggered");
			break;
		}
		case(WTS_SESSION_UNLOCK):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_SESSION_UNLOCK is triggered");
			break;
		}
		case(WTS_SESSION_LOGOFF):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_SESSION_LOGOFF is triggered");
			break;
		}
		case(WTS_REMOTE_CONNECT):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_REMOTE_CONNECT is triggered");
			//LOGGER->Log("User is in remote session");
			break;
		}
		case(WTS_CONSOLE_CONNECT):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_CONSOLE_CONNECT is triggered");
			break;
		}
		case(WTS_CONSOLE_DISCONNECT):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_CONSOLE_DISCONNECT  is triggered");
			break;
		}
		case(WTS_REMOTE_DISCONNECT):
		{
			LOGGER->Log("SERVICE_CONTROL_SESSIONCHANGE:WTS_REMOTE_DISCONNECT  is triggered");
			break;
		}
		}

		break;
	}
	default:
	{
		break;
	}
	}
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
	DWORD Status = NULL;

	g_StatusHandle = RegisterServiceCtrlHandlerEx(SERVICE_NAME, (LPHANDLER_FUNCTION_EX)ServiceCtrlHandler, 0);

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (g_StatusHandle == NULL)
	{
		return;
	}

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		LOGGER->Log("ServiceMain: SetServiceStatus returned error. Error: %d ", GetLastError());
	}

	// Create a service stop event to wait on later
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (g_ServiceStopEvent == NULL)
	{
		// Error creating event
		// Tell service controller we are stopped and exit
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			LOGGER->Log("ServiceMain: SetServiceStatus returned error. Error: %d ", GetLastError());
		}
		return;
	}

	// Tell the service controller we are started
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		LOGGER->Log("ServiceMain: SetServiceStatus returned error. Error: %d ", GetLastError());
	}

	HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	// Wait until our worker thread exits signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);

	CloseHandle(hThread);
	CloseHandle(g_ServiceStopEvent);

	// Tell the service controller we are stopped
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		LOGGER->Log("ServiceMain: SetServiceStatus returned error. Error: %d ", GetLastError());
	}
}

int main(int argc, TCHAR* argv[])
{
	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
		{NULL, NULL}
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}

