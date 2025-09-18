#include "pch.h"

#include "App.h"
#include "MainWindow.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Navigation;
using namespace WinSearch;
using namespace WinSearch::implementation;

/// <summary>
/// Initializes the singleton application object.  This is the first line of authored code
/// executed, and as such is the logical equivalent of main() or WinMain().
/// </summary>
App::App()
{
    InitializeComponent();
    Suspending({ this, &App::OnSuspending });

    // Initialize hotkey manager
    m_hotkeyManager = std::make_unique<GlobalHotkeyManager>();

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
    UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& e)
    {
        if (IsDebuggerPresent())
        {
            auto errorMessage = e.Message();
            __debugbreak();
        }
    });
#endif
}

/// <summary>
/// Invoked when the application is launched normally by the end user.  Other entry points
/// will be used such as when the application is launched to open a specific file.
/// </summary>
/// <param name="e">Details about the launch request and process.</param>
void App::OnLaunched(LaunchActivatedEventArgs const&)
{
    window = make<MainWindow>();
    
    // Create the window but don't show it initially
    // This ensures the app runs in the background until the hotkey is pressed
    
    // Register Ctrl+Shift+F hotkey
    m_hotkeyManager->RegisterHotkey(
        HOTKEY_SEARCH_OVERLAY,
        SEARCH_HOTKEY_MODIFIERS,
        SEARCH_HOTKEY_KEY,
        [this]() { OnSearchOverlayHotkey(); }
    );
    
    // Initialize the window as hidden
    if (window)
    {
        auto mainWindow = window.as<winrt::WinSearch::implementation::MainWindow>();
        // Don't call ShowWindow() or Activate() here - keep it hidden
    }
}

/// <summary>
/// Invoked when application execution is being suspended.  Application state is saved
/// without knowing whether the application will be terminated or resumed with the contents
/// of memory still intact.
/// </summary>
/// <param name="sender">The source of the suspend request.</param>
/// <param name="e">Details about the suspend request.</param>
void App::OnSuspending([[maybe_unused]] IInspectable const& sender, [[maybe_unused]] Windows::ApplicationModel::SuspendingEventArgs const& e)
{
    // Save application state and stop any background activity
    if (m_hotkeyManager)
    {
        m_hotkeyManager->UnregisterHotkey(HOTKEY_SEARCH_OVERLAY);
    }
}

void App::OnSearchOverlayHotkey()
{
    // Toggle window visibility when hotkey is pressed
    if (window)
    {
        auto mainWindow = window.as<winrt::WinSearch::implementation::MainWindow>();
        if (mainWindow->IsWindowVisible())
        {
            mainWindow->HideWindow();
        }
        else
        {
            mainWindow->ShowWindow();
        }
    }
}