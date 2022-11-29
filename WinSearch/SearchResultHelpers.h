#pragma once

#include "pch.h"
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

inline bool IsMailItem(PCWSTR url)
{
    std::wstring urlStr(url);
    size_t indexProtocolFound = urlStr.find(L"mapi");
    return (indexProtocolFound != std::wstring::npos);
}

inline void ConvertUrlToFilePath(std::wstring& url, bool* converted)
{
    *converted = false;
    if (IsMailItem(url.c_str()))
    {
        return;
    }

    std::replace(url.begin(), url.end(), L'/', L'\\'); // replace all '/' to '\\'

    std::wstring fileProtocolString;

    fileProtocolString = L"file:";

    size_t indexProtocolFound = url.find(fileProtocolString.c_str());

    if ((indexProtocolFound != std::wstring::npos)
        && ((indexProtocolFound + fileProtocolString.length()) < url.size()))
    {
        url = url.substr(indexProtocolFound + fileProtocolString.length());
        *converted = true;
    }
}

struct SearchResultImageUriManager
{
public:
    SearchResultImageUriManager() { }

    bool NeedProcessThumbnailForItem(PCWSTR itemPath, bool isFolder)
    {
        auto lock = m_cs.lock();
        if (isFolder && (m_folderThumbnail == nullptr))
        {
            return true;
        }
        else if (!isFolder)
        {
            std::wstring filePath(itemPath);
            std::wstring ext = filePath.substr(filePath.rfind('.'));
            if (ext.c_str() != nullptr && ext.c_str()[0] != L'\0')
            {
                auto it = m_extThumbnailPathMapping.find(ext);
                if (it == m_extThumbnailPathMapping.end())
                {
                    return true;
                }
            }
        }

        return false;
    }

    void AddThumbnailForFolder(winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumbnail)
    {
        auto lock = m_cs.lock();
        m_folderThumbnail = thumbnail;
    }

    void AddThumbnailForStorageFile(winrt::Windows::Storage::FileProperties::StorageItemThumbnail thumbnail, PCWSTR itemPath)
    {
        auto lock = m_cs.lock();
        std::wstring filePath(itemPath);
        std::wstring ext = filePath.substr(filePath.rfind('.'));
        if (ext.c_str() != nullptr && ext.c_str()[0] != L'\0')
        {
            m_extThumbnailPathMapping.emplace(ext, thumbnail);
        }
    }

    winrt::Windows::Storage::FileProperties::StorageItemThumbnail GetThumbnailForItem(PCWSTR itemPath, bool isFolder, bool isMail)
    {
        auto lock = m_cs.lock();
        if (isFolder)
        {
            return m_folderThumbnail;
        }
        else if (!isMail)
        {
            std::wstring filePath(itemPath);
            std::wstring ext = filePath.substr(filePath.rfind('.'));
            if (ext.c_str() != nullptr && ext.c_str()[0] != L'\0')
            {
                auto it = m_extThumbnailPathMapping.find(ext);
                if (it != m_extThumbnailPathMapping.end())
                {
                    return it->second;
                }
            }
        }

        return nullptr;
    }

private:
    std::unordered_map<std::wstring, winrt::Windows::Storage::FileProperties::StorageItemThumbnail> m_extThumbnailPathMapping;
    std::wstring m_mailImageUriPath;
    winrt::Windows::Storage::FileProperties::StorageItemThumbnail m_folderThumbnail = nullptr;

    wil::critical_section m_cs;
};