#pragma once

#include "../../core/state.h"
#include "../../core/logging.h"
#include "../popup/window.h"

namespace codex_dashboard {

void EnsureWidgetForTray(const wux::FrameworkElement& tray);
void RestoreWidgetsForCurrentThread();

bool IsNamedElement(const wux::FrameworkElement& element, PCWSTR name) {
    return element && element.Name() == name;
}

wux::FrameworkElement FindAncestorByName(wux::FrameworkElement element,
                                         PCWSTR name) {
    for (auto parent = wuxm::VisualTreeHelper::GetParent(element); parent;) {
        auto parentElement = parent.try_as<wux::FrameworkElement>();
        if (!parentElement) {
            break;
        }

        if (IsNamedElement(parentElement, name)) {
            return parentElement;
        }

        parent = wuxm::VisualTreeHelper::GetParent(parentElement);
    }

    return nullptr;
}

wuxc::Panel FindAncestorPanel(wux::FrameworkElement element) {
    for (auto parent = wuxm::VisualTreeHelper::GetParent(element); parent;) {
        auto panel = parent.try_as<wuxc::Panel>();
        if (panel) {
            return panel;
        }

        parent = wuxm::VisualTreeHelper::GetParent(parent);
    }

    return nullptr;
}

wux::Thickness MakeWidgetMargin(double trayWidth) {
    return wux::Thickness{0, 0, std::max(0.0, trayWidth) + 8, 0};
}

void UpdateWidgetPosition(const wux::FrameworkElement& tray,
                          const wux::FrameworkElement& widget) {
    if (!tray || !widget) {
        return;
    }

    widget.Margin(MakeWidgetMargin(tray.ActualWidth()));
}

wux::FrameworkElement FindRootWidget(const wuxc::Panel& root) {
    const auto children = root.Children();
    for (uint32_t i = 0; i < children.Size(); i++) {
        auto child = children.GetAt(i).try_as<wux::FrameworkElement>();
        if (IsNamedElement(child, L"CodexLbDashboardWidget")) {
            return child;
        }
    }

    return nullptr;
}

winrt::Windows::UI::Color GetPercentColor(int percent) {
    if (percent >= 61) {
        return winrt::Windows::UI::Color{0xFF, 0x6A, 0xD4, 0x7C};
    }

    if (percent >= 31) {
        return winrt::Windows::UI::Color{0xFF, 0xF6, 0xC4, 0x53};
    }

    return winrt::Windows::UI::Color{0xFF, 0xF2, 0x71, 0x71};
}

void AppendTextRun(const wuxc::TextBlock& textBlock,
                   const std::wstring& text,
                   winrt::Windows::UI::Color color) {
    if (text.empty()) {
        return;
    }

    auto run = wuxd::Run();
    run.Text(text);
    run.Foreground(wuxm::SolidColorBrush(color));
    textBlock.Inlines().Append(run);
}

void SetTextBlockDashboardText(const wuxc::TextBlock& textBlock,
                               const std::wstring& text) {
    textBlock.Inlines().Clear();

    const auto defaultColor = winrt::Windows::UI::Colors::White();
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == L'\n') {
            textBlock.Inlines().Append(wuxd::LineBreak());
            i++;
            continue;
        }

        if (text[i] >= L'0' && text[i] <= L'9') {
            size_t numberStart = i;
            int percent = 0;
            while (i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
                percent = percent * 10 + (text[i] - L'0');
                i++;
            }

            if (i < text.size() && text[i] == L'%') {
                i++;
                AppendTextRun(textBlock, text.substr(numberStart,
                                                     i - numberStart),
                              GetPercentColor(percent));
                continue;
            }

            AppendTextRun(textBlock, text.substr(numberStart, i - numberStart),
                          defaultColor);
            continue;
        }

        size_t textStart = i;
        while (i < text.size() && text[i] != L'\n' &&
               !(text[i] >= L'0' && text[i] <= L'9')) {
            i++;
        }

        AppendTextRun(textBlock, text.substr(textStart, i - textStart),
                      defaultColor);
    }
}

bool ShouldShowLatestUpdateIndicator(const DashboardVersion& version,
                                     int remainingPercent) {
    return remainingPercent >= 0 && version.latestUpdateAvailable;
}

