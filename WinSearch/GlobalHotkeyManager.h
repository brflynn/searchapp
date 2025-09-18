#pragma once

#include <Windows.h>
#include <functional>

class GlobalHotkeyManager
{
public:
    GlobalHotkeyManager();
    ~GlobalHotkeyManager();

    bool RegisterHotkey(int id, UINT modifiers, UINT virtualKey, std::function<void()> callback);
    void UnregisterHotkey(int id);
    void StartMessageLoop();
    void StopMessageLoop();

private:
    static LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static GlobalHotkeyManager* s_instance;
    
    HWND m_hwnd = nullptr;
    bool m_messageLoopRunning = false;
    std::unordered_map<int, std::function<void()>> m_callbacks;
    HANDLE m_messageLoopThread = nullptr;
    
    void CreateMessageWindow();
    void DestroyMessageWindow();
    static DWORD WINAPI MessageLoopThreadProc(LPVOID param);
};

// Hotkey IDs
constexpr int HOTKEY_SEARCH_OVERLAY = 1;

// Common hotkey combinations that are usually available
constexpr UINT SEARCH_HOTKEY_MODIFIERS = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
constexpr UINT SEARCH_HOTKEY_KEY = 'F'; // Ctrl+Shift+F