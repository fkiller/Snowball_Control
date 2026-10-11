[CmdletBinding()]
param([string]$Device, [string]$Adb, [Parameter(Mandatory)][string]$Output, [switch]$NoDeploy, [string]$RuntimeZip, [string]$RuntimeSha256)
$ErrorActionPreference = 'Stop'
if (-not $NoDeploy) { throw 'Use the tray Device setup for MK20 QMK/USB updates with verified recovery backups. This legacy ADB helper only supports explicit -NoDeploy diagnostics.' }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$control = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$state = Split-Path ([IO.Path]::GetFullPath($Output))
New-Item -ItemType Directory -Path $state -Force | Out-Null
$tools = Join-Path $state 'tools'
New-Item -ItemType Directory -Path $tools -Force | Out-Null
function Invoke-Adb([string[]]$Arguments) {
    $result = & $script:Adb @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'ADB operation failed; device installation is incomplete.' }
    return ($result | Out-String).Trim()
}
if (-not $Adb) { $Adb = $env:MK20_ADB }
if (-not $Adb) { $Adb = Get-Command adb.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1 }
if (-not $Adb) {
    # Resolve the actual Windows download and checksum from Google's SDK metadata.
    [xml]$metadata = (Invoke-WebRequest 'https://dl.google.com/android/repository/repository2-1.xml' -UseBasicParsing).Content
    $package = $metadata.SelectSingleNode("//*[local-name()='remotePackage' and @path='platform-tools']")
    $archive = $package.SelectSingleNode(".//*[local-name()='archive'][*[local-name()='host-os']='windows']/*[local-name()='complete']")
    $url = $archive.SelectSingleNode("*[local-name()='url']").InnerText
    $checksum = $archive.SelectSingleNode("*[local-name()='checksum']").InnerText
    if ($url -notmatch '^platform-tools.*(?:win|windows)\.zip$' -or $checksum -notmatch '^[a-f0-9]{40}$') { throw 'Invalid official Android Platform Tools metadata.' }
    $zip = Join-Path $tools 'platform-tools.zip'
    Invoke-WebRequest ('https://dl.google.com/android/repository/' + $url) -OutFile $zip -UseBasicParsing
    if ((Get-FileHash -LiteralPath $zip -Algorithm SHA1).Hash.ToLowerInvariant() -ne $checksum) { throw 'ADB download checksum mismatch.' }
    Expand-Archive -LiteralPath $zip -DestinationPath $tools -Force
    $Adb = Join-Path $tools 'platform-tools\adb.exe'
}
$script:Adb = (Resolve-Path -LiteralPath $Adb).Path
function Test-Mk20([string]$Endpoint) {
    if ($Endpoint -notmatch '^(?:\d{1,3}\.){3}\d{1,3}:\d{1,5}$') { return $false }
    $hostName,$port = $Endpoint.Split(':')
    $tcp = [Net.Sockets.TcpClient]::new()
    try { if (-not $tcp.ConnectAsync($hostName,[int]$port).Wait(300)) { return $false } }
    catch { return $false }
    finally { $tcp.Dispose() }
    try {
        [void](Invoke-Adb @('connect',$Endpoint))
        $answer = Invoke-Adb @('-s',$Endpoint,'shell','test -e /dev/fb21 && test -e /dev/ttyS1 && uname -m')
        return $answer -match 'armv7'
    } catch { return $false }
}
if (-not $Device -and (Test-Path -LiteralPath $Output)) {
    $saved = Get-Content -LiteralPath $Output -Raw | ConvertFrom-Json
    if (Test-Mk20 $saved.mk20AdbDevice) { $Device = $saved.mk20AdbDevice }
}
if (-not $Device) {
    $candidates = @((Invoke-Adb @('devices')) -split '\r?\n' | Where-Object { $_ -match '\sdevice$' } | ForEach-Object { ($_ -split '\s')[0] })
    $candidates += @(Get-NetNeighbor -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.State -in @('Reachable','Stale','Delay','Probe') -and $_.IPAddress -match '^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)' } | Select-Object -First 64 | ForEach-Object { $_.IPAddress + ':5555' })
    $found = @($candidates | Select-Object -Unique | Where-Object { Test-Mk20 $_ })
    if ($found.Count -eq 1) { $Device = $found[0] }
    elseif ($found.Count -gt 1) { throw 'More than one MK20 is reachable. Rerun with -Mk20Address IP:5555.' }
}
if ($Device -and $Device -notmatch ':') { $Device += ':5555' }
if (-not $Device -or -not (Test-Mk20 $Device)) { throw 'No MK20 debug endpoint is reachable. Use native Device setup for preparation; ADB diagnostics never modify media.' }
$network = Invoke-Adb @('-s',$Device,'shell','ip -4 addr show wlan0')
if ($network -notmatch 'inet ((?:\d{1,3}\.){3}\d{1,3})/') { throw 'MK20 has no Wi-Fi IPv4 address.' }
$mk20Address = $Matches[1]
# Explicit maintenance command only; ordinary PC profile installation never calls ADB.
if ($RuntimeZip) {
    if ($RuntimeSha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Supply -RuntimeSha256 for the reviewed local bundle.' }
    $zip = (Resolve-Path -LiteralPath $RuntimeZip).Path
    $expectedHash = $RuntimeSha256.ToLowerInvariant()
} else {
    $release = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
    $zip = Join-Path $tools 'mk20-runtime.zip'
    $expectedHash = $release.sha256
    if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
        Invoke-WebRequest $release.url -OutFile $zip -UseBasicParsing
    }
}
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) { throw 'MK20 runtime checksum mismatch.' }
$bundle = Join-Path $tools 'mk20-runtime'
Expand-Archive -LiteralPath $zip -DestinationPath $bundle -Force
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
$allowedDeploy = @('mk20-hud','mk20-audio','lunch.sh','fonts/D2Coding.ttf','mk20-update')
$missingDeploy = @($allowedDeploy | Where-Object { $_ -notin $manifest.deploy })
if ($manifest.deviceProtocol -ne 'SNMK1' -or $manifest.audioProtocol -ne 'SNAU-lease-v1' -or $missingDeploy.Count -ne 0) { throw 'This bundle does not implement device-owned discovery and native leased audio. Supply the reviewed LAN runtime bundle with -RuntimeZip and -RuntimeSha256.' }
if (@($manifest.deploy | Where-Object { $_ -notin $allowedDeploy }).Count) { throw 'Unexpected runtime deployment target.' }
foreach ($file in $manifest.files) {
    if ($file.path -notmatch '^[a-zA-Z0-9_./-]+$' -or $file.path -match '(^|[/\\])\.\.([/\\]|$)' -or [IO.Path]::IsPathRooted($file.path)) { throw 'Invalid bundle path.' }
    if ((Get-FileHash -LiteralPath (Join-Path $bundle $file.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'Invalid bundle file digest.' }
}
foreach ($native in @('mk20-hud','mk20-audio')) {
    $runtimePid = Invoke-Adb @('-s',$Device,'shell',('pidof ' + $native))
    if ($runtimePid -notmatch '^\d+$') { throw 'Expected one managed native service.' }
    $runningDigest = Invoke-Adb @('-s',$Device,'shell',('md5sum /proc/' + $runtimePid + '/exe'))
    $expectedDigest = (Get-FileHash -LiteralPath (Join-Path $bundle $native) -Algorithm MD5).Hash.ToLowerInvariant()
    if (($runningDigest -split '\s')[0] -ne $expectedDigest) { throw 'The running device process does not match the reviewed runtime bundle.' }
}
$verified = Invoke-Adb @('-s',$Device,'shell','pidof mk20-hud >/dev/null && echo hud-ready; pidof mk20-audio >/dev/null && echo audio-ready; test -e /dev/fb21 && test -e /dev/ttyS1 && echo hardware-ready; if test -r /mnt/SDCARD/fonts/D2Coding.ttf || test -r /usr/share/fonts/D2Coding.ttf; then echo font-ready; fi')
if (($verified -notmatch 'hud-ready' -or $verified -notmatch 'audio-ready') -or $verified -notmatch 'hardware-ready' -or $verified -notmatch 'font-ready') { throw 'Native MK20 HUD/audio/keyboard/font readiness was not verified.' }
$record = @{mk20Address=$mk20Address;mk20AdbDevice=$Device;adb=$script:Adb}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Output),($record | ConvertTo-Json) + "`n",[Text.UTF8Encoding]::new($false))
Write-Host "Verified MK20 native runtime at $mk20Address; select your middleware from Machines (K17)."
