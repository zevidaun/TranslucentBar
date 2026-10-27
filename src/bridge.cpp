// SPDX-License-Identifier: GPL-3.0-or-later
// Código original. Solo se usan contratos COM/WinRT del SDK, sin C++/WinRT.
#include "protocol.h"
// MinGW 14 genera IReference<BYTE> e IReference<boolean> con el mismo tipo
// C++ unsigned char. No usamos esa especialización; evitar su definición doble.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.ui.xaml.h>
#include <xamlom.h>
#include <roapi.h>
#include <winstring.h>
#include <vector>
#include <algorithm>

using namespace tb;
namespace xaml = ABI::Windows::UI::Xaml;
namespace {
// Primer miembro del contrato IShapeStatics publicado por el SDK de Microsoft.
struct ShapeStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_FillProperty(xaml::IDependencyProperty**) = 0;
};
constexpr IID ShapeStaticsId = {0x1d7b4c55,0x9df3,0x48dc,{0x91,0x94,0x9d,0x30,0x6f,0xaa,0x60,0x89}};
HMODULE module;
struct ThreadState;
struct Target;
thread_local ThreadState* local;

bool Same(IUnknown* a, IUnknown* b) {
    IUnknown *aa = nullptr, *bb = nullptr;
    if (a) a->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&aa));
    if (b) b->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&bb));
    bool same = aa == bb; Release(aa); Release(bb); return same;
}
template<class T> HRESULT Factory(const wchar_t* name, REFIID iid, T** out) {
    HSTRING s; HRESULT hr = WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &s);
    if (SUCCEEDED(hr)) { hr = RoGetActivationFactory(s, iid, reinterpret_cast<void**>(out)); WindowsDeleteString(s); }
    return hr;
}
struct Target final : xaml::IDependencyPropertyChangedCallback {
    LONG refs = 1;
    InstanceHandle handle = 0;
    xaml::IDependencyObject* object = nullptr;
    xaml::IDependencyObject2* events = nullptr;
    xaml::IDependencyProperty* fill = nullptr;
    IInspectable *original = nullptr, *effective = nullptr, *brush = nullptr;
    INT64 token = 0;
    bool subscribed = false, changed = false, writing = false, line = false;
    Config lastConfig{};
    bool configured = false;
    ThreadState* owner;
    Target(ThreadState* state, InstanceHandle h, bool stroke) : handle(h), line(stroke), owner(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(xaml::IDependencyPropertyChangedCallback)) { *out = static_cast<xaml::IDependencyPropertyChangedCallback*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(xaml::IDependencyObject*, xaml::IDependencyProperty*) override;
    HRESULT Init();
    HRESULT Apply(Config config);
    void Restore();
    void Detach() {
        Restore();
        if (subscribed) events->UnregisterPropertyChangedCallback(fill, token);
        subscribed = false;
        tb::Release(events); tb::Release(object); tb::Release(fill);
    }
};
struct ThreadState {
    IVisualTreeService2* service = nullptr;
    IXamlDiagnostics* diagnostics = nullptr;
    HWND window = nullptr;
    HANDLE mapping = nullptr, process = nullptr, wait = nullptr;
    Shared* shared = nullptr;
    DWORD controllerPid = 0;
    bool queued = false;
    std::vector<Target*> targets;
    void Disconnect() {
        if (wait) { UnregisterWaitEx(wait, INVALID_HANDLE_VALUE); wait = nullptr; }
        if (process) { CloseHandle(process); process = nullptr; }
        for (auto* target : targets) target->Restore();
        if (shared) { InterlockedExchange(&shared->applied, 0); UnmapViewOfFile(shared); shared = nullptr; }
        if (mapping) { CloseHandle(mapping); mapping = nullptr; }
        controllerPid = 0;
    }
    void Update(DWORD pid) {
        if (!shared || controllerPid != pid) {
            Disconnect();
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName);
            if (!mapping) return;
            shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
            if (!shared || shared->pid != pid) { Disconnect(); return; }
            controllerPid = pid;
            process = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (!process || !RegisterWaitForSingleObject(&wait, process, [](void* context, BOOLEAN) {
                auto* state=static_cast<ThreadState*>(context);
                PostMessageW(state->window, UpdateMsg(), 0, state->controllerPid);
            }, this, INFINITE, WT_EXECUTEONLYONCE)) { Disconnect(); return; }
        }
        Config config = shared->config;
        LONG applied = 0;
        for (auto* target : targets) {
            HRESULT hr = target->Apply(config);
            if (FAILED(hr)) InterlockedExchange(&shared->error, hr);
            else if (config.mode != Mode::Normal && !target->line) ++applied;
        }
        InterlockedExchange(&shared->elements, static_cast<LONG>(targets.size()));
        InterlockedExchange(&shared->applied, applied);
        InterlockedExchange(&shared->ready, 1);
    }
};
void Target::Restore() {
    if (!changed) return;
    writing = true;
    if (original) object->SetValue(fill, original); else object->ClearValue(fill);
    IInspectable* after=nullptr;HRESULT hr=object->GetValue(fill,&after);
    if(owner->shared) {
        InterlockedIncrement(&owner->shared->restored);
        if(FAILED(hr)||!Same(after,effective)) InterlockedIncrement(&owner->shared->restoreErrors);
    }
    tb::Release(after);tb::Release(effective);
    writing = false; changed = false; tb::Release(brush); tb::Release(original);
}
LRESULT CALLBACK BridgeProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<ThreadState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<ThreadState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state && message == UpdateMsg()) {
        state->queued = false;
        if (!wp) {if (!lp||state->controllerPid==static_cast<DWORD>(lp)) state->Disconnect();}
        else state->Update(static_cast<DWORD>(wp));
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
ThreadState* State(IVisualTreeService2* service, IXamlDiagnostics* diagnostics) {
    if (local) return local;
    WNDCLASSW wc{}; wc.lpfnWndProc = BridgeProc; wc.hInstance = module; wc.lpszClassName = BridgeClass;
    RegisterClassW(&wc);
    auto* state = new ThreadState;
    state->service=service;service->AddRef();state->diagnostics=diagnostics;diagnostics->AddRef();
    state->window = CreateWindowExW(WS_EX_TOOLWINDOW, BridgeClass, L"", WS_POPUP, 0,0,0,0,nullptr,nullptr,module,state);
    if (!state->window) { delete state; return nullptr; }
    local = state;
    return state;
}
void Queue(ThreadState* state) {
    if (!state->queued && state->shared) {
        state->queued = true;
        PostMessageW(state->window, UpdateMsg(), state->shared->pid, 0);
    }
}
HRESULT Target::Init() {
    IInspectable* inspectable = nullptr;
    HRESULT hr = owner->diagnostics->GetIInspectableFromHandle(handle, &inspectable);
    if (FAILED(hr)) return hr;
    hr = inspectable->QueryInterface(__uuidof(xaml::IDependencyObject), reinterpret_cast<void**>(&object));
    if (SUCCEEDED(hr)) hr = inspectable->QueryInterface(__uuidof(xaml::IDependencyObject2), reinterpret_cast<void**>(&events));
    tb::Release(inspectable);
    ShapeStatics* statics = nullptr;
    if (SUCCEEDED(hr)) hr = Factory(L"Windows.UI.Xaml.Shapes.Shape", ShapeStaticsId, &statics);
    if (SUCCEEDED(hr)) hr = statics->get_FillProperty(&fill);
    tb::Release(statics);
    if (SUCCEEDED(hr)) hr = events->RegisterPropertyChangedCallback(fill, this, &token);
    subscribed = SUCCEEDED(hr);
    return hr;
}
HRESULT Target::Invoke(xaml::IDependencyObject*, xaml::IDependencyProperty*) {
    if (writing || !owner->shared || owner->shared->config.mode == Mode::Normal) return S_OK;
    // Explorer ha puesto un nuevo pincel: conservarlo para restaurar el tema actual.
    IInspectable* value = nullptr;
    if (SUCCEEDED(object->GetValue(fill, &value)) && !Same(value, brush)) {
        tb::Release(original); original = value; value = nullptr;
        tb::Release(effective);effective=original;if(effective)effective->AddRef();
        changed = true;
        InterlockedIncrement(&owner->shared->changes);
        Queue(owner);
    }
    tb::Release(value);
    return S_OK;
}
HRESULT Create(const wchar_t* type, const wchar_t* text, InstanceHandle* value) {
    BSTR t = SysAllocString(type), v = text ? SysAllocString(text) : nullptr;
    HRESULT hr = local->service->CreateInstance(t, v, value); SysFreeString(t); SysFreeString(v); return hr;
}
HRESULT Set(InstanceHandle instance, const wchar_t* name, const wchar_t* type, const wchar_t* text) {
    UINT index = 0; InstanceHandle value = 0;
    // GetPropertyIndex devuelve E_INVALIDARG en el runtime de Explorer 26300.
    // Obtener el índice de su cadena de propiedades funciona también allí.
    HRESULT hr = E_INVALIDARG;
    UINT count=0,sourcesCount=0;PropertyChainSource* sources=nullptr;PropertyChainValue* values=nullptr;
    auto* service=local->service;
    HRESULT query=service->GetPropertyValuesChain(instance,&sourcesCount,&sources,&count,&values);
    if(SUCCEEDED(query)) {
        for(UINT i=0;i<count;++i) {
            auto& p=values[i];
            if(p.PropertyName&&wcscmp(p.PropertyName,name)==0) {index=p.Index;hr=S_OK;}
            SysFreeString(p.Type);SysFreeString(p.DeclaringType);SysFreeString(p.ValueType);SysFreeString(p.ItemType);SysFreeString(p.Value);SysFreeString(p.PropertyName);
        }
        CoTaskMemFree(values);
        for(UINT i=0;i<sourcesCount;++i) {auto& s=sources[i];SysFreeString(s.TargetType);SysFreeString(s.Name);SysFreeString(s.SrcInfo.FileName);SysFreeString(s.SrcInfo.Hash);}
        CoTaskMemFree(sources);
    } else hr=query;
    if (SUCCEEDED(hr)) { InterlockedExchange(&local->shared->stage,21); hr = Create(type, text, &value); }
    if (SUCCEEDED(hr)) { InterlockedExchange(&local->shared->stage,22); hr = service->SetProperty(instance, value, index); }
    if (FAILED(hr)) {
        UINT count=0,sourcesCount=0;PropertyChainSource* sources=nullptr;PropertyChainValue* values=nullptr;
        if(SUCCEEDED(service->GetPropertyValuesChain(instance,&sourcesCount,&sources,&count,&values))) {
            for(UINT i=0;i<count;++i) {
                auto& p=values[i];
                if(p.PropertyName&&wcscmp(p.PropertyName,name)==0) swprintf_s(local->shared->detail,L"%s: tipo=%s valor=%s",name,p.Type?p.Type:L"",p.Value?p.Value:L"");
                SysFreeString(p.Type);SysFreeString(p.DeclaringType);SysFreeString(p.ValueType);SysFreeString(p.ItemType);SysFreeString(p.Value);SysFreeString(p.PropertyName);
            }
            CoTaskMemFree(values);
            for(UINT i=0;i<sourcesCount;++i) {auto& s=sources[i];SysFreeString(s.TargetType);SysFreeString(s.Name);SysFreeString(s.SrcInfo.FileName);SysFreeString(s.SrcInfo.Hash);}
            CoTaskMemFree(sources);
        }
    }
    return hr;
}
HRESULT Target::Apply(Config config) {
    if (config.mode == Mode::Normal) { Restore(); return S_OK; }
    if (configured && brush && lastConfig.mode==config.mode && lastConfig.rgb==config.rgb && lastConfig.opacity==config.opacity) {
        writing=true; HRESULT hr=object->SetValue(fill,brush); writing=false; return hr;
    }
    InstanceHandle h = 0;
    bool glass = config.mode == Mode::Glass && !line;
    InterlockedExchange(&owner->shared->stage, 1);
    HRESULT hr = Create(glass ? L"Windows.UI.Xaml.Media.AcrylicBrush" : L"Windows.UI.Xaml.Media.SolidColorBrush", nullptr, &h);
    wchar_t color[16], opacity[32];
    swprintf_s(color, L"#%02X%06X", line ? 0 : (glass ? 255 : config.opacity * 255 / 100), config.rgb);
    if (SUCCEEDED(hr)) { InterlockedExchange(&owner->shared->stage, 2); hr = Set(h, glass ? L"TintColor" : L"Color", L"Windows.UI.Color", color); }
    if (glass && SUCCEEDED(hr)) {
        swprintf_s(opacity, L"%u.%02u", config.opacity / 100, config.opacity % 100);
        hr = Set(h, L"TintOpacity", L"Double", opacity);
        // La ventana de la barra ya compone contenido transparente. Backdrop
        // toma ese contenido; HostBackdrop cae a sólido al perder activación.
        if (SUCCEEDED(hr)) hr = Set(h, L"BackgroundSource", L"Windows.UI.Xaml.Media.AcrylicBackgroundSource", L"Backdrop");
        if (SUCCEEDED(hr)) hr = Set(h, L"FallbackColor", L"Windows.UI.Color", color);
    }
    IInspectable* next = nullptr;
    if (SUCCEEDED(hr)) { InterlockedExchange(&owner->shared->stage, 3); hr = owner->diagnostics->GetIInspectableFromHandle(h, &next); }
    if (SUCCEEDED(hr)) {
        if (!changed) {
            IInspectable* value = nullptr;
            xaml::IDependencyPropertyStatics* statics = nullptr;
            IInspectable* unset = nullptr;
            InterlockedExchange(&owner->shared->stage, 4); hr = object->ReadLocalValue(fill, &value);
            tb::Release(effective);object->GetValue(fill,&effective);
            if (SUCCEEDED(Factory(L"Windows.UI.Xaml.DependencyProperty", __uuidof(xaml::IDependencyPropertyStatics), &statics))) statics->get_UnsetValue(&unset);
            if (value && !Same(value, unset)) original = value; else tb::Release(value);
            tb::Release(unset); tb::Release(statics);
        }
        if (SUCCEEDED(hr)) {
            InterlockedExchange(&owner->shared->stage, 5); writing = true; hr = object->SetValue(fill, next); writing = false;
            if (SUCCEEDED(hr)) { changed = true; tb::Release(brush); brush = next; next = nullptr; lastConfig=config; configured=true; InterlockedExchange(&owner->shared->stage,0); }
        }
    }
    tb::Release(next); return hr;
}
struct Tap final : IObjectWithSite, IVisualTreeServiceCallback2 {
    LONG refs = 1;
    IVisualTreeService2* service = nullptr;
    IXamlDiagnostics* diagnostics = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IObjectWithSite) *out = static_cast<IObjectWithSite*>(this);
        else if (id == __uuidof(IVisualTreeServiceCallback) || id == __uuidof(IVisualTreeServiceCallback2)) *out = static_cast<IVisualTreeServiceCallback2*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        if (!site) return S_OK;
        if (service) return S_OK; // Una suscripción por sesión diagnóstica.
        HRESULT hr = site->QueryInterface(__uuidof(IXamlDiagnostics), reinterpret_cast<void**>(&diagnostics));
        if (SUCCEEDED(hr)) hr = site->QueryInterface(__uuidof(IVisualTreeService2), reinterpret_cast<void**>(&service));
        if (FAILED(hr)) return hr;
        AddRef();
        HANDLE thread = CreateThread(nullptr, 0, [](void* context)->DWORD {
            auto* tap = static_cast<Tap*>(context);
            tap->service->AdviseVisualTreeChange(tap);
            tap->Release(); return 0;
        }, this, 0, nullptr);
        if (!thread) { Release(); return HRESULT_FROM_WIN32(GetLastError()); }
        CloseHandle(thread); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID id, void** out) override { return diagnostics ? diagnostics->QueryInterface(id,out) : E_FAIL; }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element, VisualMutationType mutation) override {
        if (mutation == Remove) {
            if (local) {
                auto& targets = local->targets;
                auto it = std::find_if(targets.begin(), targets.end(), [&](Target* t){return t->handle == element.Handle;});
                if (it != targets.end()) { (*it)->Detach(); (*it)->Release(); targets.erase(it); Queue(local); }
            }
            return S_OK;
        }
        if (!element.Name || !element.Type || !wcsstr(element.Type, L"Rectangle")) return S_OK;
        bool line = wcscmp(element.Name,L"BackgroundStroke") == 0;
        if (!line && wcscmp(element.Name,L"BackgroundFill") != 0) return S_OK;
        auto* state = State(service,diagnostics); if (!state) return S_OK;
        for (auto* t : state->targets) if (t->handle == element.Handle) return S_OK;
        auto* target = new Target(state, element.Handle, line);
        HRESULT hr = target->Init();
        if (SUCCEEDED(hr)) {
            state->targets.push_back(target);
            HANDLE map = OpenFileMappingW(FILE_MAP_READ,FALSE,MappingName);
            if (map) {
                auto* shared = static_cast<const Shared*>(MapViewOfFile(map,FILE_MAP_READ,0,0,sizeof(Shared)));
                if (shared) { PostMessageW(state->window,UpdateMsg(),shared->pid,0); UnmapViewOfFile(shared); }
                CloseHandle(map);
            }
        } else { target->Detach(); target->Release(); }
        return S_OK;
    }
};
struct FactoryObject final : IClassFactory {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = this; return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* tap = new Tap; HRESULT hr = tap->QueryInterface(id,out); tap->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
} factory;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID id, void** out) {
    if (clsid != TapClsid) return CLASS_E_CLASSNOTAVAILABLE;
    return factory.QueryInterface(id,out);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) module = instance;
    return TRUE;
}
