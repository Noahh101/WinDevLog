@echo off
setlocal
pushd "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ was not found. Install MSYS2 UCRT64 and add its bin folder to PATH.
    popd
    exit /b 1
)

if not exist "build\Release" mkdir "build\Release"

g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -I imgui -I imgui\backends ^
    main.cpp ^
    imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp ^
    imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp ^
    -ld3dcompiler -ld3d11 -ldxgi -ldwmapi -lsetupapi -luuid ^
    -mwindows -municode -o build\Release\WinDeviceLog.exe

if errorlevel 1 (
    popd
    exit /b 1
)

echo Built build\Release\WinDeviceLog.exe
popd
