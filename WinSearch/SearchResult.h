#pragma once
#include "SearchResult.g.h"

#include <SearchResultHelpers.h>

extern SearchResultImageUriManager g_imageUriManager;

namespace winrt::WinSearch::implementation
{
    struct SearchResult : SearchResultT<SearchResult>
    {
        SearchResult() = default;
        SearchResult(PCWSTR itemDisplayName, PCWSTR itemUrl, PCWSTR filePath, bool isMail, bool isFolder)
        {
            m_itemDisplayName = itemDisplayName;
            m_itemUrl = itemUrl;
            m_isMail = isMail;
            m_isFolder = isFolder;

            if (m_launchUri.empty())
            {
                // We need to figure this out now so when we render all computations are ready
                if (m_isMail)
                {
                    // Launch via chooser that binds the URI for all future runs
                    m_launchUri = m_itemUrl;
                }
                else
                {
                    // Launch the file with the default app, so use the file path
                    m_launchUri = filePath;
                    TryGetThumbnailForStorageItem();
                }
            }
        }

        hstring ItemDisplayName();
        void ItemDisplayName(hstring const& value);
        hstring ItemUrl();
        void ItemUrl(hstring const& value);
        hstring LaunchUri();
        void LaunchUri(hstring const& value);
        winrt::Windows::Foundation::IAsyncAction LaunchAsync();
        bool IsFolder();
        bool IsMail();
        bool CanDisplay();
        winrt::Windows::Storage::FileProperties::StorageItemThumbnail Thumbnail();
        void Thumbnail(winrt::Windows::Storage::FileProperties::StorageItemThumbnail const& value);
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage ItemImage();
        void ItemImage(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const& value);
    private:
        void TryGetThumbnailForStorageItem();
        hstring m_imageUriPath;
        hstring m_itemDisplayName;
        hstring m_itemUrl;
        hstring m_launchUri;
        bool m_isMail = false;
        bool m_isFolder = false;
        bool m_canDisplay{true};
        winrt::Windows::Storage::FileProperties::StorageItemThumbnail m_thumbnail = nullptr;
    };
}
