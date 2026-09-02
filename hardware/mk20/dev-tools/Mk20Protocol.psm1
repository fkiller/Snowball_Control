<#
.SYNOPSIS
    Mk20Protocol - Standalone PowerShell module for MK20 A1 serial protocol.
.DESCRIPTION
    Implements the complete A1 RPC protocol over USB CDC serial connection,
    including bi-directional CRC32 verification, chunked file upload,
    native file integrity validation (setFileCRC), backlight/volume controls,
    and device status discovery without requiring ADB.
#>

Set-StrictMode -Version Latest

# Initialize fast table-driven IEEE 802.3 CRC32
if (-not ('Mk20Crc32' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
public static class Mk20Crc32 {
    private static readonly uint[] Table;
    static Mk20Crc32() {
        Table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint entry = i;
            for (int j = 0; j < 8; j++) {
                entry = (entry & 1) != 0 ? (entry >> 1) ^ 0xedb88320u : entry >> 1;
            }
            Table[i] = entry;
        }
    }
    public static uint Calculate(byte[] data) {
        if (data == null) return 0;
        return Calculate(data, 0, data.Length);
    }
    public static uint Calculate(byte[] data, int offset, int length) {
        if (data == null || length <= 0) return 0;
        uint crc = 0xffffffffu;
        for (int i = offset; i < offset + length; i++) {
            crc = (crc >> 8) ^ Table[(crc ^ data[i]) & 0xff];
        }
        return crc ^ 0xffffffffu;
    }
}
'@
}

function Get-Mk20SerialPort {
    [CmdletBinding()]
    param()
    $port = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object PNPDeviceID -Match 'VID_1D6B&PID_0104' |
        Select-Object -First 1
    return $port
}

function New-Mk20A1Frame {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [uint32]$Id,
        [Parameter(Mandatory)] [string]$Json,
        [uint32]$MessageType = 101
    )

    $payloadBytes = [Text.Encoding]::UTF8.GetBytes($Json)
    $lenBytes = [BitConverter]::GetBytes([uint32]$payloadBytes.Length)
    $lenCrc = [Mk20Crc32]::Calculate($lenBytes)
    $payloadCrc = [Mk20Crc32]::Calculate($payloadBytes)

    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        # Magic: 0xA1, 0xA5, 0x5A, 0x5E
        $writer.Write([byte]0xA1)
        $writer.Write([byte]0xA5)
        $writer.Write([byte]0x5A)
        $writer.Write([byte]0x5E)
        $writer.Write([uint32]$Id)
        $writer.Write([uint32]$MessageType)
        $writer.Write($lenBytes)
        $writer.Write([uint32]$lenCrc)
        $writer.Write($payloadBytes)
        $writer.Write([uint32]$payloadCrc)
        return $stream.ToArray()
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Parse-Mk20A1Frame {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [byte[]]$Buffer,
        [uint32]$ExpectedId = 0
    )

    # Need at least 24 bytes header/trailer overhead
    if ($Buffer.Length -lt 24) { return $null }

    for ($offset = 0; $offset -le $Buffer.Length - 24; $offset++) {
        if ($Buffer[$offset] -ne 0xA1 -or $Buffer[$offset + 1] -ne 0xA5 -or
            $Buffer[$offset + 2] -ne 0x5A -or $Buffer[$offset + 3] -ne 0x5E) {
            continue
        }

        $id = [BitConverter]::ToUInt32($Buffer, $offset + 4)
        $msgType = [BitConverter]::ToUInt32($Buffer, $offset + 8)
        $length = [BitConverter]::ToUInt32($Buffer, $offset + 12)
        $lenCrc = [BitConverter]::ToUInt32($Buffer, $offset + 16)

        if ($ExpectedId -ne 0 -and $id -ne $ExpectedId) { continue }
        if ($length -gt 1048576) { continue } # sanity check 1MB

        # Validate Length CRC32
        $calcLenCrc = [Mk20Crc32]::Calculate($Buffer, $offset + 12, 4)
        if ($calcLenCrc -ne $lenCrc) {
            Write-Verbose "A1 frame length CRC mismatch: expected $lenCrc, calculated $calcLenCrc"
            continue
        }

        # Check if full payload and payload CRC are received
        $totalFrameLen = 24 + $length
        if ($offset + $totalFrameLen -gt $Buffer.Length) {
            # Incomplete frame, wait for more data
            continue
        }

        # Validate Payload CRC32
        $payloadOffset = $offset + 20
        $payloadCrcOffset = $offset + 20 + $length
        $expectedPayloadCrc = [BitConverter]::ToUInt32($Buffer, $payloadCrcOffset)
        $calcPayloadCrc = [Mk20Crc32]::Calculate($Buffer, $payloadOffset, [int]$length)

        if ($calcPayloadCrc -ne $expectedPayloadCrc) {
            Write-Warning "A1 frame payload CRC mismatch: expected $expectedPayloadCrc, calculated $calcPayloadCrc"
            continue
        }

        $jsonStr = [Text.Encoding]::UTF8.GetString($Buffer, $payloadOffset, [int]$length)
        return [pscustomobject]@{
            Id = $id
            MessageType = $msgType
            Length = $length
            PayloadJson = $jsonStr
            Payload = ($jsonStr | ConvertFrom-Json)
            BytesConsumed = $offset + $totalFrameLen
        }
    }
    return $null
}

