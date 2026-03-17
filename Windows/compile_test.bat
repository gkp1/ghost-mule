@echo off
cd /d C:\Users\gianp\Code\ProxyBridge\Windows
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo vcvarsall failed
    exit /b 1
)
echo Environment set up, starting compile...
if not exist output mkdir output
cl /nologo /O2 /D_WIN32_WINNT=0x0601 /DPROXYBRIDGE_EXPORTS /I"C:\WinDivert-2.2.2-A\include" /LD /Fe:output\ProxyBridgeCore.dll src\ProxyBridge.c /link /LIBPATH:"C:\WinDivert-2.2.2-A\x64" WinDivert.lib ws2_32.lib iphlpapi.lib
echo Exit code: %ERRORLEVEL%
pause
