<#
.SYNOPSIS
    Automated test suite for MK20 A1 protocol implementation.
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Resolve-Path "$PSScriptRoot\Mk20Protocol.psm1") -Force

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

function Assert-Null {
    param($Value, [string]$TestName)
    Assert-Equal -Expected $true -Actual ($null -eq $Value) -TestName $TestName
}

Write-Host "=== Running MK20 A1 Protocol Unit Tests ===" -ForegroundColor Cyan

# Test 1: CRC32 Standard Test Vector
Write-Host "`n-- 1. CRC32 Validation --" -ForegroundColor Yellow
$vectorBytes = [Text.Encoding]::ASCII.GetBytes('123456789')
$crc = [Mk20Crc32]::Calculate($vectorBytes)
Assert-Equal -Expected ([uint32]3421780262) -Actual $crc -TestName 'CRC32 standard vector "123456789" == 0xCBF43926'

$emptyBytes = [byte[]]::new(0)
$emptyCrc = [Mk20Crc32]::Calculate($emptyBytes)
Assert-Equal -Expected ([uint32]0) -Actual $emptyCrc -TestName 'CRC32 empty byte array == 0'

# Test 2: Frame Packing
Write-Host "`n-- 2. Frame Packing --" -ForegroundColor Yellow
$testJson = '{"method":"getInfo"}'
$testId = [uint32]12345678
$frame = New-Mk20A1Frame -Id $testId -Json $testJson

Assert-Equal -Expected 0xA1 -Actual $frame[0] -TestName 'Magic byte 0 == 0xA1'
Assert-Equal -Expected 0xA5 -Actual $frame[1] -TestName 'Magic byte 1 == 0xA5'
Assert-Equal -Expected 0x5A -Actual $frame[2] -TestName 'Magic byte 2 == 0x5A'
Assert-Equal -Expected 0x5E -Actual $frame[3] -TestName 'Magic byte 3 == 0x5E'

$parsedId = [BitConverter]::ToUInt32($frame, 4)
Assert-Equal -Expected $testId -Actual $parsedId -TestName "Frame ID matches $testId"

$expectedTotalLength = 24 + [Text.Encoding]::UTF8.GetByteCount($testJson)
Assert-Equal -Expected $expectedTotalLength -Actual $frame.Length -TestName "Total frame length == 24 + payload ($expectedTotalLength)"

# Test 3: Frame Parsing
Write-Host "`n-- 3. Frame Parsing --" -ForegroundColor Yellow
$parsed = Parse-Mk20A1Frame -Buffer $frame -ExpectedId $testId
Assert-True -Condition ($null -ne $parsed) -TestName 'Parse-Mk20A1Frame successfully parses valid frame'
Assert-Equal -Expected $testId -Actual $parsed.Id -TestName 'Parsed ID matches expected'
Assert-Equal -Expected 'getInfo' -Actual $parsed.Payload.method -TestName 'Parsed payload JSON object contains method'
Assert-Equal -Expected $frame.Length -Actual $parsed.BytesConsumed -TestName 'BytesConsumed matches total frame length'

# Test 4: Leading Garbage Resynchronization
Write-Host "`n-- 4. Stream Resynchronization --" -ForegroundColor Yellow
$garbage = [byte[]](0x00, 0xFF, 0x12, 0x34, 0x56)
$streamWithGarbage = $garbage + $frame
$resynced = Parse-Mk20A1Frame -Buffer $streamWithGarbage -ExpectedId $testId
Assert-True -Condition ($null -ne $resynced) -TestName 'Parser recovers and resyncs past leading garbage bytes'
Assert-Equal -Expected ($garbage.Length + $frame.Length) -Actual $resynced.BytesConsumed -TestName 'BytesConsumed accounts for skipped garbage'

# Test 5: Incomplete Frame Handling
Write-Host "`n-- 5. Incomplete / Truncated Frames --" -ForegroundColor Yellow
$truncated = $frame[0..($frame.Length - 5)]
$truncatedParsed = Parse-Mk20A1Frame -Buffer $truncated -ExpectedId $testId
Assert-Null -Value $truncatedParsed -TestName 'Truncated frame returns $null (waits for more stream data)'

# Test 6: Corrupted Magic Header Rejection
Write-Host "`n-- 6. Corruption Rejection --" -ForegroundColor Yellow
$corruptMagic = [byte[]]$frame.Clone()
$corruptMagic[0] = 0x00
$corruptMagicParsed = Parse-Mk20A1Frame -Buffer $corruptMagic -ExpectedId $testId
Assert-Null -Value $corruptMagicParsed -TestName 'Frame with corrupted magic header is rejected'

# Test 7: Corrupted Length CRC Rejection
$corruptLenCrc = [byte[]]$frame.Clone()
$corruptLenCrc[16] = ($corruptLenCrc[16] -bxor 0xFF)
$corruptLenCrcParsed = Parse-Mk20A1Frame -Buffer $corruptLenCrc -ExpectedId $testId
Assert-Null -Value $corruptLenCrcParsed -TestName 'Frame with corrupted Length CRC32 is rejected'

# Test 8: Corrupted Payload CRC Rejection
$corruptPayloadCrc = [byte[]]$frame.Clone()
$corruptPayloadCrc[$frame.Length - 1] = ($corruptPayloadCrc[$frame.Length - 1] -bxor 0xFF)
$corruptPayloadCrcParsed = Parse-Mk20A1Frame -Buffer $corruptPayloadCrc -ExpectedId $testId
Assert-Null -Value $corruptPayloadCrcParsed -TestName 'Frame with corrupted Payload CRC32 is rejected'

# Test 9: Expected ID Filter
Write-Host "`n-- 7. ID Filtering --" -ForegroundColor Yellow
$wrongIdParsed = Parse-Mk20A1Frame -Buffer $frame -ExpectedId ([uint32]99999999)
Assert-Null -Value $wrongIdParsed -TestName 'Frame with mismatched ExpectedId is ignored'

# Summary
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Test Results: $script:Passed Passed, $script:Failed Failed" -ForegroundColor $(if ($script:Failed -eq 0) { 'Green' } else { 'Red' })
if ($script:Failed -gt 0) {
    throw "$script:Failed test(s) failed."
}
