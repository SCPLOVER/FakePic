#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <chrono>

#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

constexpr std::size_t BUFFER_SIZE = 32ull * 1024 * 1024;

struct ProgressState {
    double target = 0;
    double displayed = 0;
    HWND hProgress = nullptr;
    HWND hWnd = nullptr;
    std::wstring currentFile;
    long long processed = 0;
    long long totalSize = 0;
    std::chrono::steady_clock::time_point startTime;
} g_progressState;

std::wstring utf8_to_wstr(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), size_needed);
    return wstr;
}

LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH bgBrush = CreateSolidBrush(RGB(230, 230, 230));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            double delta = g_progressState.target - g_progressState.displayed;
            g_progressState.displayed += delta * 0.1;

            int fillWidth = (int)((rc.right - rc.left) * (g_progressState.displayed / 100.0));
            if (fillWidth > 0) {
                TRIVERTEX vert[2]{};
                vert[0] = { rc.left, rc.top, 0x00 * 256, 0x7A * 256, 0xCC * 256, 0 };
                vert[1] = { rc.left + fillWidth, rc.bottom, 0x00 * 256, 0xCC * 256, 0xFF * 256, 0 };
                GRADIENT_RECT gRect{ 0, 1 };
                GradientFill(hdc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_H);
            }

            std::wstring info;
            double percent = g_progressState.displayed;
            long long processedMB = g_progressState.processed / (1024*1024);
            long long totalMB = g_progressState.totalSize / (1024*1024);
            auto now = std::chrono::steady_clock::now();
            double elapsedSec = std::chrono::duration<double>(now - g_progressState.startTime).count();
            double remainingSec = percent > 0 ? elapsedSec * (100 - percent) / percent : 0;
            int minRem = (int)(remainingSec / 60);
            int secRem = (int)(remainingSec) % 60;

            info = g_progressState.currentFile + L" "
                   + std::to_wstring((int)percent) + L"% "
                   + L"(" + std::to_wstring(processedMB) + L"/" + std::to_wstring(totalMB) + L" MB) "
                   + L"剩余: " + std::to_wstring(minRem) + L"分" + std::to_wstring(secRem) + L"秒";

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextW(hdc, info.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hwnd, &ps);

            if (g_progressState.displayed < 99.99)
                SetTimer(hwnd, 1, 10, nullptr);
            else
                KillTimer(hwnd, 1);

            return 0;
        }
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void UpdateProgress(const std::wstring& fileName, long long processed, long long totalSize, int percent) {
    g_progressState.currentFile = fileName;
    g_progressState.processed = processed;
    g_progressState.totalSize = totalSize;
    g_progressState.target = percent;
    InvalidateRect(g_progressState.hProgress, nullptr, TRUE);
}

HWND CreateProgressBar(HINSTANCE hInstance) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t* progressClass = L"ModernProgressBar";

    WNDCLASSW wc{};
    wc.lpfnWndProc = ProgressWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = progressClass;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwndParent = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 100,
        nullptr, nullptr, hInstance, nullptr);

    g_progressState.hProgress = CreateWindowExW(
        0, progressClass, nullptr,
        WS_CHILD | WS_VISIBLE,
        10, 40, 580, 40,
        hwndParent, nullptr, hInstance, nullptr);

    ShowWindow(hwndParent, SW_SHOW);
    UpdateWindow(hwndParent);

    g_progressState.hWnd = hwndParent;
    g_progressState.startTime = std::chrono::steady_clock::now();
    return hwndParent;
}

void appendFile(FILE* out, const std::wstring& inPath, int startPercent, int endPercent) {
    FILE* in = _wfopen(inPath.c_str(), L"rb");
    if (!in) throw std::runtime_error("无法打开文件");

    fseek(in, 0, SEEK_END);
    long long totalSize = ftell(in);
    fseek(in, 0, SEEK_SET);

    std::vector<char> buffer(BUFFER_SIZE);
    long long processed = 0;
    std::wstring fileName = fs::path(inPath).filename().wstring();

    while (!feof(in)) {
        size_t bytesRead = fread(buffer.data(), 1, BUFFER_SIZE, in);
        if (bytesRead > 0) {
            fwrite(buffer.data(), 1, bytesRead, out);
            processed += bytesRead;
            int percent = startPercent + static_cast<int>((processed * (endPercent - startPercent)) / totalSize);
            UpdateProgress(fileName, processed, totalSize, percent);

            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    fclose(in);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    try {
        int argc;
        LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argc != 3) {
            MessageBoxW(nullptr,
                        L"使用方法: 将 图片文件 和 压缩文件 一起拖到本程序图标上。\n示例: fake.exe test.jpg test.zip",
                        L"提示", MB_OK);
            return 0;
        }

        std::wstring firstPath = argvW[1];
        std::wstring secondPath = argvW[2];

        fs::path firstP(firstPath);
        fs::path secondP(secondPath);

        fs::path imgP, zipP;
        std::wstring ext1 = firstP.extension().wstring();
        std::wstring ext2 = secondP.extension().wstring();

        if (ext1 == L".jpg" || ext1 == L".png" || ext1 == L".jpeg") {
            imgP = firstP;
            zipP = secondP;
        } else {
            imgP = secondP;
            zipP = firstP;
        }

        std::wstring outName = imgP.stem().wstring() + L"_fake" + imgP.extension().wstring();
        std::wstring outPath = (fs::current_path() / outName).wstring();

        CreateProgressBar(hInstance);

        FILE* out = _wfopen(outPath.c_str(), L"wb");
        if (!out) throw std::runtime_error("无法创建输出文件");

        appendFile(out, imgP.wstring(), 0, 50);
        appendFile(out, zipP.wstring(), 50, 100);

        fclose(out);

        MessageBoxW(nullptr, L"✅ 成功生成伪造图种！", L"完成", MB_OK);
    } catch (const std::exception& e) {
        std::wstring msg = L"错误: " + utf8_to_wstr(e.what());
        MessageBoxW(nullptr, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    return 0;
}
