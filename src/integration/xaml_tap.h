#pragma once

#include "../core/platform.h"
#include "../core/state.h"
#include "../core/logging.h"
#include "../ui/taskbar/widgets.h"

namespace codex_dashboard {

// {98E44439-8BF8-4B3F-8B8F-6F9D08C8B842}
static constexpr CLSID CLSID_WindhawkTAP = {
    0x98e44439,
    0x8bf8,
    0x4b3f,
    {0x8b, 0x8f, 0x6f, 0x9d, 0x08, 0xc8, 0xb8, 0x42}};

class VisualTreeWatcher
    : public winrt::implements<VisualTreeWatcher,
                               IVisualTreeServiceCallback2,
                               winrt::non_agile> {
public:
    explicit VisualTreeWatcher(winrt::com_ptr<IUnknown> site)
        : m_xamlDiagnostics(site.as<IXamlDiagnostics>()) {
        Wh_Log(L"Constructing VisualTreeWatcher");

        m_adviseCompleted = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!m_adviseCompleted) {
            winrt::throw_last_error();
        }

        AddRef();
        HANDLE thread = CreateThread(
            nullptr, 0,
            [](LPVOID parameter) -> DWORD {
                auto watcher = reinterpret_cast<VisualTreeWatcher*>(parameter);
                bool releaseNeeded = true;
                try {
                    HRESULT hr = watcher->m_xamlDiagnostics
                                     .as<IVisualTreeService3>()
                                     ->AdviseVisualTreeChange(watcher);
                    if (FAILED(hr)) {
                        Wh_Log(L"AdviseVisualTreeChange failed: %08X", hr);
                    } else {
                        watcher->m_advised = true;
                        if (watcher->m_unadviseRequested &&
                            watcher->m_advised.load()) {
                            HRESULT unadviseHr = watcher->m_xamlDiagnostics
                                                     .as<IVisualTreeService3>()
                                                     ->UnadviseVisualTreeChange(
                                                         watcher);
                            if (SUCCEEDED(unadviseHr)) {
                                watcher->m_advised = false;
                            } else {
                                Wh_Log(L"Deferred UnadviseVisualTreeChange failed: %08X",
                                       unadviseHr);
                            }
                        }
                    }

                    SetEvent(watcher->m_adviseCompleted);
                    watcher->Release();
                    releaseNeeded = false;
                    return 0;
                } catch (...) {
                    LogException(L"AdviseVisualTreeChangeThread",
                                 CurrentExceptionHResult());
                    SetEvent(watcher->m_adviseCompleted);
                    if (releaseNeeded) {
                        watcher->Release();
                    }

                    return 0;
                }
            },
            this, 0, nullptr);

        if (thread) {
            CloseHandle(thread);
        } else {
            SetEvent(m_adviseCompleted);
            Release();
        }
    }

    ~VisualTreeWatcher() {
        if (m_adviseCompleted) {
            CloseHandle(m_adviseCompleted);
        }
    }

    void UnadviseVisualTreeChange() noexcept {
        try {
            Wh_Log(L"UnadviseVisualTreeChange");

            m_unadviseRequested = true;
            DWORD waitResult = WaitForSingleObject(m_adviseCompleted, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                Wh_Log(L"Waiting for AdviseVisualTreeChange failed: %u",
                       GetLastError());
                return;
            }
            if (m_advised.load()) {
                HRESULT hr = m_xamlDiagnostics.as<IVisualTreeService3>()
                                 ->UnadviseVisualTreeChange(this);
                if (FAILED(hr)) {
                    Wh_Log(L"UnadviseVisualTreeChange failed: %08X", hr);
                } else {
                    m_advised = false;
                }
            }
        } catch (...) {
            LogException(L"UnadviseVisualTreeChange",
                         CurrentExceptionHResult());
        }
    }

private:
    void EnsureWidgetFromHandle(InstanceHandle handle) {
        wf::IInspectable inspectable;
        winrt::check_hresult(m_xamlDiagnostics->GetIInspectableFromHandle(
            handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(inspectable))));

        auto frameworkElement = inspectable.try_as<wux::FrameworkElement>();
        if (frameworkElement) {
            EnsureWidgetForTray(frameworkElement);
        }
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation,
        VisualElement element,
        VisualMutationType mutationType) override try {
        if (g_unloading || g_reconcilingWidgets) {
            return S_OK;
        }
        if (mutationType == Remove) {
            g_taskbarRecoveryRequested = true;
            return S_OK;
        }
        if (mutationType == Add) {
            EnsureWidgetFromHandle(element.Handle);
        }

        return S_OK;
    } catch (...) {
        HRESULT hr = CurrentExceptionHResult();
        if (hr != static_cast<HRESULT>(0x800F1000)) {
            LogException(L"OnVisualTreeChange", hr);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle handle,
        VisualElementState,
        LPCWSTR) override try {
        if (!g_unloading && !g_reconcilingWidgets) {
            EnsureWidgetFromHandle(handle);
        }
        return S_OK;
    } catch (...) {
        HRESULT hr = CurrentExceptionHResult();
        if (hr != static_cast<HRESULT>(0x800F1000)) {
            LogException(L"OnElementStateChanged", hr);
        }
        return S_OK;
    }

    winrt::com_ptr<IXamlDiagnostics> m_xamlDiagnostics;
    HANDLE m_adviseCompleted = nullptr;
    std::atomic<bool> m_advised = false;
    std::atomic<bool> m_unadviseRequested = false;
};

