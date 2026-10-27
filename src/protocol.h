// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>

namespace tb {
inline constexpr wchar_t AppClass[] = L"TranslucentBar.Control.v1";
inline constexpr wchar_t BridgeClass[] = L"TranslucentBar.Xaml.v1";
inline constexpr wchar_t UpdateMessage[] = L"TranslucentBar.Actualizar.v1";
inline constexpr wchar_t MappingName[] = L"Local\\TranslucentBar.Config.v1";
inline constexpr CLSID TapClsid = {0x12c1a0f7,0x8b44,0x4e09,{0x91,0xb4,0xf0,0xe8,0x22,0x35,0x9a,0x64}};
enum class Mode : uint32_t { Normal, Translucent, Glass };
struct Config { Mode mode = Mode::Translucent; uint32_t rgb = 0x172535; uint32_t opacity = 25; };
struct Shared {
    DWORD pid;
    Config config;
    volatile LONG ready;
    volatile LONG elements;
    volatile LONG applied;
    volatile LONG error;
    volatile LONG changes;
    volatile LONG stage;
    wchar_t detail[512];
    volatile LONG restored;
    volatile LONG restoreErrors;
};
inline UINT UpdateMsg() { return RegisterWindowMessageW(UpdateMessage); }
template<class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
}
