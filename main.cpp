#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FILE             "config.txt"
#define SPLASH_IMAGE_FILE       "splash.png"
#define MUTEX_NAME              "Global\\Start_Notice_SingleInstance_Mutex"
#define SPLASH_WINDOW_TITLE     "Start_Notice_SplashWindow_Instance"
#define WM_SPLASH_ACTIVATE      (WM_USER + 100)
#define FLASH_COUNT             5
#define FLASH_RATE              100

// 前向声明
static void get_exe_dir(char* dir, size_t size);
static void get_config_path(char* out, size_t size, const char* exe_dir);
static void make_absolute(char* abs_path, size_t size, const char* base_dir, const char* rel_path);
static LPSTR format_error_message(DWORD err);
static void fatal_error(const char* title, const char* format, ...);
static HWND create_splash_window(HINSTANCE hInstance, const char* exe_dir);
static void activate_splash_window(HWND hwnd);
LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 枚举窗口用参数
struct FindWindowParam {
    DWORD pid;
    HWND hwndFound;
};

static BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam);

// GDI+ 全局变量
static ULONG_PTR gdiplusToken = 0;
static bool gdiplusInitialized = false;

// ---------- 辅助函数 ----------
static void get_exe_dir(char* dir, size_t size) {
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    PathRemoveFileSpecA(exe_path);
    strcpy_s(dir, size, exe_path);
}

static void get_config_path(char* out, size_t size, const char* exe_dir) {
    PathCombineA(out, exe_dir, CONFIG_FILE);
}

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

static LPSTR format_error_message(DWORD err) {
    LPSTR msgBuf = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuf, 0, NULL);
    return msgBuf;
}

static void fatal_error(const char* title, const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, format, args);
    va_end(args);
    MessageBoxA(NULL, buf, title, MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static void activate_splash_window(HWND hwnd) {
    DWORD targetThreadId = GetWindowThreadProcessId(hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();
    AttachThreadInput(currentThreadId, targetThreadId, TRUE);
    SetForegroundWindow(hwnd);
    AttachThreadInput(currentThreadId, targetThreadId, FALSE);

    FLASHWINFO fwi;
    fwi.cbSize = sizeof(fwi);
    fwi.hwnd = hwnd;
    fwi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fwi.uCount = FLASH_COUNT;
    fwi.dwTimeout = FLASH_RATE;
    FlashWindowEx(&fwi);
}

// 使用 GDI+ 加载透明 PNG 并显示到分层窗口
static bool show_png_splash(HWND hwnd, const char* imagePath) {
    if (!gdiplusInitialized) return false;

    // 转换为宽字符路径
    int wlen = MultiByteToWideChar(CP_ACP, 0, imagePath, -1, NULL, 0);
    wchar_t* wpath = new wchar_t[wlen];
    MultiByteToWideChar(CP_ACP, 0, imagePath, -1, wpath, wlen);

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(wpath, FALSE);
    delete[] wpath;

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        delete bitmap;
        return false;
    }

    UINT width = bitmap->GetWidth();
    UINT height = bitmap->GetHeight();
    if (width == 0 || height == 0) {
        delete bitmap;
        return false;
    }

    // 设置窗口位置和大小
    int x = (GetSystemMetrics(SM_CXSCREEN) - (int)width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (int)height) / 2;
    SetWindowPos(hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);

    // 创建 32 位 DIB 并绘制 PNG
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), (LONG)width, -(LONG)height, 1, 32, BI_RGB } };
    void* pvBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hBitmap) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        delete bitmap;
        return false;
    }
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Gdiplus::Graphics graphics(hdcMem);
    graphics.DrawImage(bitmap, 0, 0, width, height);

    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { (LONG)width, (LONG)height };
    POINT ptDst = { x, y };

    BOOL ret = UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd,
        hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    delete bitmap;

    return ret != FALSE;
}

// 创建启动窗口（图片优先，回退文字）
static HWND create_splash_window(HINSTANCE hInstance, const char* exe_dir) {
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = SplashWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "SplashWindowClass";
    RegisterClassA(&wc);

    char imagePath[MAX_PATH];
    PathCombineA(imagePath, exe_dir, SPLASH_IMAGE_FILE);
    DWORD fileAttr = GetFileAttributesA(imagePath);
    bool useImage = (fileAttr != INVALID_FILE_ATTRIBUTES && !(fileAttr & FILE_ATTRIBUTE_DIRECTORY));

    HWND hwnd = NULL;
    if (useImage && gdiplusInitialized) {
        hwnd = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            "SplashWindowClass",
            SPLASH_WINDOW_TITLE,
            WS_POPUP,
            0, 0, 1, 1,
            NULL, NULL, hInstance, NULL);
        if (hwnd && !show_png_splash(hwnd, imagePath)) {
            DestroyWindow(hwnd);
            hwnd = NULL;
        }
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
    }

    if (!hwnd) {
        // 回退：400x400 文字窗口
        int x = (GetSystemMetrics(SM_CXSCREEN) - 400) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - 400) / 2;
        hwnd = CreateWindowExA(
            0,
            "SplashWindowClass",
            SPLASH_WINDOW_TITLE,
            WS_POPUP | WS_VISIBLE,
            x, y, 400, 400,
            NULL, NULL, hInstance, NULL);
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
    }
    return hwnd;
}