void SetWidgetText(const wux::FrameworkElement& widget,
                   const std::wstring& text) {
    auto border = widget.try_as<wuxc::Border>();
    if (!border) {
        return;
    }

    auto content = border.Child().try_as<wuxc::StackPanel>();
    if (!content || content.Children().Size() != 2) {
        return;
    }
    auto textBlock = content.Children().GetAt(0).try_as<wuxc::TextBlock>();
    auto indicator = content.Children().GetAt(1).try_as<wuxc::Border>();
    if (!textBlock || !indicator) {
        return;
    }

    SetTextBlockDashboardText(textBlock, text);
    bool showIndicator = false;
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        showIndicator = ShouldShowLatestUpdateIndicator(g_dashboardVersion,
                                                        g_apiKeyRemainingPercent);
    }
    indicator.Visibility(showIndicator ? wux::Visibility::Visible
                                       : wux::Visibility::Collapsed);
}

void UpdateWidgetsText(const std::wstring& text) {
    std::vector<wux::FrameworkElement> widgets;
    {
        std::lock_guard<std::mutex> lock(g_widgetsMutex);
        DWORD threadId = GetCurrentThreadId();
        for (auto& state : g_widgets) {
            if (state.threadId != threadId) {
                continue;
            }

            auto widget = state.widget.get();
            if (widget) {
                widgets.push_back(widget);
            }
        }
    }

    for (auto& widget : widgets) {
        SetWidgetText(widget, text);
    }
}

void WINAPI UpdateWidgetsTextFromThread(PVOID parameter) {
    auto* text = reinterpret_cast<const std::wstring*>(parameter);
    UpdateWidgetsText(*text);
}

wux::FrameworkElement CreateDashboardWidget(const wux::FrameworkElement& tray) {
    auto text = wuxc::TextBlock();
    text.FontSize(11);
    text.VerticalAlignment(wux::VerticalAlignment::Center);
    text.TextWrapping(wux::TextWrapping::NoWrap);

    const auto indicatorColor = winrt::Windows::UI::Color{0xFF, 0x60, 0xA5, 0xFA};
    auto indicatorText = wuxc::TextBlock();
    indicatorText.Text(L"!");
    indicatorText.FontSize(10);
    indicatorText.Foreground(wuxm::SolidColorBrush(indicatorColor));
    indicatorText.HorizontalAlignment(wux::HorizontalAlignment::Center);
    indicatorText.VerticalAlignment(wux::VerticalAlignment::Center);

    auto indicator = wuxc::Border();
    indicator.Name(L"CodexLbLatestUpdateIndicator");
    indicator.Width(14);
    indicator.Height(14);
    indicator.CornerRadius(wux::CornerRadius{7, 7, 7, 7});
    indicator.BorderThickness(wux::Thickness{1, 1, 1, 1});
    indicator.BorderBrush(wuxm::SolidColorBrush(indicatorColor));
    indicator.Margin(wux::Thickness{6, 0, 0, 0});
    indicator.VerticalAlignment(wux::VerticalAlignment::Center);
    indicator.Child(indicatorText);
    wuxc::ToolTipService::SetToolTip(
        indicator, winrt::box_value(L"Доступна новая версия codex-lb (latest)"));
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        SetTextBlockDashboardText(text, g_dashboardText);
        indicator.Visibility(
            ShouldShowLatestUpdateIndicator(g_dashboardVersion, g_apiKeyRemainingPercent)
                ? wux::Visibility::Visible
                : wux::Visibility::Collapsed);
    }

    auto content = wuxc::StackPanel();
    content.Orientation(wuxc::Orientation::Horizontal);
    content.VerticalAlignment(wux::VerticalAlignment::Center);
    content.Children().Append(text);
    content.Children().Append(indicator);

    auto border = wuxc::Border();
    border.Name(L"CodexLbDashboardWidget");
    border.Background(
        wuxm::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    border.CornerRadius(wux::CornerRadius{16, 16, 16, 16});
    border.Child(content);
    border.Padding(wux::Thickness{10, 0, 10, 0});
    border.MinWidth(0);
    border.Height(36);
    border.HorizontalAlignment(wux::HorizontalAlignment::Right);
    border.VerticalAlignment(wux::VerticalAlignment::Center);
    border.Margin(MakeWidgetMargin(tray.ActualWidth()));
    border.IsHitTestVisible(true);

    return border;
}

void RevokeWidgetEvents(WidgetState& state) {
    state.traySizeChangedRevoker.revoke();
    state.widgetTappedRevoker.revoke();
    state.widgetPointerEnteredRevoker.revoke();
    state.widgetPointerExitedRevoker.revoke();
}

