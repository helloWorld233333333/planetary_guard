param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build-native-v12'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$Version = '0.1.9'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
$buildPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
$application = Join-Path $buildPath 'planetary_guard.exe'
$setup = Join-Path $buildPath 'planetary_setup.exe'
if (!(Test-Path -LiteralPath $application) -or !(Test-Path -LiteralPath $setup)) { throw 'Build both application and setup first.' }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$staging = Join-Path $outputPath "PlanetaryGuard-$Version-win-x64"
New-Item -ItemType Directory -Path $staging -Force | Out-Null
Copy-Item -LiteralPath $application -Destination $staging -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\README.md'),(Join-Path $PSScriptRoot '..\LICENSE') -Destination $staging -Force
$zip = Join-Path $outputPath "PlanetaryGuard-$Version-win-x64-portable.zip"
Compress-Archive -LiteralPath (Join-Path $staging 'planetary_guard.exe'),(Join-Path $staging 'README.md'),(Join-Path $staging 'LICENSE') -DestinationPath $zip -Force
$installer = Join-Path $outputPath "PlanetaryGuard-$Version-win-x64-setup.exe"
Copy-Item -LiteralPath $setup -Destination $installer -Force
Get-Item -LiteralPath $zip,$installer | Select-Object FullName,Length