function Invoke-Mk20A1 {
    [CmdletBinding()]
    param(
        [string]$PortName,
        [Parameter(Mandatory)] [string]$Method,
        [hashtable]$Parameters = @{},
        [int]$TimeoutMs = 5000,
        [IO.Ports.SerialPort]$ExistingPort
    )

    if (-not $PortName -and -not $ExistingPort) {
        $detected = Get-Mk20SerialPort
        if (-not $detected) { throw "MK20 product COM interface not found." }
        $PortName = $detected.DeviceID
    }

    $id = [uint32](Get-Random -Minimum 10000 -Maximum 2000000000)
    $requestBody = @{
        method = $Method
    }
    if ($Parameters.Count -gt 0) {
        $requestBody['parameters'] = $Parameters
    }
    $json = $requestBody | ConvertTo-Json -Compress -Depth 8
    $frame = New-Mk20A1Frame -Id $id -Json $json

    $ownsPort = $false
    $serial = $ExistingPort
    if (-not $serial) {
        $serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
        $serial.ReadTimeout = 200
        $serial.WriteTimeout = 1500
        $serial.Open()
        $serial.DiscardInBuffer()
        $ownsPort = $true
    }

    $buffer = [Collections.Generic.List[byte]]::new()
    try {
        $serial.Write($frame, 0, $frame.Length)
        $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
        do {
            if ($serial.BytesToRead -gt 0) {
                $chunk = [byte[]]::new($serial.BytesToRead)
                [void]$serial.Read($chunk, 0, $chunk.Length)
                $buffer.AddRange($chunk)
                $parsed = Parse-Mk20A1Frame -Buffer $buffer.ToArray() -ExpectedId $id
                if ($parsed) {
                    return $parsed.Payload
                }
            }
            Start-Sleep -Milliseconds 25
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "Timeout waiting for A1 response to '$Method' (id $id) from $($serial.PortName)."
    }
    finally {
        if ($ownsPort) {
            if ($serial.IsOpen) { $serial.Close() }
            $serial.Dispose()
        }
    }
}

function Get-Mk20Info {
    [CmdletBinding()]
    param([string]$PortName)
    return Invoke-Mk20A1 -PortName $PortName -Method 'getInfo'
}

function Set-Mk20Backlight {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateRange(0, 100)] [int]$Brightness,
        [string]$PortName
    )
    return Invoke-Mk20A1 -PortName $PortName -Method 'setBacklight' -Parameters @{ level = $Brightness }
}

function Set-Mk20Volume {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateRange(0, 10)] [int]$Volume,
        [string]$PortName
    )
    return Invoke-Mk20A1 -PortName $PortName -Method 'setVolume' -Parameters @{ level = $Volume }
}

