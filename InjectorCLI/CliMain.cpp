#include <Windows.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>
#include <Scylla/Logger.h>
#include <Scylla/NtApiLoader.h>
#include <Scylla/PebHider.h>
#include <Scylla/Settings.h>
#include <Scylla/Util.h>

#include "DynamicMapping.h"
#include "..\HookLibrary\HookMain.h"
#include "RemoteHook.h"
#include "ApplyHooking.h"
#include "../PluginGeneric/Injector.h"

scl::Settings g_settings;
scl::Logger g_log;
std::wstring g_ntApiCollectionIniPath;
std::wstring g_scyllaHideIniPath;

HOOK_DLL_DATA g_hdd;


void ChangeBadWindowText();
void ReadSettings();
DWORD GetProcessIdByName(const WCHAR * processName);
void startInjection(DWORD targetPid, const WCHAR * dllPath);
bool SetDebugPrivileges();
void startInjectionProcess(HANDLE hProcess, BYTE * dllMemory);
bool StartHooking(HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase);

#define PREFIX_PATH L"C:\\Users\\Admin\\Documents\\Visual Studio 2010\\Projects\\ScyllaHide"

static void LogCallback(const wchar_t *msg)
{
    _putws(msg);
}

int wmain(int argc, wchar_t* argv[])
{
    DWORD targetPid = 0;
    WCHAR * dllPath = 0;

    auto wstrPath = scl::GetModuleFileNameW();
    wstrPath.resize(wstrPath.find_last_of(L'\\') + 1);

    g_ntApiCollectionIniPath = wstrPath + scl::NtApiLoader::kFileName;
    g_scyllaHideIniPath = wstrPath + scl::Settings::kFileName;

    auto log_file = wstrPath + scl::Logger::kFileName;
    g_log.SetLogFile(log_file.c_str());
    g_log.SetLogCb(scl::Logger::Info, LogCallback);
    g_log.SetLogCb(scl::Logger::Error, LogCallback);

    ReadNtApiInformation(g_ntApiCollectionIniPath.c_str(), &g_hdd);
    SetDebugPrivileges();
    //ChangeBadWindowText();
    g_settings.Load(g_scyllaHideIniPath.c_str());
    ReadSettings();

    if (argc >= 3)
    {
        targetPid = GetProcessIdByName(argv[1]);
        dllPath = argv[2];
    }
    else
    {

#ifdef _WIN64
        targetPid = GetProcessIdByName(L"scylla_x64.exe");//scylla_x64
        dllPath = PREFIX_PATH L"\\Release\\HookLibraryx64.dll";
#else
        targetPid = GetProcessIdByName(L"ThemidaTest.exe");//GetProcessIdByName(L"ThemidaTest.exe");//GetProcessIdByName(L"VMProtect.vmp.exe");//GetProcessIdByName(L"scylla_x86.exe");
        dllPath = PREFIX_PATH L"\\Release\\HookLibraryx86.dll";
#endif
    }

    if (targetPid && dllPath)
    {
        wprintf(L"\nPID\t: %d 0x%X\nDLL Path: %s\n\n", targetPid, targetPid, dllPath);
        startInjection(targetPid, dllPath);
    }
    else
    {
        wprintf(L"Usage: %s <process name> <dll path>", argv[0]);
    }

    getchar();
    return 0;
}

static bool StartHooking(HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase)
{
    g_hdd.dwProtectedProcessId = 0; //for olly plugins
    g_hdd.EnableProtectProcessId = FALSE;

    DWORD enableEverything = PEB_PATCH_BeingDebugged|PEB_PATCH_HeapFlags|PEB_PATCH_NtGlobalFlag|PEB_PATCH_ProcessParameters;
    ApplyPEBPatch(hProcess, enableEverything);

    return ApplyHook(&g_hdd, hProcess, dllMemory, imageBase);
}

