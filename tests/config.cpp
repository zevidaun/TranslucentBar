// SPDX-License-Identifier: GPL-3.0-or-later
// Casos de frontera del archivo de configuración, en una carpeta de prueba.
#define wWinMain TranslucentBarAppEntry
#include "../src/main.cpp"
#undef wWinMain
#include <cassert>
int main() {
    directory=L"build\\config-test";CreateDirectoryW(directory.c_str(),nullptr);
    auto test=[](const std::string& text,bool valid) {
        HANDLE file=CreateFileW((directory+L"\\config.json").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        assert(file!=INVALID_HANDLE_VALUE);DWORD written;WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);CloseHandle(file);
        Config c;assert(LoadConfig(c)==valid);
        if(valid) {assert(c.mode==Mode::Glass);assert(c.rgb==0xABCDEF);assert(c.opacity==100);}
    };
    test("{\"color\":\"#abcdef\",\"opacidad\":100,\"modo\":\"glass\"}",true);
    test("\xEF\xBB\xBF{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":100}\r\n",true);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":101}",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":-1}",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":1.5}",false);
    test("{\"modo\":\"glass\",\"modo\":\"glass\",\"opacidad\":100}",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEG\",\"opacidad\":100}",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":999999999999999}",false);
    test("{\"modo\":\"desconocido\",\"color\":\"#ABCDEF\",\"opacidad\":100}",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":100} basura",false);
    test("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":100,}",false);
    test(std::string("{\"modo\":\"glass\",\"color\":\"#ABCDEF\",\"opacidad\":100}")+std::string(1,'\0')+"basura",false);
    test(std::string(1025,' '),false);
    test("{}",false);
    test("",false);
    config={Mode::Glass,0xABCDEF,100};assert(SaveConfig());Config loaded;assert(LoadConfig(loaded));assert(loaded.mode==config.mode&&loaded.rgb==config.rgb&&loaded.opacity==config.opacity);
    // Comprobar HKCU sin dejar alterado el inicio del usuario.
    HKEY key=nullptr;assert(RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS);
    BYTE backup[65536];DWORD bytes=sizeof(backup),type=0;LONG existed=RegQueryValueExW(key,L"TranslucentBar",nullptr,&type,backup,&bytes);
    assert(existed==ERROR_SUCCESS||existed==ERROR_FILE_NOT_FOUND);
    bool startupOk=SetStartup(true)&&StartupEnabled()&&SetStartup(false)&&!StartupEnabled();
    LONG restored=existed==ERROR_SUCCESS?RegSetValueExW(key,L"TranslucentBar",0,type,backup,bytes):RegDeleteValueW(key,L"TranslucentBar");
    RegCloseKey(key);assert(startupOk);assert(restored==ERROR_SUCCESS||restored==ERROR_FILE_NOT_FOUND);
    puts("16 comprobaciones de configuración y prueba reversible de HKCU correctas.");
    return 0;
}
