param(
    [string]$InstallationPath = (Split-Path -Parent $MyInvocation.MyCommand.Path),
    [switch]$SkipShellIntegration
)

$ErrorActionPreference = 'Stop'
$installationPath = [System.IO.Path]::GetFullPath($InstallationPath)
$installedExecutable = Join-Path $installationPath 'planetary_guard.exe'
Get-Process -Name 'planetary_guard' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $installedExecutable } | Stop-Process -Force -ErrorAction SilentlyContinue

if (-not $SkipShellIntegration) {
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Planetary Guard.lnk'
    $uninstallShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\卸载 Planetary Guard.lnk'
    foreach ($shortcut in @($startMenu, $uninstallShortcut)) {
        if (Test-Path -LiteralPath $shortcut) {
            Remove-Item -LiteralPath $shortcut -Force
        }
    }
    Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PlanetaryGuard' -Recurse -Force -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath $installationPath) {
    # 开始菜单快捷方式把工作目录设在安装目录；先切出该目录，Windows
    # 才允许同步删除整个目录。PowerShell 脚本已经读入内存，可以删除自身。
    Set-Location ([System.IO.Path]::GetTempPath())
    # 仅删除安装器拥有的文件，保留用户自行放入目录的其他内容。
    foreach ($name in @('planetary_guard.exe','README.md','LICENSE','uninstall.ps1')) {
        $ownedFile = Join-Path $installationPath $name
        if (Test-Path -LiteralPath $ownedFile -PathType Leaf) { Remove-Item -LiteralPath $ownedFile -Force }
    }
    if (@(Get-ChildItem -LiteralPath $installationPath -Force).Count -eq 0) {
        Remove-Item -LiteralPath $installationPath
    }
}

Write-Host 'Planetary Guard uninstalled. User configuration under %LOCALAPPDATA%\PlanetaryGuard was preserved.'