void startInjectionProcess(HANDLE hProcess, BYTE * dllMemory)
{
    LPVOID remoteImageBase = MapModuleToProcess(hProcess, dllMemory);
    if (remoteImageBase)
    {
        FillHookDllData(hProcess, &g_hdd);
        //DWORD initDllFuncAddressRva = GetDllFunctionAddressRVA(dllMemory, "InitDll");
        DWORD hookDllDataAddressRva = GetDllFunctionAddressRVA(dllMemory, "HookDllData");

        StartHooking(hProcess, dllMemory, (DWORD_PTR)remoteImageBase);



        if (WriteProcessMemory(hProcess, (LPVOID)((DWORD_PTR)hookDllDataAddressRva + (DWORD_PTR)remoteImageBase), &g_hdd, sizeof(HOOK_DLL_DATA), 0))
        {
            //DWORD exitCode = StartDllInitFunction(hProcess, ((DWORD_PTR)initDllFuncAddressRva + (DWORD_PTR)remoteImageBase), remoteImageBase);


            //if (exitCode == HOOK_ERROR_SUCCESS)

            //{
            wprintf(L"Injection successful, Imagebase %p\n", remoteImageBase);
            //}
            //else
            //{
            //	wprintf(L"Injection failed, exit code %d 0x%X Imagebase %p\n", exitCode, exitCode, remoteImageBase);
            //}
        }
        else
        {
            wprintf(L"Failed to write hook dll data\n");
        }
    }
}

void startInjection(DWORD targetPid, const WCHAR * dllPath)
{
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, 0, targetPid);
    if (hProcess)
    {
        std::basic_string<BYTE> dllMemory;
        if (scl::ReadFileContents(dllPath, dllMemory))
        {
            startInjectionProcess(hProcess, &dllMemory[0]);
        }
        else
        {
            wprintf(L"Cannot read file to memory %s\n", dllPath);
        }
        CloseHandle(hProcess);
    }
    else
    {
        wprintf(L"Cannot open process handle %d\n", targetPid);
    }
}

bool SetDebugPrivileges()
{
    TOKEN_PRIVILEGES Debug_Privileges;
	bool retVal = false;

    if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &Debug_Privileges.Privileges[0].Luid))
	{
		HANDLE hToken = 0;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
		{
			Debug_Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			Debug_Privileges.PrivilegeCount = 1;

			retVal = AdjustTokenPrivileges(hToken, FALSE, &Debug_Privileges, 0, NULL, NULL) != FALSE;

			CloseHandle(hToken);
		}
	}

    return retVal;
}

DWORD GetProcessIdByName(const WCHAR * processName)
{
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hProcessSnap, &pe32))
    {
        wprintf(L"Error getting first process\n");
        CloseHandle(hProcessSnap);
        return 0;
    }

    DWORD pid = 0;

    do
    {
        if (!_wcsicmp(pe32.szExeFile, processName))
        {
            pid = pe32.th32ProcessID;
            break;
        }
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return pid;
}