void TrackTaskbarRoot(const wux::FrameworkElement& element) {
    auto xamlRoot = element.XamlRoot();
    if (!xamlRoot) {
        return;
    }

    DWORD threadId = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(g_widgetsMutex);
    for (auto& state : g_taskbarRoots) {
        if (state.threadId == threadId && state.xamlRoot.get() == xamlRoot) {
            return;
        }
    }
    g_taskbarRoots.push_back(TaskbarRootState{threadId, xamlRoot});
}

void EnsureWidgetForTray(const wux::FrameworkElement& element) {
    if (IsNamedElement(element, L"TaskbarFrame")) {
        TrackTaskbarRoot(element);
    }

    auto tray = IsNamedElement(element, L"SystemTrayFrameGrid")
                    ? element
                    : FindAncestorByName(element, L"SystemTrayFrameGrid");
    if (!tray) {
        return;
    }
    TrackTaskbarRoot(tray);

    auto xamlRoot = tray.XamlRoot();
    if (!xamlRoot) {
        return;
    }

    auto root = FindAncestorPanel(tray);
    if (!root) {
        Wh_Log(L"SystemTrayFrameGrid found, no parent panel found");
        return;
    }

    auto widget = FindRootWidget(root);
    WidgetState previousState;
    bool alreadyTracked = false;
    {
        std::lock_guard<std::mutex> lock(g_widgetsMutex);
        for (auto it = g_widgets.begin(); it != g_widgets.end(); ++it) {
            if (it->threadId != GetCurrentThreadId() || it->root.get() != root) {
                continue;
            }
            if (widget && it->widget.get() == widget &&
                it->tray.get() == tray && it->xamlRoot.get() == xamlRoot) {
                alreadyTracked = true;
            } else {
                previousState = std::move(*it);
                g_widgets.erase(it);
                g_widgetCount = static_cast<int>(g_widgets.size());
            }
            break;
        }
    }
    if (alreadyTracked) {
        UpdateWidgetPosition(tray, widget);
        return;
    }
    RevokeWidgetEvents(previousState);

    if (!widget) {
        widget = CreateDashboardWidget(tray);
        root.Children().Append(widget);
    }
    UpdateWidgetPosition(tray, widget);
    wuxc::Canvas::SetZIndex(widget, 1000);

    WidgetState state;
    state.threadId = GetCurrentThreadId();
    state.root = root;
    state.tray = tray;
    state.widget = widget;
    state.xamlRoot = xamlRoot;
    state.traySizeChangedRevoker = tray.SizeChanged(
        winrt::auto_revoke,
        [weakTray = winrt::make_weak(tray),
         weakWidget = winrt::make_weak(widget)](auto&&, auto&&) {
            try {
                UpdateWidgetPosition(weakTray.get(), weakWidget.get());
            } catch (...) {
                LogException(L"TraySizeChanged", CurrentExceptionHResult());
            }
        });
    state.widgetTappedRevoker = widget.Tapped(
        winrt::auto_revoke,
        [](auto&&, wuxi::TappedRoutedEventArgs const& args) {
            try {
                TogglePopupWindow();
                args.Handled(true);
            } catch (...) {
                LogException(L"WidgetTapped", CurrentExceptionHResult());
            }
        });
    state.widgetPointerEnteredRevoker = widget.PointerEntered(
        winrt::auto_revoke,
        [](auto&& sender, wuxi::PointerRoutedEventArgs const&) {
            try {
                auto border = sender.template try_as<wuxc::Border>();
                if (!border) {
                    return;
                }

                border.Background(wuxm::SolidColorBrush(
                    winrt::Windows::UI::Color{0x26, 0xFF, 0xFF, 0xFF}));
            } catch (...) {
                LogException(L"WidgetPointerEntered",
                             CurrentExceptionHResult());
            }
        });
    state.widgetPointerExitedRevoker = widget.PointerExited(
        winrt::auto_revoke,
        [](auto&& sender, wuxi::PointerRoutedEventArgs const&) {
            try {
                auto border = sender.template try_as<wuxc::Border>();
                if (!border) {
                    return;
                }

                border.Background(wuxm::SolidColorBrush(
                    winrt::Windows::UI::Colors::Transparent()));
            } catch (...) {
                LogException(L"WidgetPointerExited",
                             CurrentExceptionHResult());
            }
        });

    {
        std::lock_guard<std::mutex> lock(g_widgetsMutex);
        g_widgets.push_back(std::move(state));
        g_widgetCount = static_cast<int>(g_widgets.size());
    }

    Wh_Log(L"Attached codex-lb dashboard widget to taskbar root, tid=%u",
           GetCurrentThreadId());
}

