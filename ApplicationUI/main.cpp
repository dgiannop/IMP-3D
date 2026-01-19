#ifdef _WIN32
extern "C" {
// For NVIDIA Optimus
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;

// For AMD Switchable Graphics
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include <QApplication>
#include <QFile>

#include "MainWindow.hpp"

#ifdef _WIN32

#include <dwmapi.h>
#include <windows.h>
#pragma comment(lib, "dwmapi.lib")

// -----------------------------------------------------------------------------
// Helper: force dark title bar (works on Win10 1809+ / Win11)
// -----------------------------------------------------------------------------
static void setDarkTitleBar(QWidget* w, bool enabled = true)
{
    if (!w)
        return;

    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd)
        return;

    BOOL            useDark                           = enabled ? TRUE : FALSE;
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE     = 20;
    constexpr DWORD DWMWA_CAPTION_COLOR               = 35;
    constexpr DWORD DWMWA_BORDER_COLOR                = 34;
    constexpr DWORD DWMWA_TEXT_COLOR                  = 36;

    HRESULT hr = DwmSetWindowAttribute(hwnd,
                                       DWMWA_USE_IMMERSIVE_DARK_MODE,
                                       &useDark,
                                       sizeof(useDark));
    if (FAILED(hr))
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));

    if (enabled)
    {
        COLORREF caption = RGB(37, 37, 41);
        COLORREF border  = RGB(37, 37, 41);
        COLORREF text    = RGB(230, 230, 235);

        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    }
}
#endif // _WIN32

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFile styleFile(":/styles/main.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text))
        app.setStyleSheet(QLatin1String(styleFile.readAll()));
    else
        qWarning() << "Failed to open stylesheet file!";

    MainWindow win;

#ifdef _WIN32
    // Force dark title bar even if OS theme is Light
    setDarkTitleBar(&win, true);
#endif

    win.show();
    return app.exec();
}
