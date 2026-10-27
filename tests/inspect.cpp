// SPDX-License-Identifier: GPL-3.0-or-later
// Inspección de integración: captura los píxeles reales de las barras, sin entradas de usuario.
#include "../src/protocol.h"
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace tb;
constexpr wchar_t TestClass[]=L"TranslucentBar.PruebaVisual";
LRESULT CALLBACK TestProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT rect;GetClientRect(window,&rect);
        const COLORREF colors[]={RGB(238,56,76),RGB(32,210,176),RGB(40,115,244),RGB(245,202,55)};
        for(int x=0;x<rect.right;x+=32) {HBRUSH brush=CreateSolidBrush(colors[(x/32)%4]);RECT bar{x,0,x+32,rect.bottom};FillRect(dc,&bar,brush);DeleteObject(brush);}
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_CLOSE){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc!=2) return 2;
    std::wstring prefix=argv[1];LocalFree(argv);
    if(prefix==L"--avisar-tema") {
        DWORD_PTR result;SendMessageTimeoutW(HWND_BROADCAST,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),SMTO_ABORTIFHUNG,1000,&result);return 0;
    }
    if(prefix==L"--ordenar") {
        EnumWindows([](HWND taskbar,LPARAM)->BOOL {
            wchar_t name[80];GetClassNameW(taskbar,name,80);if(wcscmp(name,L"Shell_TrayWnd")&&wcscmp(name,L"Shell_SecondaryTrayWnd"))return TRUE;
            EnumWindows([](HWND test,LPARAM context)->BOOL {
                HWND bar=reinterpret_cast<HWND>(context);wchar_t cls[80];GetClassNameW(test,cls,80);
                if(wcscmp(cls,TestClass)==0&&MonitorFromWindow(test,MONITOR_DEFAULTTONEAREST)==MonitorFromWindow(bar,MONITOR_DEFAULTTONEAREST))
                    SetWindowPos(test,bar,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
                return TRUE;
            },reinterpret_cast<LPARAM>(taskbar));return TRUE;
        },0);return 0;
    }
    if(prefix==L"--cerrar") {while(HWND window=FindWindowW(TestClass,nullptr)) SendMessageW(window,WM_CLOSE,0,0);return 0;}
    if(prefix==L"--fondo") {
        WNDCLASSW wc{};wc.lpfnWndProc=TestProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=TestClass;RegisterClassW(&wc);
        EnumWindows([](HWND window,LPARAM)->BOOL {
            wchar_t name[80];GetClassNameW(window,name,80);if(wcscmp(name,L"Shell_TrayWnd")&&wcscmp(name,L"Shell_SecondaryTrayWnd"))return TRUE;
            RECT rect;GetWindowRect(window,&rect);
            HWND test=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,TestClass,L"Prueba visual de TranslucentBar",WS_POPUP,rect.left,rect.top-120,rect.right-rect.left,rect.bottom-rect.top+120,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            SetWindowPos(test,window,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);return TRUE;
        },0);
        MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
    }
    struct Context {std::wstring prefix;int count=0;bool ok=true;} context{prefix};
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        auto& c=*reinterpret_cast<Context*>(data);wchar_t name[80];GetClassNameW(window,name,80);
        if(wcscmp(name,L"Shell_TrayWnd")&&wcscmp(name,L"Shell_SecondaryTrayWnd")) return TRUE;
        RECT rect;GetWindowRect(window,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
        if(width<=0||height<=0) {c.ok=false;return TRUE;}
        HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(mem,bitmap);
        BitBlt(mem,0,0,width,height,screen,rect.left,rect.top,SRCCOPY|CAPTUREBLT);GdiFlush();
        ++c.count;
        std::wstring path=c.prefix+(wcscmp(name,L"Shell_TrayWnd")==0?L"-principal":L"-secundaria")+L".bmp";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
        DWORD written;
        if(file==INVALID_HANDLE_VALUE) c.ok=false;
        else {WriteFile(file,&header,sizeof(header),&written,nullptr);WriteFile(file,&info.bmiHeader,sizeof(BITMAPINFOHEADER),&written,nullptr);WriteFile(file,pixels,width*height*4,&written,nullptr);CloseHandle(file);}
        SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(nullptr,screen);return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    return context.ok&&context.count?0:1;
}