function Send-Mk20File {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$SourcePath,
        [Parameter(Mandatory)] [string]$DestinationPath,
        [string]$PortName,
        [int]$ChunkSize = 96,
        [switch]$VerifyCrc
    )

    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Source file not found: $SourcePath"
    }

    $resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
    $rawBytes = [IO.File]::ReadAllBytes($resolvedSource)
    $fileCrc32 = [Mk20Crc32]::Calculate($rawBytes)
    $crcHex = "{0:x8}" -f $fileCrc32

    if (-not $PortName) {
        $detected = Get-Mk20SerialPort
        if (-not $detected) { throw "MK20 product COM interface not found." }
        $PortName = $detected.DeviceID
    }

    $serial = [IO.Ports.SerialPort]::new($PortName, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 1500
    $serial.Open()
    $serial.DiscardInBuffer()

    $totalChunks = [Math]::Ceiling($rawBytes.Length / [double]$ChunkSize)
    if ($totalChunks -eq 0) { $totalChunks = 1 } # handle empty file
    $sent = 0

    try {
        for ($offset = 0; $offset -lt $rawBytes.Length; $offset += $ChunkSize) {
            $len = [Math]::Min($ChunkSize, $rawBytes.Length - $offset)
            $chunk = [byte[]]::new($len)
            [Array]::Copy($rawBytes, $offset, $chunk, 0, $len)

            $reply = Invoke-Mk20A1 -ExistingPort $serial -Method 'saveToFile' -Parameters @{
                filePath = $DestinationPath
                seek = $offset
                data = [Convert]::ToBase64String($chunk)
            } -TimeoutMs 4000

            if (-not $reply.success) {
                throw "saveToFile failed at offset $offset`: $($reply.errorString)"
            }
            $sent++
            Write-Progress -Activity "Sending $DestinationPath over $PortName" `
                -Status "$sent / $totalChunks chunks acknowledged" `
                -PercentComplete (($sent / $totalChunks) * 100)
        }

        # Validate with firmware setFileCRC if requested or default
        $crcVerified = $false
        try {
            $crcReply = Invoke-Mk20A1 -ExistingPort $serial -Method 'setFileCRC' -Parameters @{
                filePath = $DestinationPath
                crc = $crcHex
            } -TimeoutMs 3000
            $crcVerified = [bool]$crcReply.success
        }
        catch {
            Write-Verbose "setFileCRC check returned: $($_.Exception.Message)"
        }

        return [pscustomobject]@{
            DestinationPath = $DestinationPath
            BytesSent = $rawBytes.Length
            ChunksAcknowledged = $sent
            Crc32Hex = $crcHex
            CrcVerified = $crcVerified
        }
    }
    finally {
        Write-Progress -Activity "Sending $DestinationPath over $PortName" -Completed
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}

function Remove-Mk20File {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]]$FilePaths,
        [string]$PortName
    )
    return Invoke-Mk20A1 -PortName $PortName -Method 'deleteFiles' -Parameters @{ filePaths = $FilePaths }
}

function Get-Mk20Files {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]]$Suffixes,
        [string]$PortName
    )
    return Invoke-Mk20A1 -PortName $PortName -Method 'getFilesBySuffix' -Parameters @{ suffixs = $Suffixes }
}

function Send-Mk20KeyboardInput {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$Text,
        [string]$PortName
    )
    return Invoke-Mk20A1 -PortName $PortName -Method 'keyboardInput' -Parameters @{
        inputString = $Text
        inputLength = $Text.Length
    }
}

Export-ModuleMember -Function @(
    'Get-Mk20SerialPort',
    'New-Mk20A1Frame',
    'Parse-Mk20A1Frame',
    'Invoke-Mk20A1',
    'Get-Mk20Info',
    'Set-Mk20Backlight',
    'Set-Mk20Volume',
    'Send-Mk20File',
    'Remove-Mk20File',
    'Get-Mk20Files',
    'Send-Mk20KeyboardInput'
)
