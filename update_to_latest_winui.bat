@echo off
echo Updating WinSearch to latest WinUI 3...
echo.

REM Clean the solution first
echo Cleaning solution...
msbuild WinSearch.sln -t:Clean
if %ERRORLEVEL% neq 0 (
    echo Failed to clean solution
    exit /b 1
)

REM Update packages manually - you may need to do this through Visual Studio Package Manager
echo.
echo Manual steps required:
echo 1. Open Visual Studio
echo 2. Go to Tools ^> NuGet Package Manager ^> Manage NuGet Packages for Solution
echo 3. Go to the Updates tab
echo 4. Update the following packages:
echo    - Microsoft.WinUI to version 3.0.9 or latest
echo    - Microsoft.Windows.CppWinRT to latest
echo    - Microsoft.Windows.ImplementationLibrary to latest
echo.
echo After updating packages, run this script again to build.
echo.

REM Try to build
echo Building solution...
msbuild WinSearch.sln
if %ERRORLEVEL% neq 0 (
    echo Build failed. You may need to update packages manually in Visual Studio.
    echo See instructions above.
    exit /b 1
)

echo.
echo Build successful!
echo.
echo Your search app now has the following improvements:
echo - Hotkey changed to Ctrl+Shift+F (was Alt+Spacebar)
echo - Better window visibility management
echo - App starts hidden and only shows when hotkey is pressed
echo - Improved foreground window handling
echo.
echo To use:
echo 1. Run the app (it will start hidden)
echo 2. Press Ctrl+Shift+F anywhere to show the search window
echo 3. Press Ctrl+Shift+F again (or click Hide button) to hide it
echo.