void RestoreWidgetsForCurrentThread() {
    if (g_unloading || g_reconcilingWidgets) {
        return;
    }
    g_reconcilingWidgets = true;
    struct ReconcileGuard {
        ~ReconcileGuard() {
            g_reconcilingWidgets = false;
        }
    } reconcileGuard;

    DWORD threadId = GetCurrentThreadId();
    std::vector<WidgetState> staleWidgets;
    std::vector<wux::XamlRoot> roots;
    {
        std::lock_guard<std::mutex> lock(g_widgetsMutex);
        for (auto it = g_widgets.begin(); it != g_widgets.end();) {
            if (it->threadId != threadId) {
                ++it;
                continue;
            }
            auto root = it->root.get();
            auto tray = it->tray.get();
            auto widget = it->widget.get();
            auto xamlRoot = it->xamlRoot.get();
            if (root && tray && widget && xamlRoot &&
                tray.XamlRoot() == xamlRoot &&
                FindAncestorPanel(tray) == root &&
                wuxm::VisualTreeHelper::GetParent(widget) == root) {
                ++it;
                continue;
            }
            staleWidgets.push_back(std::move(*it));
            it = g_widgets.erase(it);
        }
        g_widgetCount = static_cast<int>(g_widgets.size());

        for (auto it = g_taskbarRoots.begin(); it != g_taskbarRoots.end();) {
            if (it->threadId != threadId) {
                ++it;
                continue;
            }
            auto root = it->xamlRoot.get();
            if (!root || !root.Content()) {
                it = g_taskbarRoots.erase(it);
                continue;
            }
            roots.push_back(root);
            ++it;
        }
    }

    for (auto& state : staleWidgets) {
        RevokeWidgetEvents(state);
        auto root = state.root.get();
        auto widget = state.widget.get();
        uint32_t index;
        if (root && widget && root.Children().IndexOf(widget, index)) {
            root.Children().RemoveAt(index);
        }
    }

    for (auto& root : roots) {
        std::vector<wux::DependencyObject> pending{root.Content()};
        std::vector<wux::FrameworkElement> trays;
        while (!pending.empty()) {
            auto element = pending.back();
            pending.pop_back();
            auto frameworkElement = element.try_as<wux::FrameworkElement>();
            if (IsNamedElement(frameworkElement, L"SystemTrayFrameGrid")) {
                trays.push_back(frameworkElement);
                continue;
            }
            int childCount = wuxm::VisualTreeHelper::GetChildrenCount(element);
            for (int i = 0; i < childCount; ++i) {
                pending.push_back(wuxm::VisualTreeHelper::GetChild(element, i));
            }
        }
        for (auto& tray : trays) {
            EnsureWidgetForTray(tray);
        }
    }
}

void RemoveWidgets() {
    std::vector<WidgetState> widgetsToRemove;
    {
        std::lock_guard<std::mutex> lock(g_widgetsMutex);
        DWORD threadId = GetCurrentThreadId();
        for (auto it = g_widgets.begin(); it != g_widgets.end();) {
            if (it->threadId != threadId) {
                ++it;
                continue;
            }

            widgetsToRemove.push_back(std::move(*it));
            it = g_widgets.erase(it);
        }

        g_widgetCount = static_cast<int>(g_widgets.size());
        g_taskbarRoots.erase(
            std::remove_if(g_taskbarRoots.begin(), g_taskbarRoots.end(),
                           [threadId](const TaskbarRootState& state) {
                               return state.threadId == threadId;
                           }),
            g_taskbarRoots.end());
    }

    for (auto& state : widgetsToRemove) {
        RevokeWidgetEvents(state);

        auto root = state.root.get();
        auto widget = state.widget.get();
        if (!root || !widget) {
            continue;
        }

        uint32_t index;
        if (root.Children().IndexOf(widget, index)) {
            root.Children().RemoveAt(index);
        }
    }

    {
        std::lock_guard<std::mutex> popupLock(g_popupCreationMutex);
        HWND popupWindow = g_popupWindow.load();
        DWORD popupThreadId = g_popupThreadId.load();
        if (popupWindow &&
            (!popupThreadId || popupThreadId == GetCurrentThreadId())) {
            HidePopupWindow();
            DestroyWindow(popupWindow);
            g_popupWindow.store(nullptr);
            g_popupThreadId = 0;
        }
    }
}

}  // namespace codex_dashboard
