#pragma once

inline bool _debugout(TCHAR* format, ...)
{
    TCHAR buffer[1000];

    va_list argptr;
    va_start(argptr, format);
    wvsprintf(buffer, format, argptr);
    va_end(argptr);

    OutputDebugString(buffer);

    return true;
}

inline bool _tracelog(TCHAR* format, ...)
{
    TCHAR buffer[10000];

    va_list argptr;
    va_start(argptr, format);
    wvsprintf(buffer, format, argptr);
    va_end(argptr);

    // load the log file and write to it...
    winrt::Windows::Storage::StorageFolder storageFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
    static winrt::Windows::Storage::StorageFile storageFile = storageFolder.CreateFileAsync(L"LogTrace.txt", 
        winrt::Windows::Storage::CreationCollisionOption::OpenIfExists).get();
    winrt::Windows::Storage::FileIO::AppendTextAsync(storageFile, buffer).get();

    return true;
}
