#include "pch.h"
#include "SearchResult.h"
#include "SearchResult.g.cpp"
#include "shellapi.h"

#include <winrt/Windows.Storage.Streams.h>

namespace winrt::WinSearch::implementation
{
    hstring SearchResult::ItemDisplayName()
    {
        return m_itemDisplayName;
    }
    void SearchResult::ItemDisplayName(hstring const&)
    {
        throw hresult_not_implemented();
    }
    hstring SearchResult::ItemUrl()
    {
        return m_itemUrl;
    }
    void SearchResult::ItemUrl(hstring const&)
    {
        throw hresult_not_implemented();
    }
    hstring SearchResult::LaunchUri()
    {
        return m_launchUri;
    }
    void SearchResult::LaunchUri(hstring const&)
    {
        throw hresult_not_implemented();
    }

    bool SearchResult::IsFolder()
    {
        return m_isFolder;
    }

    bool SearchResult::IsMail()
    {
        return m_isMail;
    }

    winrt::Windows::Foundation::IAsyncAction SearchResult::LaunchAsync()
    {
        co_await winrt::resume_background(); // do this on a bg thread
        if (!IsMail())
        {
            if (!IsFolder())
            {
                winrt::Windows::Storage::StorageFile file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(m_launchUri.c_str()).get();
                winrt::Windows::System::Launcher::LaunchFileAsync(file).get();
            }
            else
            {
                winrt::Windows::Storage::StorageFolder folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(m_launchUri.c_str()).get();
                winrt::Windows::System::Launcher::LaunchFolderAsync(folder).get();
            }
        }
        else
        {
            winrt::Windows::Foundation::Uri uri{ m_launchUri.c_str() };
            try
            {
                if (winrt::Windows::System::Launcher::LaunchUriAsync(uri).get())
                {

                }
                else
                {
                    // Try again with a popup to choose the app
                    winrt::Windows::System::LauncherOptions options;
                    options.DisplayApplicationPicker(true);

                    winrt::Windows::System::Launcher::LaunchUriAsync(uri, options).get();
                }
            }
            catch (winrt::hresult_error const& ex)
            {
            }
        }
    }

    void SearchResult::TryGetThumbnailForStorageItem()
    {
        if (g_imageUriManager.NeedProcessThumbnailForItem(m_launchUri.c_str(), IsFolder()))
        {
            if (IsFolder())
            {
                try
                {
                    // Create the storage folder from the item
                    winrt::Windows::Storage::StorageFolder folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(m_launchUri.c_str()).get();

                    // Get the thumbnail
                    winrt::Windows::Storage::FileProperties::ThumbnailMode mode = winrt::Windows::Storage::FileProperties::ThumbnailMode::ListView;
                    winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumb = folder.GetThumbnailAsync(mode, 25).get();

                    m_thumbnail = thumb;
                    g_imageUriManager.AddThumbnailForFolder(thumb);
                }
                catch (winrt::hresult_error const& ex)
                {
                    m_thumbnail = nullptr;
                }
            }
            else
            {
                try
                {
                    // Create the storage folder from the item
                    winrt::Windows::Storage::StorageFile folder = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(m_launchUri.c_str()).get();

                    // Get the thumbnail
                    winrt::Windows::Storage::FileProperties::ThumbnailMode mode = winrt::Windows::Storage::FileProperties::ThumbnailMode::ListView;
                    winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumb = folder.GetThumbnailAsync(mode, 25).get();

                    m_thumbnail = thumb;
                    g_imageUriManager.AddThumbnailForStorageFile(thumb, m_launchUri.c_str());
                }
                catch (winrt::hresult_error const& ex)
                {
                    m_thumbnail = nullptr;
                }
            }
        }
        else
        {
            m_thumbnail = g_imageUriManager.GetThumbnailForItem(m_launchUri.c_str(), IsFolder(), IsMail());
        }
    }

    winrt::Windows::Storage::FileProperties::StorageItemThumbnail SearchResult::Thumbnail()
    {
        return m_thumbnail;
    }

    void SearchResult::Thumbnail(winrt::Windows::Storage::FileProperties::StorageItemThumbnail const& thumbnail)
    {
        m_thumbnail = thumbnail;
    }


    winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage SearchResult::ItemImage()
    {
        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmapImage{};
        if (m_thumbnail != nullptr)
        {
            bitmapImage.SetSource(m_thumbnail.CloneStream());
        }
        
        return bitmapImage;
    }

    void SearchResult::ItemImage(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage const&)
    {
        throw hresult_not_implemented();
    }
}
