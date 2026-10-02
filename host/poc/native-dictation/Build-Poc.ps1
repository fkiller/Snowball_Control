param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'artifacts\mk20'))
$ErrorActionPreference = 'Stop'
$targetDir = [IO.Path]::GetFullPath($OutputDirectory)
dotnet build (Join-Path $PSScriptRoot 'NativeDictation.csproj') -c Release -o $targetDir --nologo
if ($LASTEXITCODE -ne 0) { throw 'PoC build failed.' }
# Windows PowerShell includes the .NET Framework speech synthesizer; no cloud call.
& "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -File (Join-Path $PSScriptRoot 'New-Fixture.ps1') -OutputPath (Join-Path $targetDir 'fixture.wav')
if ($LASTEXITCODE -ne 0) { throw 'Test fixture generation failed.' }
Write-Output (Join-Path $targetDir 'Snowball.Dictation.Poc.exe')
