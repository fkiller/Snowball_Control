[CmdletBinding()]
param([string]$Device, [string]$Adb, [Parameter(Mandatory)][string]$Output, [switch]$NoDeploy)
$ErrorActionPreference = 'Stop'
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
    if ($url -notmatch '^platform-tools.*windows\.zip$' -or $checksum -notmatch '^[a-f0-9]{40}$') { throw 'Invalid official Android Platform Tools metadata.' }
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
if (-not $Device -or -not (Test-Mk20 $Device)) {
    Write-Host 'Initial MK20 access: insert its boot-resource SD card in the PC. A full disk image backup is required before modifying it.'
    $sd = Read-Host 'Path to the backed-up MK20 SD root (for example F:\)'
    $sd = (Resolve-Path -LiteralPath $sd).Path
    if (-not (Test-Path -LiteralPath (Join-Path $sd 'dev-access.conf'))) {
        $config = Join-Path $sd 'dev-access.conf'
        $ssid = Read-Host '2.4GHz Wi-Fi SSID'
        $secret = Read-Host 'Wi-Fi password' -AsSecureString
        $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
        try { $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
        $mac = (Get-NetAdapter | Where-Object { $_.Status -eq 'Up' -and $_.PhysicalMediaType -eq 'Native 802.11' } | Select-Object -First 1).MacAddress
        if (-not $mac) { $mac = Read-Host 'PC Wi-Fi adapter MAC (AA:BB:CC:DD:EE:FF)' }
        $mac = $mac.Replace('-',':')
        if ($mac -notmatch '^([a-f0-9]{2}:){5}[a-f0-9]{2}$') { throw 'Invalid PC Wi-Fi MAC.' }
        function Shell-Quote([string]$Value) { return "'" + $Value.Replace("'", "'\" + "'" + "'") + "'" }
        # Write credentials to the selected local card only; never to logs or Git.
        $content = 'WIFI_SSID=' + (Shell-Quote $ssid) + "`nWIFI_PSK=" + (Shell-Quote $password) + "`nDEV_PC_MAC='" + $mac + "'`n"
        [IO.File]::WriteAllText($config,$content,[Text.UTF8Encoding]::new($false))
        $password = $null; $content = $null
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'lunch.sh') -Destination (Join-Path $sd 'lunch.sh') -Force
    Write-Host 'Return the card to MK20 and power it on. The factory boot hook configures Wi-Fi and private ADB.'
    $Device = Read-Host 'MK20 DHCP IPv4 address (router lease or current_ip.txt on the card)'
    if ($Device -notmatch ':') { $Device += ':5555' }
    if (-not (Test-Mk20 $Device)) { throw 'MK20 is not reachable after bootstrap. Check Wi-Fi and the allowed PC MAC, then rerun.' }
}
$network = Invoke-Adb @('-s',$Device,'shell','ip -4 addr show wlan0')
if ($network -notmatch 'inet ((?:\d{1,3}\.){3}\d{1,3})/') { throw 'MK20 has no Wi-Fi IPv4 address.' }
$mk20Address = $Matches[1]
$release = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
$zip = Join-Path $tools 'mk20-runtime.zip'
if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $release.sha256) {
    Invoke-WebRequest $release.url -OutFile $zip -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $release.sha256) { throw 'MK20 runtime checksum mismatch.' }
$bundle = Join-Path $tools 'mk20-runtime'
Expand-Archive -LiteralPath $zip -DestinationPath $bundle -Force
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
    if ($file.path -match '(^|[/\\])\.\.([/\\]|$)' -or [IO.Path]::IsPathRooted($file.path)) { throw 'Invalid bundle path.' }
    if ((Get-FileHash -LiteralPath (Join-Path $bundle $file.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'Invalid bundle file digest.' }
}
if (-not $NoDeploy) {
    # Retain the whole current persistent card/eMMC resource filesystem and firmware/config files.
    $backup = Join-Path $state ('backups\mk20-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    [void](Invoke-Adb @('-s',$Device,'pull','/mnt/SDCARD', $backup))
    Write-Host "Device files backed up privately at $backup. Full SD block-image backup remains the recovery prerequisite."
    # Factory QMK must be replaced in physical DFU mode. Existing Snowball HUD
    # installations already passed this step. Do not claim a keyboard was flashed by merely downloading a file.
    $installed = Invoke-Adb @('-s',$Device,'shell','test -x /mnt/SDCARD/mk20-hud && echo snowball || echo factory')
    if ($installed -ne 'snowball') {
        Write-Host 'Flash the included MK20 QMK binary: hold top-left while reconnecting USB, then Flash in QMK Toolbox.'
        $toolbox = Join-Path $tools 'qmk_toolbox.exe'
        if (-not (Test-Path -LiteralPath $toolbox)) { Invoke-WebRequest 'https://github.com/qmk/qmk_toolbox/releases/download/0.3.4/qmk_toolbox.exe' -OutFile $toolbox -UseBasicParsing }
        Start-Process -FilePath $toolbox -WindowStyle Hidden
        Start-Process -FilePath explorer.exe -ArgumentList @((Join-Path $control 'hardware\mk20\qmk\bin')) -WindowStyle Hidden
        [void](Read-Host 'After flashing and reconnecting MK20, press Enter to continue')
        if (-not (Test-Mk20 $Device)) { throw 'MK20 did not reconnect after QMK flashing.' }
    }
    foreach ($file in $manifest.deploy) {
        $local = Join-Path $bundle $file
        $remote = '/mnt/SDCARD/' + $file.Replace('\','/')
        [void](Invoke-Adb @('-s',$Device,'shell',"mkdir -p /mnt/SDCARD/fonts"))
        [void](Invoke-Adb @('-s',$Device,'push',$local,($remote + '.new')))
        $md5 = (Get-FileHash -LiteralPath $local -Algorithm MD5).Hash.ToLowerInvariant()
        $remoteSum = Invoke-Adb @('-s',$Device,'shell',("md5sum '" + $remote + ".new'"))
        if (($remoteSum -split '\s')[0] -ne $md5) { throw 'Device file verification failed; old file retained.' }
        [void](Invoke-Adb @('-s',$Device,'shell',("mv '" + $remote + ".new' '" + $remote + "'")))
    }
    [void](Invoke-Adb @('-s',$Device,'shell','chmod +x /mnt/SDCARD/mk20-hud /mnt/SDCARD/voice_rec.sh /mnt/SDCARD/pcm-stream; /etc/init.d/qt_app2 disable; /etc/init.d/qt_app2 stop; killall KeyboardDevice 2>/dev/null; killall mk20-hud 2>/dev/null; /mnt/SDCARD/mk20-hud -d'))
    Start-Sleep -Seconds 2
}
$verified = Invoke-Adb @('-s',$Device,'shell','pidof mk20-hud; test -e /dev/fb21 && test -e /dev/ttyS1 && echo hardware-ready; if test -r /mnt/SDCARD/fonts/D2Coding.ttf || test -r /usr/share/fonts/D2Coding.ttf; then echo font-ready; fi')
if ($verified -notmatch '\d+' -or $verified -notmatch 'hardware-ready' -or $verified -notmatch 'font-ready') { throw 'Native MK20 HUD/keyboard/font readiness was not verified.' }
$record = @{mk20Address=$mk20Address;mk20AdbDevice=$Device;adb=$script:Adb}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Output),($record | ConvertTo-Json) + "`n",[Text.UTF8Encoding]::new($false))
Write-Host "Verified native MK20 HUD at $mk20Address; middleware will connect to UDP 7701."