void ReadSettings()
{
    g_hdd.EnableBlockInputHook = g_settings.opts().hookBlockInput;
    g_hdd.EnableGetLocalTimeHook = g_settings.opts().hookGetLocalTime;
    g_hdd.EnableGetSystemTimeHook = g_settings.opts().hookGetSystemTime;
    g_hdd.EnableGetTickCount64Hook = g_settings.opts().hookGetTickCount64;
    g_hdd.EnableGetTickCountHook = g_settings.opts().hookGetTickCount;
    g_hdd.EnableKiUserExceptionDispatcherHook = g_settings.opts().hookKiUserExceptionDispatcher;
    g_hdd.EnableNtCloseHook = g_settings.opts().hookNtClose;
    g_hdd.EnableNtContinueHook = g_settings.opts().hookNtContinue;
    g_hdd.EnableNtCreateThreadExHook = g_settings.opts().hookNtCreateThreadEx;
    g_hdd.EnableNtGetContextThreadHook = g_settings.opts().hookNtGetContextThread;
    g_hdd.EnableNtQueryInformationProcessHook = g_settings.opts().hookNtQueryInformationProcess;
    g_hdd.EnableNtQueryObjectHook = g_settings.opts().hookNtQueryObject;
    g_hdd.EnableNtQueryPerformanceCounterHook = g_settings.opts().hookNtQueryPerformanceCounter;
    g_hdd.EnableNtQuerySystemInformationHook = g_settings.opts().hookNtQuerySystemInformation;
    g_hdd.EnableNtQuerySystemTimeHook = g_settings.opts().hookNtQuerySystemTime;
    g_hdd.EnableNtSetContextThreadHook = g_settings.opts().hookNtSetContextThread;
    g_hdd.EnableNtSetDebugFilterStateHook = g_settings.opts().hookNtSetDebugFilterState;
    g_hdd.EnableNtSetInformationThreadHook = g_settings.opts().hookNtSetInformationThread;
    g_hdd.EnableNtUserBuildHwndListHook = g_settings.opts().hookNtUserBuildHwndList;
    g_hdd.EnableNtUserFindWindowExHook = g_settings.opts().hookNtUserFindWindowEx;
    g_hdd.EnableNtUserQueryWindowHook = g_settings.opts().hookNtUserQueryWindow;
    g_hdd.EnableNtYieldExecutionHook = g_settings.opts().hookNtYieldExecution;
    g_hdd.EnableOutputDebugStringHook = g_settings.opts().hookOutputDebugStringA;
    g_hdd.EnablePebBeingDebugged = g_settings.opts().fixPebBeingDebugged;
    g_hdd.EnablePebHeapFlags = g_settings.opts().fixPebHeapFlags;
    g_hdd.EnablePebNtGlobalFlag = g_settings.opts().fixPebNtGlobalFlag;
    g_hdd.EnablePebStartupInfo = g_settings.opts().fixPebStartupInfo;
    g_hdd.EnablePreventThreadCreation = g_settings.opts().preventThreadCreation;
    g_hdd.EnableProtectProcessId = g_settings.opts().protectProcessId;
}

//BOOL CALLBACK MyEnumChildProc(
//	_In_  HWND hwnd,
//	_In_  LPARAM lParam
//	)
//{
//	WCHAR windowText[1000] = { 0 };
//	WCHAR classText[1000] = { 0 };
//	if (GetWindowTextW(hwnd, windowText, _countof(windowText)) > 1)
//	{
//		GetClassName(hwnd, classText, _countof(classText));
//
//		wprintf(L"\t%s\n\t%s\n", windowText, classText);
//	}
//
//	return TRUE;
//}
//
//BOOL CALLBACK MyEnumWindowsProc(HWND hwnd,LPARAM lParam)
//{
//	WCHAR windowText[1000] = { 0 };
//	WCHAR classText[1000] = { 0 };
//	if (GetWindowTextW(hwnd, windowText, _countof(windowText)) > 1)
//	{
//		GetClassName(hwnd, classText, _countof(classText));
//
//		wprintf(L"------------------\n%s\n%s\n", windowText, classText);
//
//		if (wcsistr(windowText, L"x32_dbg"))
//		{
//
//			EnumChildWindows(hwnd, MyEnumChildProc, 0);
//
//			DWORD_PTR result;
//			SendMessageTimeoutW(hwnd, WM_SETTEXT, 0, (LPARAM)title, SMTO_ABORTIFHUNG, 1000, &result);
//
//			//LPVOID stringW = WriteStringInProcessW(hwnd, L"ficken");
//			//LPVOID stringA = WriteStringInProcessA(hwnd, "ficken");
//			//if (!SetClassLongPtrW(hwnd, (int)&((WNDCLASSEXW*)0)->lpszClassName, (LONG_PTR)stringW))
//			//{
//			//	printf("%d %d\n", (int)&((WNDCLASSEXW*)0)->lpszClassName,  GetLastError());
//			//}
//			//if (!SetClassLongPtrA(hwnd, (int)&((WNDCLASSEXA*)0)->lpszClassName, (LONG_PTR)stringA))
//			//{
//			//	printf("%d %d\n", (int)&((WNDCLASSEXA*)0)->lpszClassName, GetLastError());
//			//}
//		}
//	}
//
//	return TRUE;
//}
//void ChangeBadWindowText()
//{
//	EnumWindows(MyEnumWindowsProc, 0);
//}
