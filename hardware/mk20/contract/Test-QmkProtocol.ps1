<#
.SYNOPSIS
    Test-QmkProtocol - Protocol fixtures and unit tests for Linux <-> GD32/QMK serial contract.
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Passed = 0
$script:Failed = 0

function Assert-Equal {
    param($Expected, $Actual, [string]$TestName)
    if ($Expected -eq $Actual) {
        Write-Host "  [PASS] $TestName" -ForegroundColor Green
        $script:Passed++
    }
    else {
        Write-Host "  [FAIL] $TestName" -ForegroundColor Red
        Write-Host "         Expected: $Expected" -ForegroundColor DarkGray
        Write-Host "         Actual:   $Actual" -ForegroundColor DarkGray
        $script:Failed++
    }
}

function Assert-True {
    param([bool]$Condition, [string]$TestName)
    Assert-Equal -Expected $true -Actual $Condition -TestName $TestName
}

Write-Host "=== Running Linux <-> GD32/QMK Serial Contract Tests ===" -ForegroundColor Cyan

# Test 1: VIA Frame Packing & Checksum Calculation
Write-Host "`n-- 1. VIA Frame Packing --" -ForegroundColor Yellow

function New-ViaFrame {
    param([byte]$CmdId, [byte[]]$Payload)
    $len = [byte]($Payload.Length)
    $sum = (0xA5 + $CmdId + $len)
    foreach ($b in $Payload) { $sum = ($sum + $b) }
    $checksum = [byte]($sum % 256)
    return [byte[]](@(0xA5, $CmdId, $len) + $Payload + @($checksum))
}

function Parse-ViaFrame {
    param([byte[]]$Frame)
    if ($Frame.Length -lt 4) { return $null }
    if ($Frame[0] -ne 0xA5) { return $null }
    $cmdId = $Frame[1]
    $len = $Frame[2]
    if ($Frame.Length -lt (4 + $len)) { return $null }
    $payload = $Frame[3..(3 + $len - 1)]
    $expectedChecksum = $Frame[3 + $len]

    $sum = (0xA5 + $cmdId + $len)
    foreach ($b in $payload) { $sum = ($sum + $b) }
    $calcChecksum = [byte]($sum % 256)

    if ($calcChecksum -ne $expectedChecksum) { return $null }
    return [pscustomobject]@{
        CommandId = $cmdId
        Payload = $payload
    }
}

$testPayload = [byte[]](0x01, 0x02, 0x03, 0x04)
$viaFrame = New-ViaFrame -CmdId 0x10 -Payload $testPayload
Assert-Equal -Expected 0xA5 -Actual $viaFrame[0] -TestName 'VIA start byte == 0xA5'
Assert-Equal -Expected 0x10 -Actual $viaFrame[1] -TestName 'VIA command ID == 0x10'
Assert-Equal -Expected 4 -Actual $viaFrame[2] -TestName 'VIA payload length == 4'
Assert-Equal -Expected 8 -Actual $viaFrame.Length -TestName 'Total VIA frame length == 8'

$parsedVia = Parse-ViaFrame -Frame $viaFrame
Assert-True -Condition ($null -ne $parsedVia) -TestName 'Parse-ViaFrame successfully validates valid frame'
Assert-Equal -Expected 0x10 -Actual $parsedVia.CommandId -TestName 'Parsed CommandId matches'

# Test 2: Checksum Corruption Rejection
Write-Host "`n-- 2. Corruption Rejection --" -ForegroundColor Yellow
$corruptVia = [byte[]]$viaFrame.Clone()
$corruptVia[7] = ($corruptVia[7] -bxor 0xFF)
$corruptParsed = Parse-ViaFrame -Frame $corruptVia
Assert-True -Condition ($null -eq $corruptParsed) -TestName 'Frame with invalid checksum is rejected'

# Test 3: KeyboardInfo Binary Specification Verification
Write-Host "`n-- 3. KeyboardInfo Matrix Layout Parser --" -ForegroundColor Yellow

function Parse-KeyboardInfo {
    param([byte[]]$Data)
    if ($Data.Length -ne 268) {
        throw "Invalid KeyboardInfo length $($Data.Length); expected exactly 268 bytes."
    }

    # Big-endian 4-byte count
    $itemCount = ([uint32]$Data[0] -shl 24) -bor ([uint32]$Data[1] -shl 16) -bor ([uint32]$Data[2] -shl 8) -bor [uint32]$Data[3]

    # 6 auxiliary/dial records (10 bytes each: offsets 4, 14, 24, 34, 44, 54)
    # 20 matrix switch records (10 bytes each: offsets 64..263)
    $matrixKeys = [Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt 20; $i++) {
        $offset = 64 + ($i * 10)
        $row = ([uint32]$Data[$offset] -shl 24) -bor ([uint32]$Data[$offset + 1] -shl 16) -bor ([uint32]$Data[$offset + 2] -shl 8) -bor [uint32]$Data[$offset + 3]
        $col = ([uint32]$Data[$offset + 4] -shl 24) -bor ([uint32]$Data[$offset + 5] -shl 16) -bor ([uint32]$Data[$offset + 6] -shl 8) -bor [uint32]$Data[$offset + 7]
        $matrixKeys.Add([pscustomobject]@{ Index = $i; Row = $row; Col = $col })
    }

    # Terminator at 264..267 (-1 / 0xFFFFFFFF)
    $term = [BitConverter]::ToInt32($Data, 264)

    return [pscustomobject]@{
        ItemCount = $itemCount
        MatrixKeys = $matrixKeys
        Terminator = $term
    }
}

$snapshotKbInfoPath = Join-Path $PSScriptRoot "..\dev-tools\snapshots\snapshot-20260902-182631\files\KeyboardInfo"
if (Test-Path -LiteralPath $snapshotKbInfoPath) {
    $kbBytes = [IO.File]::ReadAllBytes((Resolve-Path $snapshotKbInfoPath).Path)
    $kbParsed = Parse-KeyboardInfo -Data $kbBytes
    Assert-Equal -Expected ([uint32]26) -Actual $kbParsed.ItemCount -TestName 'KeyboardInfo total item count == 26'
    Assert-Equal -Expected 20 -Actual $kbParsed.MatrixKeys.Count -TestName 'Parsed exactly 20 mechanical key coordinate records'
    Assert-Equal -Expected ([uint32]3) -Actual $kbParsed.MatrixKeys[0].Row -TestName 'Key 0 is at Row 3'
    Assert-Equal -Expected ([uint32]4) -Actual $kbParsed.MatrixKeys[0].Col -TestName 'Key 0 is at Col 4'
    Assert-Equal -Expected ([uint32]0) -Actual $kbParsed.MatrixKeys[19].Row -TestName 'Key 19 is at Row 0'
    Assert-Equal -Expected ([uint32]0) -Actual $kbParsed.MatrixKeys[19].Col -TestName 'Key 19 is at Col 0'
    Assert-Equal -Expected (-1) -Actual $kbParsed.Terminator -TestName 'KeyboardInfo terminator == -1 (0xFFFFFFFF)'
} else {
    Write-Warning "Snapshot KeyboardInfo not found; skipping binary layout parser test."
}

# Summary
Write-Host "`n=======================================================" -ForegroundColor Cyan
Write-Host "Test Results: $script:Passed Passed, $script:Failed Failed" -ForegroundColor $(if ($script:Failed -eq 0) { 'Green' } else { 'Red' })
if ($script:Failed -gt 0) {
    throw "$script:Failed test(s) failed."
}