winrt::com_ptr<VisualTreeWatcher> g_visualTreeWatcher;

class WindhawkTAP
    : public winrt::implements<WindhawkTAP, IObjectWithSite, winrt::non_agile> {
public:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override try {
        Wh_Log(L"WindhawkTAP SetSite: %s", site ? L"site" : L"null");

        if (g_visualTreeWatcher) {
            g_visualTreeWatcher->UnadviseVisualTreeChange();
            g_visualTreeWatcher = nullptr;
        }

        m_site.copy_from(site);
        if (m_site) {
            FreeLibrary(GetCurrentModuleHandle());
            g_visualTreeWatcher = winrt::make_self<VisualTreeWatcher>(m_site);
        }

        return S_OK;
    } catch (...) {
        HRESULT hr = CurrentExceptionHResult();
        LogException(L"SetSite", hr);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** site) noexcept override {
        return m_site.as(riid, site);
    }

private:
    winrt::com_ptr<IUnknown> m_site;
};

template <class T>
struct SimpleFactory
    : winrt::implements<SimpleFactory<T>, IClassFactory, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,
                                             REFIID riid,
                                             void** object) override try {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }

        *object = nullptr;
        return winrt::make<T>().as(riid, object);
    } catch (...) {
        HRESULT hr = CurrentExceptionHResult();
        LogException(L"CreateInstance", hr);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override {
        return S_OK;
    }
};

}  // namespace codex_dashboard

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"

__declspec(dllexport) STDAPI DllGetClassObject(REFCLSID clsid,
                                               REFIID riid,
                                               LPVOID* object) try {
    if (clsid != codex_dashboard::CLSID_WindhawkTAP) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    *object = nullptr;
    return winrt::make<codex_dashboard::SimpleFactory<codex_dashboard::WindhawkTAP>>().as(riid, object);
} catch (...) {
    HRESULT hr = codex_dashboard::CurrentExceptionHResult();
    codex_dashboard::LogException(L"DllGetClassObject", hr);
    return hr;
}

__declspec(dllexport) STDAPI DllCanUnloadNow() {
    return winrt::get_module_lock() ? S_FALSE : S_OK;
}

#pragma clang diagnostic pop

namespace codex_dashboard {

using InitializeXamlDiagnosticsEx_t =
    decltype(&InitializeXamlDiagnosticsEx);

HRESULT InjectWindhawkTAP() noexcept {
    HMODULE module = GetCurrentModuleHandle();
    if (!module) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    WCHAR location[MAX_PATH];
    switch (GetModuleFileName(module, location, ARRAYSIZE(location))) {
        case 0:
        case ARRAYSIZE(location):
            return HRESULT_FROM_WIN32(GetLastError());
    }

    HMODULE wux = LoadLibraryEx(L"Windows.UI.Xaml.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wux) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto initializeXamlDiagnosticsEx =
        reinterpret_cast<InitializeXamlDiagnosticsEx_t>(
            GetProcAddress(wux, "InitializeXamlDiagnosticsEx"));
    if (!initializeXamlDiagnosticsEx) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = E_FAIL;
    for (int i = 0; i < 10000; i++) {
        WCHAR connectionName[256];
        wsprintf(connectionName, L"VisualDiagConnection%d", i + 1);

        hr = initializeXamlDiagnosticsEx(connectionName, GetCurrentProcessId(),
                                         L"", location, CLSID_WindhawkTAP,
                                         nullptr);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    }

    return hr;
}

void InitializeForCurrentThread() {
    RestoreWidgetsForCurrentThread();
}

void UninitializeForCurrentThread() {
    RemoveWidgets();
}

bool InitializeSettingsAndTap() {
    if (g_initialized.exchange(true)) {
        return true;
    }

    Wh_Log(L"Injecting Windhawk TAP");

    HRESULT hr = InjectWindhawkTAP();
    if (FAILED(hr)) {
        Wh_Log(L"InjectWindhawkTAP failed: %08X", hr);
        g_initialized = false;
        return false;
    } else {
        Wh_Log(L"Windhawk TAP injected");
        return true;
    }
}

void UninitializeSettingsAndTap() {
    if (g_visualTreeWatcher) {
        g_visualTreeWatcher->UnadviseVisualTreeChange();
        g_visualTreeWatcher = nullptr;
    }

    g_initialized = false;
}

}  // namespace codex_dashboard
