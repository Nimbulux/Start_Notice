#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#define CONFIG_FILE     "config.txt"
#define MUTEX_NAME      "Global\\Launcher_SingleInstance_Mutex"

 /* 获取当前 .exe 所在目录 (绝对路径) */
static void get_exe_dir(char* dir, size_t size) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    PathRemoveFileSpecA(exe_path);
    strcpy_s(dir, size, exe_path);
}

/* 构建 config.txt 的完整路径 */
static void get_config_path(char* out, size_t size, const char* exe_dir) {
    PathCombineA(out, exe_dir, CONFIG_FILE);
}

/* 将相对路径转换为绝对路径 */
static void make_absolute(char* abs_path, size_t size,
    const char* base_dir, const char* rel_path) {
    if (strlen(rel_path) >= 2 && rel_path[1] == ':') {
        strncpy_s(abs_path, size, rel_path, _TRUNCATE);
    }
    else if (rel_path[0] == '\\' && rel_path[1] == '\\') {
        strncpy_s(abs_path, size, rel_path, _TRUNCATE);
    }
    else {
        PathCombineA(abs_path, base_dir, rel_path);
    }
}

/* 将错误码转换为可读字符串，返回需 LocalFree 释放 */
static LPSTR format_error_message(DWORD err) {
    LPSTR msgBuf = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuf, 0, NULL);
    return msgBuf;
}

/* 弹出错误消息框并退出 */
static void fatal_error(const char* title, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, format, args);
    va_end(args);
    MessageBoxA(NULL, buf, title, MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    /* 1. 获取自身所在目录 */
    char exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, sizeof(exe_dir));

    /* 2. 读取 config.txt */
    char config_path[MAX_PATH];
    get_config_path(config_path, sizeof(config_path), exe_dir);

    FILE* fp = fopen(config_path, "r");
    if (!fp) {
        fatal_error("启动失败", "未找到 %s，请在同目录下创建该文件并写入要启动的程序名称。", CONFIG_FILE);
    }

    char target_line[MAX_PATH];
    if (!fgets(target_line, sizeof(target_line), fp)) {
        fclose(fp);
        fatal_error("启动失败", "%s 内容为空。", CONFIG_FILE);
    }
    fclose(fp);

    // 去除换行符
    target_line[strcspn(target_line, "\r\n")] = '\0';
    if (strlen(target_line) == 0) {
        fatal_error("启动失败", "%s 中未指定有效的程序名称。", CONFIG_FILE);
    }

    char target_path[MAX_PATH];
    make_absolute(target_path, sizeof(target_path), exe_dir, target_line);

    /* 3. 互斥体防止重复运行 */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
    if (hMutex == NULL) {
        DWORD err = GetLastError();
        LPSTR msg = format_error_message(err);
        fatal_error("互斥体错误", "创建互斥体失败: %s", msg ? msg : "未知错误");
        if (msg) LocalFree(msg);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fatal_error("程序已在运行", "启动器已在运行中，不允许重复启动。");
    }
    // 持有互斥体直到进程退出

    /* 4. 启动目标程序 */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd_line[MAX_PATH];
    strcpy_s(cmd_line, sizeof(cmd_line), target_path);

    if (!CreateProcessA(
        NULL,
        cmd_line,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)) {
        DWORD err = GetLastError();
        LPSTR msg = format_error_message(err);
        fatal_error("启动失败", "无法启动目标程序:\n%s\n\n%s", target_path, msg ? msg : "未知错误");
        if (msg) LocalFree(msg);
    }

    CloseHandle(pi.hThread);

    /* 5. 等待目标程序结束（期间不显示任何窗口） */
    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(hMutex);

    return 0;
}