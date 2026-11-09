# Win Device Log

I made WinDeviceLog to solve a small problem I had myself: I wanted a quick,
clear way to see when USB devices connect to or disconnect from my PC. 

![screenshot](screenshots/win-device-log.png)

## Download

Download `WinDeviceLog.exe` from the
[latest GitHub Release](https://github.com/Noahh101/WinDevLog/releases/latest)
and open it. No build tools are needed. New releases are built and published
automatically from version tags.

## Build from source (optional)

Install MSYS2 UCRT64 with `g++` and add its `bin` folder to `PATH`, then
double-click `build.bat` or run it from the project folder. The executable is
created at `build\Release\WinDeviceLog.exe`. The USB-themed `app.ico` is embedded
in the executable and used for its taskbar and system tray icons.

The application entry point is `main.cpp`; its supporting source and header
files are in `src/`. ImGui and the required Win32/DX11 backends are included in
`imgui/`, so no internet connection is needed.
Device names use the USB-reported product name when Windows provides one, with a
peripheral-type fallback when it does not.

Minimize the window to keep Win Device Log running in the system tray. Double-click
its tray icon or choose **Open Win Device Log** from the right-click menu to restore
it; choose **Exit** to close the application.
