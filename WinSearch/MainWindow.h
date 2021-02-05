#pragma once

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime

#include "MainWindow.g.h"
#include "SearchQueryHelper.h"

#pragma pop_macro("GetCurrentTime")

namespace winrt::WinSearch::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {

    public:
        MainWindow();
        void SearchTextChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void MainWindowSizeChanged(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::WindowSizeChangedEventArgs const& handler);
        void ContentSearch_Clicked(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void EmailSearch_Clicked(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void WindowsSearchSettings_Clicked(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void SearchResults_ItemClicked(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);

    private:
        void OnQueryCompleted();
        winrt::Windows::Foundation::IAsyncAction ExecuteAsync(PCWSTR searchText);
        winrt::Windows::Foundation::IAsyncAction LaunchItemAsync(winrt::WinSearch::SearchResult const& result);
        winrt::Windows::Foundation::IAsyncAction GetImageForResult(winrt::WinSearch::SearchResult const& result);
        bool CanReuseQuery(PCWSTR newSearchText, PCWSTR currentSearchText);
        winrt::com_ptr<ISearchQuery> m_searchQueryHelper;
        wil::srwlock m_lock;
        DWORD m_currentQueryCookie = 10;
        void UpdateContent();
        void UpdateResults();
        bool m_contentSearchEnabled = false;
        bool m_mailSearchEnabled = false;
        winrt::Windows::Foundation::Collections::IVector<IInspectable> m_searchResults;
    };
}

namespace winrt::WinSearch::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
