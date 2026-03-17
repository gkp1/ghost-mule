@echo off
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

if not exist "C:\WinDivert-2.2.2-A" (
    echo WinDivert not found at C:\WinDivert-2.2.2-A
    exit /b 1
)

if not exist "output" mkdir output

cl /nologo /O2 /Ot /GL /Gy /W4 /wd4100 /wd4189 /wd4267 /wd4244 /wd4996 ^
    /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DPROXYBRIDGE_EXPORTS /DNDEBUG ^
    /arch:SSE2 /fp:fast /GS /guard:cf ^
    /I"C:\WinDivert-2.2.2-A\include" ^
    src\ProxyBridge.c ^
    /LD ^
    /link /LTCG /OPT:REF /OPT:ICF /RELEASE /DYNAMICBASE /NXCOMPAT ^
    /LIBPATH:"C:\WinDivert-2.2.2-A\x64" ^
    WinDivert.lib ws2_32.lib iphlpapi.lib ^
    /Fe:output\ProxyBridgeCore.dll

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    move output\ProxyBridgeCore.dll output\ProxyBridgeCore.dll.bak 2>nul
    del output\ProxyBridgeCore.dll.bak 2>nul
    move ProxyBridgeCore.dll output\ 2>nul
    echo DLL written to output\ProxyBridgeCore.dll
) else (
    echo.
    echo Compilation failed!
    exit /b 1
)
