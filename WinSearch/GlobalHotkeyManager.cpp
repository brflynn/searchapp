#include "pch.h"
#include "GlobalHotkeyManager.h"
#include "Logging.h"
#include <unordered_map>

GlobalHotkeyManager* GlobalHotkeyManager::s_instance = nullptr;

GlobalHotkeyManager::GlobalHotkeyManager()
{
    s_instance = this;
}

GlobalHotkeyManager::~GlobalHotkeyManager()
{
    StopMessageLoop();
    s_instance = nullptr;
}

bool GlobalHotkeyManager::RegisterHotkey(int id, UINT modifiers, UINT virtualKey, std::function<void()> callback)
{
    if (m_hwnd == nullptr)
    {
        CreateMessageWindow();
        StartMessageLoop();
    }

    if (::RegisterHotKey(m_hwnd, id, modifiers, virtualKey))
    {
        m_callbacks[id] = callback;
        _debugout(L"Registered hotkey with ID %d\n", id);
        return true;
    }
    else
    {
        DWORD error = GetLastError();
        _debugout(L"Failed to register hotkey with ID %d, error: %d\n", id, error);
        return false;
    }
}

void GlobalHotkeyManager::UnregisterHotkey(int id)
{
    if (m_hwnd != nullptr)
    {
        ::UnregisterHotKey(m_hwnd, id);
        m_callbacks.erase(id);
        _debugout(L"Unregistered hotkey with ID %d\n", id);
    }
}

void GlobalHotkeyManager::StartMessageLoop()
{
    if (!m_messageLoopRunning)
    {
        m_messageLoopRunning = true;
        m_messageLoopThread = CreateThread(nullptr, 0, MessageLoopThreadProc, this, 0, nullptr);
    }
}

void GlobalHotkeyManager::StopMessageLoop()
{
    if (m_messageLoopRunning)
    {
        m_messageLoopRunning = false;
        
        if (m_hwnd != nullptr)
        {
            PostMessage(m_hwnd, WM_QUIT, 0, 0);
        }
        
        if (m_messageLoopThread != nullptr)
        {
            WaitForSingleObject(m_messageLoopThread, 5000); // Wait up to 5 seconds
            CloseHandle(m_messageLoopThread);
            m_messageLoopThread = nullptr;
        }
        
        DestroyMessageWindow();
    }
}

void GlobalHotkeyManager::CreateMessageWindow()
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HotkeyWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"WinSearchHotkeyWindow";
    
    RegisterClassEx(&wc);
    
    m_hwnd = CreateWindowEx(
        0,
        L"WinSearchHotkeyWindow",
        L"WinSearch Hotkey Window",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );
}

void GlobalHotkeyManager::DestroyMessageWindow()
{
    if (m_hwnd != nullptr)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        UnregisterClass(L"WinSearchHotkeyWindow", GetModuleHandle(nullptr));
    }
}

DWORD WINAPI GlobalHotkeyManager::MessageLoopThreadProc(LPVOID param)
{
    GlobalHotkeyManager* manager = static_cast<GlobalHotkeyManager*>(param);
    
    MSG msg;
    while (manager->m_messageLoopRunning && GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}

LRESULT CALLBACK GlobalHotkeyManager::HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY && s_instance != nullptr)
    {
        int hotkeyId = static_cast<int>(wParam);
        auto it = s_instance->m_callbacks.find(hotkeyId);
        if (it != s_instance->m_callbacks.end())
        {
            _debugout(L"Hotkey %d activated\n", hotkeyId);
            it->second(); // Call the callback
        }
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}