param(
    [string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\PlanetaryGuard'),
    [string]$Version = '0.1.0',
    [switch]$SkipShellIntegration
)

$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$destinationPath = [System.IO.Path]::GetFullPath($Destination)

New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'planetary_guard.exe') -Destination $destinationPath -Force
Copy-Item -LiteralPath (Join-Path $source 'README.md') -Destination $destinationPath -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination $destinationPath -Force
Copy-Item -LiteralPath (Join-Path $source 'uninstall.ps1') -Destination $destinationPath -Force

if (-not $SkipShellIntegration) {
    $startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Planetary Guard.lnk'
    $uninstallShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\卸载 Planetary Guard.lnk'
    $startMenuDirectory = Split-Path -Parent $startMenu
    try {
        New-Item -ItemType Directory -Path $startMenuDirectory -Force | Out-Null
        $shell = New-Object -ComObject WScript.Shell
        $shortcut = $shell.CreateShortcut($startMenu)
        $shortcut.TargetPath = Join-Path $destinationPath 'planetary_guard.exe'
        $shortcut.WorkingDirectory = $destinationPath
        $shortcut.Description = 'Planetary Guard 轻量 Mac 风格 Dock'
        $shortcut.Save()

        $uninstallLink = $shell.CreateShortcut($uninstallShortcut)
        $uninstallLink.TargetPath = 'powershell.exe'
        $uninstallLink.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $destinationPath 'uninstall.ps1')`""
        $uninstallLink.WorkingDirectory = $destinationPath
        $uninstallLink.Description = '卸载 Planetary Guard'
        $uninstallLink.Save()
        Write-Host "Start Menu shortcut: $startMenu"
    } catch {
        Write-Warning "无法创建开始菜单快捷方式，程序本体已安装：$($_.Exception.Message)"
    }

    $uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PlanetaryGuard'
    New-Item -Path $uninstallKey -Force | Out-Null
    $uninstallCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $destinationPath 'uninstall.ps1')`""
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'Planetary Guard' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value $Version -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'Planetary Guard Contributors' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $destinationPath -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value (Join-Path $destinationPath 'planetary_guard.exe') -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
}

Write-Host "Planetary Guard installed to $destinationPath"
