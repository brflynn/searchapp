#include "pch.h"
#include "MainWindow.h"
#include "MainWindow.g.cpp"
#include "Logging.h"
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.h>
#include <Windows.h>
#include <microsoft.ui.xaml.window.h>


using namespace winrt;
using namespace winrt::Windows::Foundation;

#include <SearchResultHelpers.h>
SearchResultImageUriManager g_imageUriManager;

namespace winrt::WinSearch::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        UpdateContent();
        m_searchResults = winrt::single_threaded_observable_vector<IInspectable>();
        CacheSearchSettingState();
        ExecuteAsync(L"");
    }

    void MainWindow::ContentSearch_Clicked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_contentSearchEnabled = ContentSearchOption().IsChecked();
        {
            auto lock = m_lock.lock_exclusive();
            m_searchQueryHelper = nullptr;
        }
        SearchTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        ExecuteAsync(SearchTextBox().Text().c_str());
    }

    void MainWindow::EmailSearch_Clicked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_mailSearchEnabled = EmailSearchOption().IsChecked();
        {
            auto lock = m_lock.lock_exclusive();
            m_searchQueryHelper = nullptr;
        }
        SearchTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        ExecuteAsync(SearchTextBox().Text().c_str());
    }

    void MainWindow::AllUsersSearch_Clicked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        m_allUsersSearchEnabled = AllUsersSearchOption().IsChecked();
        {
            auto lock = m_lock.lock_exclusive();
            m_searchQueryHelper = nullptr;
        }
        SearchTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        ExecuteAsync(SearchTextBox().Text().c_str());
    }

    void MainWindow::SearchTextChanged(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        try
        {
            // Just send the search query helper...it will do the work asynchronously
            PCWSTR text = SearchTextBox().Text().c_str();
            
            // Hide results container while searching
            if (wcslen(text) == 0)
            {
                ResultsContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                QuickActions().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            
            ExecuteAsync(text);
        }
        CATCH_LOG();
    }

    void MainWindow::WindowsSearchSettings_Clicked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        winrt::Windows::Foundation::Uri uri{ L"ms-settings:cortana-windowssearch" };
        winrt::Windows::System::Launcher::LaunchUriAsync(uri);
    }

    void MainWindow::MainWindowSizeChanged(IInspectable const&, Microsoft::UI::Xaml::WindowSizeChangedEventArgs const&)
    {
        UpdateContent();
    }

    void MainWindow::SearchResults_ItemClicked(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        Windows::Foundation::IInspectable item = args.ClickedItem();
        winrt::WinSearch::SearchResult result = item.as<winrt::WinSearch::SearchResult>();

        LaunchItemAsync(result);
    }

    IAsyncAction MainWindow::GeneratePropertyAnalysisAsync()
    {
        // Property analysis functionality - simplified for overlay
        co_return;
    }

    IAsyncAction MainWindow::LaunchItemAsync(winrt::WinSearch::SearchResult const& result)
    {
        co_await result.LaunchAsync();
    }

    void MainWindow::UpdateContent()
    {
        Rect bounds = this->Bounds();
        SearchTextBox().Width(bounds.Height);
        SearchResults().Width(bounds.Height);
        SearchResults().Height(bounds.Width);
        
        std::wstring myString = L"Window Width ";
        myString += std::to_wstring(bounds.Width);
        myString += L" Text box width ";
        myString += std::to_wstring(SearchTextBox().Width());
        //DebugTextBlock().Text(winrt::to_hstring(myString.c_str()));
    }

    bool MainWindow::CanReuseQuery(PCWSTR currentSearchText, PCWSTR newSearchText)
    {
        try
        {
            const size_t newLen = wcslen(newSearchText);
            const size_t currentLen = wcslen(currentSearchText);
            if (!newLen || (currentLen > newLen))
            {
                return false;
            }
            else if (!wcslen(currentSearchText))
            {
                // Old search text of L"" is always a prefix of everything...this just means we've got an object that has been primed
                // and is waiting for characters...
                return true;
            }

            std::wstring currentString(currentSearchText);
            std::wstring newString(newSearchText);

            std::transform(currentString.begin(), currentString.end(), currentString.begin(), ::towlower);
            std::transform(newString.begin(), newString.end(), newString.begin(), ::towlower);

            auto res = std::mismatch(currentString.begin(), currentString.end(), newString.begin());

            if (res.first == currentString.end())
            {
                // currentString is a prefix of newString...we can reuse this query
                return true;
            }
        }
        CATCH_LOG();

        return false;
    }

    void MainWindow::CacheSearchSettingState()
    {
        m_allUsersSearchEnabled = AllUsersSearchOption().IsChecked() && AllUsersSearchOption().IsEnabled();
        m_contentSearchEnabled = ContentSearchOption().IsChecked() && ContentSearchOption().IsEnabled();
        m_mailSearchEnabled = EmailSearchOption().IsChecked() && EmailSearchOption().IsEnabled();
    }

    IAsyncAction MainWindow::ExecuteAsync(PCWSTR searchText)
    {
        // Queries will come in as soon as the user begins typing...so we want to do a few things here to make sure we don't block
        // the ui while the queries are executing
        // 1) Capture caller context
        winrt::apartment_context ui_thread;

        {
            auto lock = m_lock.lock_exclusive();
            CacheSearchSettingState();
            m_currentQueryCookie++;
        }
        // 2) Execute the query on a background thread
        co_await winrt::resume_background();

        {
            auto lock = m_lock.lock_exclusive();
            if ((m_searchQueryHelper != nullptr) && !CanReuseQuery(m_searchQueryHelper->GetQueryString(), searchText))
            {
                m_searchQueryHelper.as<ISearchUXQuery>()->CancelOutstandingQueries();
                m_searchQueryHelper = nullptr;
            }

            if (m_searchQueryHelper == nullptr)
            {
                // Create a new one, give it a new cookie, and put it into our map
                m_searchQueryHelper = CreateSearchQueryHelper();
                m_searchQueryHelper.as<ISearchUXQuery>()->Init(m_contentSearchEnabled, m_mailSearchEnabled, m_allUsersSearchEnabled);
            }

            // Just forward on to the helper with the right callback for feeding us results
            // Set up the binding for the items
            m_searchQueryHelper.as<ISearchUXQuery>()->Execute(searchText, m_currentQueryCookie);

        }
        
        // Wait for the query executed event
        m_searchQueryHelper.as<ISearchUXQuery>()->WaitForQueryCompletedEvent();

        // 3) Switch back to the calling thread to update the UI
        co_await ui_thread;
        _debugout(L"UI thread OnQueryCompleted Cookie: %d\n", m_currentQueryCookie);
        size_t searchTextLen = wcslen(searchText);
        _debugout(L"SearchTextLength: %d", searchTextLen);
        if (searchTextLen > 0)
        {
            OnQueryCompleted();
        }
        else
        {
            // Just clear all the results from the UI
            SearchResults().ItemsSource(nullptr);
        }
    }

    void MainWindow::OnQueryCompleted()
    {
        auto lock = m_lock.lock_exclusive();

        if (m_searchQueryHelper != nullptr) // race between drawing and selecting options...always check validity.
        {
            // Get the results from the query helper and stash in the UI
            DWORD cookie = m_searchQueryHelper.as<ISearchUXQuery>()->GetCookie();
            if (cookie == m_currentQueryCookie)
            {
                // If we are here, we are returning results on the same user input
                DWORD numResults = m_searchQueryHelper->GetNumResults();
                m_searchResults = winrt::single_threaded_observable_vector<IInspectable>();

                for (DWORD i = 0; i < numResults; ++i)
                {
                    WinSearch::SearchResult result = m_searchQueryHelper.as<ISearchUXQuery>()->GetResult(i);
                    m_searchResults.Append(result);
                }
                
                SearchResults().ItemsSource(m_searchResults);
                
                // Show results container if we have results, hide if empty
                if (numResults > 0)
                {
                    ResultsContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                    QuickActions().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                }
                else
                {
                    ResultsContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                    QuickActions().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
                }
            }
        }
    }

    void MainWindow::FocusSearchBox()
    {
        SearchTextBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        SearchTextBox().SelectAll();
    }

    void MainWindow::ShowWindow()
    {
        if (!m_isVisible)
        {
            // Make window full screen and borderless
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative)
            {
                HWND hwnd;
                windowNative->get_WindowHandle(&hwnd);
                if (hwnd)
                {
                    // Get screen dimensions
                    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                    
                    // Set window to full screen, borderless, and topmost
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, screenWidth, screenHeight, 
                        SWP_SHOWWINDOW | SWP_FRAMECHANGED);
                    
                    // Remove window decorations for true overlay effect
                    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
                    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
                    SetWindowLongPtr(hwnd, GWL_STYLE, style);
                    
                    SetForegroundWindow(hwnd);
                }
            }
            
            this->Activate();
            m_isVisible = true;
            
            // Hide results initially
            ResultsContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            QuickActions().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            
            FocusSearchBox();
        }
    }

    void MainWindow::HideWindow()
    {
        if (m_isVisible)
        {
            // Use Win32 API to hide the window
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative)
            {
                HWND hwnd;
                windowNative->get_WindowHandle(&hwnd);
                if (hwnd)
                {
                    ::ShowWindow(hwnd, SW_HIDE);
                }
            }
            
            m_isVisible = false;
            
            // Clear search text and hide results when hiding
            SearchTextBox().Text(L"");
            SearchResults().ItemsSource(nullptr);
            ResultsContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            QuickActions().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }
}
