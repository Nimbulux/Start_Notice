#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

// 自身单实例互斥体名称
#define APP_MUTEX_NAME L"Global\\Start_Notice_SingleInstance"

// 配置文件名称（与启动器同目录）
#define CONFIG_FILE L"config.txt"

// 检查指定进程名是否已在运行（通过进程名精确匹配）
static BOOL IsProcessRunning(const wchar_t* exeName)
{
    BOOL running = FALSE;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return FALSE;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);
    if (Process32FirstW(hSnap, &pe32))
    {
        do
        {
            if (_wcsicmp(pe32.szExeFile, exeName) == 0)
            {
                running = TRUE;
                break;
            }
        } while (Process32NextW(hSnap, &pe32));
    }
    CloseHandle(hSnap);
    return running;
}

// 启动目标程序（传入可写的命令行缓冲区，避免访问冲突）
static BOOL LaunchTarget(const wchar_t* exePath)
{
    wchar_t cmdLine[512];
    wcscpy_s(cmdLine, _countof(cmdLine), exePath);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    BOOL ret = CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE,
        0, NULL, NULL, &si, &pi);
    if (ret)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ret;
}

// 从配置文件读取目标程序路径（UTF‑8 编码的 config.txt）
static BOOL ReadConfigFile(wchar_t* targetPath, size_t pathSize)
{
    FILE* fp = NULL;
    if (_wfopen_s(&fp, CONFIG_FILE, L"rt, ccs=UTF-8") != 0 || fp == NULL)
        return FALSE;

    // 用 fgetws 读取一行宽字符串（会去除行尾换行符）
    if (fgetws(targetPath, (int)pathSize, fp) == NULL)
    {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    // 去除末尾换行符
    size_t len = wcslen(targetPath);
    while (len > 0 && (targetPath[len - 1] == L'\n' || targetPath[len - 1] == L'\r'))
        targetPath[--len] = L'\0';

    // 跳过前导空白
    wchar_t* trimmed = targetPath;
    while (*trimmed == L' ' || *trimmed == L'\t')
        trimmed++;
    if (*trimmed == L'\0')
        return FALSE;

    // 如果 trimmed 不是原始开头，则移动内容
    if (trimmed != targetPath)
        wmemmove(targetPath, trimmed, wcslen(trimmed) + 1);

    return TRUE;
}

int WinMain(void)
{
    // 1. 自身单实例检查
    HANDLE hMutex = CreateMutexW(NULL, FALSE, APP_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutex) CloseHandle(hMutex);
        return 0;   // 已有实例，静默退出
    }

    // 2. 从配置文件读取目标程序名，失败则提示
    wchar_t targetPath[512] = { 0 };
    if (!ReadConfigFile(targetPath, _countof(targetPath)))
    {
        MessageBoxW(NULL, L"配置文件读取失败，请检查是否有'config.txt'", L"提示", MB_OK);
        CloseHandle(hMutex);
        return 1;
    }

    // 3. 提取纯文件名（用于进程名检查）
    wchar_t* exeName = wcsrchr(targetPath, L'\\');
    if (exeName != NULL)
        exeName++;      // 跳过反斜杠
    else
        exeName = targetPath;   // 没有路径，直接就是文件名

    // 4. 检查目标程序是否已在运行
    if (IsProcessRunning(exeName))
    {
        // 已在运行，不再启动
        CloseHandle(hMutex);
        return 0;
    }

    // 5. 启动目标程序
    if (!LaunchTarget(targetPath))
    {
        MessageBoxW(NULL, L"启动目标程序失败！", L"错误", MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 6. 启动成功，退出启动器
    CloseHandle(hMutex);
    return 0;
}