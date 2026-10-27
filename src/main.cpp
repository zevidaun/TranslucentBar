// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.h"
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

using namespace tb;
namespace {
HWND control;
HINSTANCE instance;
HANDLE mapping;
Shared* shared;
Config config;
UINT taskbarCreated;
HWINEVENTHOOK createHook, locationHook;
bool pending = false, exiting = false;
std::wstring directory, bridgePath;
std::vector<DWORD> attached;
constexpr UINT Refresh = WM_APP + 1, Tray = WM_APP + 2;
constexpr UINT Exit = 100, Color = 110, Startup = 111, Status = 112;
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring ExePath() {
    wchar_t path[32768]; DWORD n = GetModuleFileNameW(nullptr,path,32768); return {path,n};
}
void Error(const wchar_t* text) { MessageBoxW(control,text,L"TranslucentBar",MB_OK|MB_ICONWARNING); }
std::wstring DataDirectory() {
    wchar_t path[32768]; DWORD count=GetEnvironmentVariableW(L"APPDATA",path,32768);
    if(!count||count>=32768) return {};
    std::wstring result(path); result += L"\\TranslucentBar";
    if (!CreateDirectoryW(result.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return {};
    return result;
}
bool SaveConfig() {
    wchar_t path[32768]; swprintf_s(path,L"%s\\config.json.tmp",directory.c_str());
    HANDLE file = CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char json[180]; const char* mode = config.mode == Mode::Normal ? "normal" : config.mode == Mode::Glass ? "glass" : "translucido";
    int size = snprintf(json,sizeof(json),"{\n  \"modo\": \"%s\",\n  \"color\": \"#%06X\",\n  \"opacidad\": %u\n}\n",mode,config.rgb,config.opacity);
    DWORD written = 0; bool ok = WriteFile(file,json,size,&written,nullptr) && written == static_cast<DWORD>(size);
    FlushFileBuffers(file); CloseHandle(file);
    return ok && MoveFileExW(path,(directory+L"\\config.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
// Parser acotado al esquema plano propio. Rechaza documentos parciales o ambiguos.
bool LoadConfig(Config& out) {
    HANDLE file = CreateFileW((directory+L"\\config.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    char buffer[1025]{}; DWORD read = 0;
    bool ok = GetFileSize(file,nullptr) <= 1024 && ReadFile(file,buffer,1024,&read,nullptr); CloseHandle(file);
    if (!ok || memchr(buffer,0,read)) return false;
    const char* p = buffer;
    if(read>=3&&static_cast<unsigned char>(p[0])==0xef&&static_cast<unsigned char>(p[1])==0xbb&&static_cast<unsigned char>(p[2])==0xbf) p+=3;
    auto whitespace = [&]{ while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p; };
    auto character = [&](char c){whitespace(); if (*p!=c) return false; ++p; return true;};
    auto string = [&](std::string& value) {
        if (!character('"')) return false;
        value.clear(); while (*p && *p!='"') { if (*p=='\\'||static_cast<unsigned char>(*p)<32) return false; value+=*p++; }
        if (*p!='"') return false; ++p; return true;
    };
    Config candidate; unsigned seen=0;
    if (!character('{')) return false;
    for (int i=0;i<3;++i) {
        std::string key,value; if (!string(key)||!character(':')) return false;
        if (key=="modo") {
            if ((seen&1)||!string(value)) return false; seen|=1;
            if (value=="normal") candidate.mode=Mode::Normal;
            else if(value=="glass") candidate.mode=Mode::Glass;
            else if(value=="translucido") candidate.mode=Mode::Translucent; else return false;
        } else if(key=="color") {
            if ((seen&2)||!string(value)||value.size()!=7||value[0]!='#') return false; seen|=2;
            uint32_t rgb=0;
            for(size_t j=1;j<7;++j) {
                char c=value[j]; unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;
                if(digit>15) return false; rgb=(rgb<<4)|digit;
            }
            candidate.rgb=rgb;
        } else if(key=="opacidad") {
            if(seen&4) return false; seen|=4; whitespace();
            if(*p<'0'||*p>'9') return false;
            unsigned number=0; do { number=number*10+static_cast<unsigned>(*p++-'0'); if(number>100) return false; }while(*p>='0'&&*p<='9');
            candidate.opacity=number;
        } else return false;
        if (i<2&&!character(',')) return false;
    }
    if(!character('}')) return false; whitespace(); if(*p||seen!=7) return false;
    out=candidate; return true;
}
bool IsTaskbar(HWND window) {
    wchar_t name[80]{}; GetClassNameW(window,name,80);
    return wcscmp(name,L"Shell_TrayWnd")==0 || wcscmp(name,L"Shell_SecondaryTrayWnd")==0;
}
bool IsBridge(HWND window) {
    wchar_t name[80]{}; GetClassNameW(window,name,80); return wcscmp(name,BridgeClass)==0;
}
void Notify(bool stop=false) {
    if (!stop) shared->config=config;
    EnumWindows([](HWND window,LPARAM value)->BOOL {
        if (IsBridge(window)) {
            DWORD_PTR result;
            SendMessageTimeoutW(window,UpdateMsg(),value,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1500,&result);
        }
        return TRUE;
    },stop?0:GetCurrentProcessId());
}
bool ExtractBridge() {
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_RCDATA);
    if(!resource) return false;
    DWORD size=SizeofResource(instance,resource);
    const void* data=LockResource(LoadResource(instance,resource));
    // Nombre por contenido: una actualización no sobrescribe una DLL cargada por Explorer.
    uint64_t hash=14695981039346656037ull;
    for(DWORD i=0;i<size;++i) { hash^=static_cast<const unsigned char*>(data)[i]; hash*=1099511628211ull; }
    wchar_t name[64]; swprintf_s(name,L"\\puente-%016llx.dll",static_cast<unsigned long long>(hash)); bridgePath=directory+name;
    HANDLE existing=CreateFileW(bridgePath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(existing!=INVALID_HANDLE_VALUE) {
        std::vector<unsigned char> bytes(size); DWORD read=0;
        bool same=GetFileSize(existing,nullptr)==size && ReadFile(existing,bytes.data(),size,&read,nullptr) && read==size && memcmp(bytes.data(),data,size)==0;
        CloseHandle(existing); if(same) return true;
        // No cargar una DLL distinta de la incrustada.
        return false;
    }
    HANDLE file=CreateFileW(bridgePath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0; bool ok=WriteFile(file,data,size,&written,nullptr)&&written==size; CloseHandle(file); return ok;
}
void Attach(DWORD pid) {
    std::pair<DWORD,bool> match{pid,false};
    EnumWindows([](HWND window,LPARAM context)->BOOL {
        auto& m=*reinterpret_cast<std::pair<DWORD,bool>*>(context); DWORD owner=0; GetWindowThreadProcessId(window,&owner);
        if(owner==m.first&&IsBridge(window)) {m.second=true;return FALSE;} return TRUE;
    },reinterpret_cast<LPARAM>(&match));
    if(match.second||std::find(attached.begin(),attached.end(),pid)!=attached.end()) return;
    std::wstring command=L"\""+ExePath()+L"\" --attach "+std::to_wstring(pid)+L" \""+bridgePath+L"\"";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION process{};
    if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&process)) {
        CloseHandle(process.hThread);CloseHandle(process.hProcess);attached.push_back(pid);
    } else InterlockedExchange(&shared->error,HRESULT_FROM_WIN32(GetLastError()));
}
void RefreshTaskbars() {
    pending=false;
    if(config.mode!=Mode::Normal) {
        EnumWindows([](HWND window,LPARAM)->BOOL { if(IsTaskbar(window)) {DWORD pid=0;GetWindowThreadProcessId(window,&pid);Attach(pid);} return TRUE;},0);
    }
    Notify();
}
void QueueRefresh() { if(!pending&&!exiting) {pending=true;PostMessageW(control,Refresh,0,0);} }
void CALLBACK WinEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG child,DWORD,DWORD) {
    if(object!=OBJID_WINDOW||child!=CHILDID_SELF||!window) return;
    if(IsTaskbar(window) && (event==EVENT_OBJECT_CREATE||event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_LOCATIONCHANGE)) QueueRefresh();
    else if(event==EVENT_OBJECT_CREATE||event==EVENT_OBJECT_SHOW) {
        wchar_t name[100]{};GetClassNameW(window,name,100);
        if(wcscmp(name,L"Windows.UI.Composition.DesktopWindowContentBridge")==0&&IsTaskbar(GetAncestor(window,GA_ROOT))) QueueRefresh();
    }
}
bool StartupEnabled() {
    wchar_t value[32768]{};DWORD bytes=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,RunKey,L"TranslucentBar",RRF_RT_REG_SZ,nullptr,value,&bytes)!=ERROR_SUCCESS) return false;
    return std::wstring(value)==L"\""+ExePath()+L"\"";
}
bool SetStartup(bool enabled) {
    HKEY key; if(RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    LONG result;
    if(enabled) {auto value=L"\""+ExePath()+L"\"";result=RegSetValueExW(key,L"TranslucentBar",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}
    else {result=RegDeleteValueW(key,L"TranslucentBar");if(result==ERROR_FILE_NOT_FOUND) result=ERROR_SUCCESS;}
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
HICON MakeIcon() {
    const int size=32; HDC dc=GetDC(nullptr), mem=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,size,size), mask=CreateBitmap(size,size,1,1,nullptr);
    auto old=SelectObject(mem,bitmap);RECT rect{0,0,size,size};
    HBRUSH dark=CreateSolidBrush(RGB(23,37,53)); FillRect(mem,&rect,dark);DeleteObject(dark);
    HBRUSH cyan=CreateSolidBrush(RGB(91,211,238));RECT bar{5,19,27,26};FillRect(mem,&bar,cyan);DeleteObject(cyan);
    SelectObject(mem,old);old=SelectObject(mem,mask);PatBlt(mem,0,0,size,size,BLACKNESS);SelectObject(mem,old);
    ICONINFO ii{TRUE,0,0,mask,bitmap};HICON icon=CreateIconIndirect(&ii);
    DeleteObject(bitmap);DeleteObject(mask);DeleteDC(mem);ReleaseDC(nullptr,dc);return icon;
}
HICON icon;
void TrayIcon(DWORD action) {
    NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=control;data.uID=1;
    data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=Tray;data.hIcon=icon;
    wcscpy_s(data.szTip,L"TranslucentBar · clic derecho para configurar");
    Shell_NotifyIconW(action,&data);
}
void ChangeConfig() {
    if(!SaveConfig()) Error(L"No se ha podido guardar config.json. El cambio se aplica solo a esta sesión.");
    QueueRefresh();
}
void ShowStatus() {
    wchar_t counters[300]; swprintf_s(counters,L"\nElementos: %ld\nFondos aplicados: %ld\nCambios de Explorer: %ld\nÚltimo error: 0x%08lX\nFase: %ld",shared->elements,shared->applied,shared->changes,static_cast<unsigned long>(shared->error),shared->stage);
    std::wstring text=L"TranslucentBar 0.1\n\nPuente XAML: ";text+=shared->ready?L"conectado":L"sin conectar";
    text+=counters;text+=L"\n\nConfiguración: ";text+=directory;text+=L"\\config.json";
    MessageBoxW(nullptr,text.c_str(),L"Estado de TranslucentBar",MB_OK|MB_ICONINFORMATION);
}
void Menu() {
    HMENU menu=CreatePopupMenu(), modes=CreatePopupMenu(), opacity=CreatePopupMenu();
    AppendMenuW(modes,MF_STRING,201,L"Translúcido");AppendMenuW(modes,MF_STRING,202,L"Glass");AppendMenuW(modes,MF_STRING,200,L"Normal (barra de Windows)");
    CheckMenuRadioItem(modes,200,202,200+static_cast<UINT>(config.mode),MF_BYCOMMAND);
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(modes),L"Modo");
    AppendMenuW(menu,MF_STRING,Color,L"Color del tinte…");
    for(UINT n=0;n<=100;n+=5) {wchar_t text[24];swprintf_s(text,L"%u %%",n);AppendMenuW(opacity,MF_STRING|(config.opacity==n?MF_CHECKED:0),300+n,text);}
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(opacity),L"Opacidad del tinte");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(StartupEnabled()?MF_CHECKED:0),Startup,L"Iniciar con Windows");
    AppendMenuW(menu,MF_STRING,Status,L"Estado y diagnóstico…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,Exit,L"Salir y restaurar la barra");
    POINT point;GetCursorPos(&point);SetForegroundWindow(control);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,control,nullptr);DestroyMenu(menu);PostMessageW(control,WM_NULL,0,0);
    if(command>=200&&command<=202) {config.mode=static_cast<Mode>(command-200);ChangeConfig();}
    else if(command>=300&&command<=400) {config.opacity=command-300;ChangeConfig();}
    else if(command==Color) {
        static COLORREF custom[16]{};CHOOSECOLORW choose{};choose.lStructSize=sizeof(choose);choose.hwndOwner=control;
        choose.rgbResult=RGB((config.rgb>>16)&255,(config.rgb>>8)&255,config.rgb&255);choose.lpCustColors=custom;choose.Flags=CC_FULLOPEN|CC_RGBINIT;
        // El diálogo de color no forma parte de las dependencias de arranque.
        HMODULE dialogs=LoadLibraryExW(L"comdlg32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Choose=BOOL(WINAPI*)(LPCHOOSECOLORW);
        auto chooseColor=dialogs?reinterpret_cast<Choose>(GetProcAddress(dialogs,"ChooseColorW")):nullptr;
        if(chooseColor&&chooseColor(&choose)) {config.rgb=(GetRValue(choose.rgbResult)<<16)|(GetGValue(choose.rgbResult)<<8)|GetBValue(choose.rgbResult);ChangeConfig();}
        if(dialogs) FreeLibrary(dialogs);
    } else if(command==Startup) {if(!SetStartup(!StartupEnabled())) Error(L"No se ha podido cambiar el inicio con Windows en HKCU.");}
    else if(command==Status) ShowStatus();
    else if(command==Exit) PostMessageW(control,WM_CLOSE,0,0);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==taskbarCreated) {
        attached.clear();InterlockedExchange(&shared->ready,0);InterlockedExchange(&shared->elements,0);InterlockedExchange(&shared->applied,0);
        TrayIcon(NIM_ADD);QueueRefresh();return 0;
    }
    switch(message) {
    case Refresh: RefreshTaskbars();return 0;
    case WM_APP+3:Menu();return 0;
    case WM_APP+4:ShowStatus();return 0;
    case WM_APP+5:
        if(FAILED(static_cast<HRESULT>(lp))) {
            DWORD pid=static_cast<DWORD>(wp);attached.erase(std::remove(attached.begin(),attached.end(),pid),attached.end());
            InterlockedExchange(&shared->error,static_cast<LONG>(lp));
            NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=control;data.uID=1;data.uFlags=NIF_INFO;data.dwInfoFlags=NIIF_WARNING;
            wcscpy_s(data.szInfoTitle,L"No se ha podido conectar con la barra");
            wcscpy_s(data.szInfo,L"Consulta «Estado y diagnóstico». Esta versión de Explorer puede no ser compatible con XAML Diagnostics.");
            Shell_NotifyIconW(NIM_MODIFY,&data);
        }
        return 0;
    case Tray: if(lp==WM_RBUTTONUP||lp==WM_LBUTTONUP||lp==WM_CONTEXTMENU) Menu();return 0;
    case WM_DISPLAYCHANGE:case WM_DPICHANGED:case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_DWMCOMPOSITIONCHANGED:QueueRefresh();return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(wp) {Notify(true);PostMessageW(window,WM_CLOSE,0,0);}return 0;
    case WM_COPYDATA: {
        auto* data=reinterpret_cast<COPYDATASTRUCT*>(lp);
        if(data&&data->dwData==1&&data->cbData==sizeof(Config)) {
            Config next=*static_cast<Config*>(data->lpData);
            if(next.mode<=Mode::Glass&&next.rgb<=0xFFFFFF&&next.opacity<=100) {config=next;ChangeConfig();return TRUE;}
        }
        return FALSE;
    }
    case WM_CLOSE:exiting=true;Notify(true);TrayIcon(NIM_DELETE);DestroyWindow(window);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    default:return DefWindowProcW(window,message,wp,lp);
    }
}
int AttachChild(int argc,wchar_t** argv) {
    if(argc!=4) return 2;
    DWORD pid=wcstoul(argv[2],nullptr,10);
    HMODULE xaml=LoadLibraryExW(L"Windows.UI.Xaml.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Init=HRESULT(WINAPI*)(PCWSTR,DWORD,PCWSTR,PCWSTR,CLSID,PCWSTR);
    auto initialize=xaml?reinterpret_cast<Init>(GetProcAddress(xaml,"InitializeXamlDiagnosticsEx")):nullptr;
    HRESULT hr=E_NOINTERFACE;
    if(initialize) {
        bool connected=false;
        // Búsqueda de endpoints finita, solo al recibir un evento de nueva barra.
        for(unsigned n=1;n<=64;++n) {
            wchar_t endpoint[64];swprintf_s(endpoint,L"VisualDiagConnection%u",n);
            hr=initialize(endpoint,pid,L"",argv[3],TapClsid,L"");
            if(SUCCEEDED(hr)) connected=true;
            // Explorer puede tener varios núcleos XAML (barra y ventanas).
            // Se conectan todos los endpoints disponibles en la pasada finita.
        }
        if(connected) hr=S_OK;
    }
    HANDLE map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,MappingName);
    if(map) {auto* data=static_cast<Shared*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));if(data){InterlockedExchange(&data->error,hr);UnmapViewOfFile(data);}CloseHandle(map);}
    if(HWND window=FindWindowW(AppClass,nullptr)) PostMessageW(window,WM_APP+5,pid,hr);
    // El proceso auxiliar termina y libera XAML; no queda un segundo proceso en reposo.
    return FAILED(hr)?1:0;
}
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int) {
    instance=inst;int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) return 1;
    if(argc>1&&wcscmp(argv[1],L"--attach")==0) {int result=AttachChild(argc,argv);LocalFree(argv);return result;}
    HWND previous=FindWindowW(AppClass,nullptr);
    bool quit=argc==2&&wcscmp(argv[1],L"--salir")==0;
    bool menu=argc==2&&wcscmp(argv[1],L"--menu")==0;
    bool status=argc==2&&wcscmp(argv[1],L"--estado")==0;
    bool set=argc==3&&wcscmp(argv[1],L"--modo")==0;
    Mode mode=Mode::Normal;
    if(set) {if(wcscmp(argv[2],L"glass")==0) mode=Mode::Glass;else if(wcscmp(argv[2],L"translucido")==0) mode=Mode::Translucent;else if(wcscmp(argv[2],L"normal")!=0) {LocalFree(argv);return 2;}}
    if(quit) {if(previous) PostMessageW(previous,WM_CLOSE,0,0);LocalFree(argv);return 0;}
    if(menu) {if(previous) PostMessageW(previous,WM_APP+3,0,0);LocalFree(argv);return previous?0:1;}
    if(status) {if(previous) PostMessageW(previous,WM_APP+4,0,0);LocalFree(argv);return previous?0:1;}
    if(argc>1&&!set) {LocalFree(argv);return 2;}
    directory=DataDirectory();if(directory.empty()) {Error(L"No se puede crear la carpeta de configuración.");LocalFree(argv);return 1;}
    bool valid=LoadConfig(config);if(set) config.mode=mode;
    if(previous) {
        if(set) {COPYDATASTRUCT data{1,sizeof(config),&config};DWORD_PTR result;SendMessageTimeoutW(previous,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG,2000,&result);}
        LocalFree(argv);return 0;
    }
    LocalFree(argv);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\TranslucentBar.Singleton.v1");
    if(!mutex||GetLastError()==ERROR_ALREADY_EXISTS) {if(mutex)CloseHandle(mutex);return 0;}
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),MappingName);
    shared=mapping?static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared))):nullptr;
    if(!shared) {Error(L"No se puede crear la configuración compartida.");return 1;}
    ZeroMemory(shared,sizeof(Shared));shared->pid=GetCurrentProcessId();shared->config=config;
    if(!ExtractBridge()) {Error(L"No se ha podido extraer o verificar el puente XAML.");return 1;}
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=AppClass;RegisterClassW(&wc);
    control=CreateWindowExW(WS_EX_TOOLWINDOW,AppClass,L"TranslucentBar",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!control) return 1;
    icon=MakeIcon();TrayIcon(NIM_ADD);
    createHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_SHOW,nullptr,WinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    locationHook=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,WinEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if(!createHook||!locationHook) Error(L"No se han podido registrar todos los eventos de Windows. Puede fallar la recuperación de la barra.");
    if(!valid) Error(L"config.json no es válido. Se usarán los valores predeterminados; el archivo se conserva hasta que cambies una opción.");
    if(valid) SaveConfig();QueueRefresh();
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    if(createHook) UnhookWinEvent(createHook);if(locationHook) UnhookWinEvent(locationHook);
    DestroyIcon(icon);UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(mutex);return 0;
}
