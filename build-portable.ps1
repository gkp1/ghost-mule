param(
    [Parameter(Mandatory=$false)]
    [string]$Version = "3.2.2"
)

$ErrorActionPreference = "Stop"

# Build both projects properly as self-contained
Write-Host "Building GUI (self-contained)..." -ForegroundColor Green
Push-Location Windows\gui
dotnet publish -c Release -r win-x64 --self-contained /p:PublishSingleFile=false -o ..\..\releases\temp
dotnet publish -c Release -r win-x64 --self-contained /p:PublishSingleFile=false -o bin\Release\net10.0-windows\publish
Pop-Location

Write-Host "Building CLI (self-contained)..." -ForegroundColor Green
Push-Location Windows\cli
dotnet publish -c Release -r win-x64 --self-contained /p:PublishSingleFile=true -o ..\..\releases\temp
dotnet publish -c Release -r win-x64 --self-contained /p:PublishSingleFile=true -o bin\Release\net10.0-windows\publish
Pop-Location

# Copy WinDivert
Write-Host "Copying WinDivert..." -ForegroundColor Green
$WinDivertPath = "C:\WinDivert-2.2.2-A\x64"
if (Test-Path $WinDivertPath) {
    Copy-Item "$WinDivertPath\WinDivert.dll" releases\temp -ErrorAction SilentlyContinue
    Copy-Item "$WinDivertPath\WinDivert64.sys" releases\temp -ErrorAction SilentlyContinue
    Copy-Item "$WinDivertPath\WinDivert32.sys" releases\temp -ErrorAction SilentlyContinue
}

# Create README
$Readme = @"
ProxyBridge Portable v$Version
================================

No installation required. Extract and run as Administrator.

Files:
- ProxyBridge.exe      - GUI application
- ProxyBridge_CLI.exe  - Command line interface
- WinDivert*.dll/sys   - Packet driver (required)

Usage:
1. Extract all files to a folder
2. Right-click ProxyBridge.exe -> Run as Administrator
3. Configure proxy in the Proxy menu

CLI Example:
  ProxyBridge_CLI.exe --proxy socks5://127.0.0.1:1080 --rule "chrome.exe:*:*:TCP:PROXY"

Full docs: https://github.com/InterceptSuite/ProxyBridge
"@

$Readme | Out-File -FilePath releases\temp\README.txt -Encoding UTF8

# Create zip
$ZipName = "ProxyBridge-Windows-Portable-v$Version.zip"
if (Test-Path releases\$ZipName) {
    Remove-Item releases\$ZipName -Force
}

Write-Host "Creating zip..." -ForegroundColor Green
Compress-Archive -Path releases\temp\* -DestinationPath releases\$ZipName

# Cleanup
Remove-Item releases\temp -Recurse -Force

$size = [math]::Round((Get-Item releases\$ZipName).Length / 1MB, 2)
Write-Host "`nSuccess!" -ForegroundColor Green
Write-Host "  File: releases\$ZipName" -ForegroundColor Cyan
Write-Host "  Size: $size MB" -ForegroundColor Cyan