// 窗口过程
LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hStatic = NULL;
    switch (msg) {
    case WM_CREATE: {
        LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED)) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            hStatic = CreateWindowExA(
                0, "STATIC", "正在启动\n请等待",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, rc.right, rc.bottom,
                hwnd, NULL, ((LPCREATESTRUCTA)lParam)->hInstance, NULL);
            if (hStatic) {
                int fontHeight = -MulDiv(1, rc.bottom, 4);
                HFONT hFont = CreateFontA(
                    fontHeight, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                    "Microsoft YaHei");
                if (hFont)
                    SendMessageA(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
            }
        }
        return 0;
    }
    case WM_SPLASH_ACTIVATE:
        activate_splash_window(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// 枚举回调：查找目标进程的可见主窗口
static BOOL CALLBACK FindMainWindowProc(HWND hwnd, LPARAM lParam) {
    FindWindowParam* param = (FindWindowParam*)lParam;
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == param->pid && IsWindowVisible(hwnd) && IsWindowEnabled(hwnd)) {
        char cls[256];
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (strcmp(cls, "Shell_TrayWnd") != 0 && strcmp(cls, "Button") != 0) {
            param->hwndFound = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

// ---------- WinMain ----------
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {

    // 初始化 GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
        gdiplusInitialized = true;
    }

    // 互斥体
    HANDLE hMutex = CreateMutexA(NULL, TRUE, MUTEX_NAME);
    if (!hMutex) {
        DWORD err = GetLastError();
        LPSTR msg = format_error_message(err);
        fatal_error("互斥体错误", "创建互斥体失败: %s", msg ? msg : "未知错误");
        if (msg) LocalFree(msg);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hwndExisting = FindWindowA(NULL, SPLASH_WINDOW_TITLE);
        if (hwndExisting)
            SendMessageA(hwndExisting, WM_SPLASH_ACTIVATE, 0, 0);
        CloseHandle(hMutex);
        if (gdiplusInitialized) Gdiplus::GdiplusShutdown(gdiplusToken);
        return 0;
    }

    char exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, sizeof(exe_dir));

    HWND hwndSplash = create_splash_window(hInstance, exe_dir);
    if (!hwndSplash) {
        fatal_error("内部错误", "无法创建启动窗口。");
    }

    // 读取 config.txt
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
    target_line[strcspn(target_line, "\r\n")] = '\0';
    if (strlen(target_line) == 0) {
        fatal_error("启动失败", "%s 中未指定有效的程序名称。", CONFIG_FILE);
    }

    char target_path[MAX_PATH];
    make_absolute(target_path, sizeof(target_path), exe_dir, target_line);

    // 启动目标程序
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd_line[MAX_PATH];
    strcpy_s(cmd_line, sizeof(cmd_line), target_path);

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        LPSTR msg = format_error_message(err);
        fatal_error("启动失败", "无法启动目标程序:\n%s\n\n%s", target_path, msg ? msg : "未知错误");
        if (msg) LocalFree(msg);
    }
    CloseHandle(pi.hThread);

    // 等待目标窗口出现或进程退出
    HANDLE hProcess = pi.hProcess;
    bool splashClosed = false;
    while (true) {
        DWORD result = MsgWaitForMultipleObjects(1, &hProcess, FALSE,
            splashClosed ? INFINITE : 200, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) break;
        else if (result == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
        else if (result == WAIT_TIMEOUT && !splashClosed) {
            FindWindowParam param = { pi.dwProcessId, NULL };
            EnumWindows(FindMainWindowProc, (LPARAM)&param);
            if (param.hwndFound) {
                DestroyWindow(hwndSplash);
                splashClosed = true;
            }
        }
    }

    CloseHandle(hProcess);
    CloseHandle(hMutex);

    if (gdiplusInitialized) Gdiplus::GdiplusShutdown(gdiplusToken);
    return 0;
}