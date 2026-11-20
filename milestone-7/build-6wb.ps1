# Manual QNX SDP 6.5 ARMv7LE build. Run this script from Windows PowerShell.
[CmdletBinding()]
param(
    [string]$QnxRoot = 'C:\Program Files (x86)\QNX6.5',
    [ValidateRange(1, 32)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$previousHost = $env:QNX_HOST
$previousTarget = $env:QNX_TARGET
$previousPath = $env:PATH
$buildExit = 1

try {
    $qnxFilesystem = New-Object -ComObject Scripting.FileSystemObject
    $hostFolder = Join-Path $QnxRoot 'host\win32\x86'
    $targetFolder = Join-Path $QnxRoot 'target\qnx6'
    foreach ($folder in @($hostFolder, $targetFolder)) {
        if (-not (Test-Path -LiteralPath $folder -PathType Container)) {
            throw "QNX folder missing: $folder"
        }
    }
    # The Windows GCC 4.4 driver splits long SDK paths at spaces.
    $env:QNX_HOST = $qnxFilesystem.GetFolder($hostFolder).ShortPath.Replace('\', '/')
    $env:QNX_TARGET = $qnxFilesystem.GetFolder($targetFolder).ShortPath.Replace('\', '/')
    if ($env:QNX_HOST.Contains(' ') -or $env:QNX_TARGET.Contains(' ')) {
        throw 'QNX short paths are unavailable. Supply -QnxRoot with an existing SDK path without spaces.'
    }
    $qnxBin = Join-Path $env:QNX_HOST 'usr/bin'
    $env:PATH = "$qnxBin;$previousPath"
    $make = Join-Path $qnxBin 'make.exe'
    $qcc = Join-Path $qnxBin 'qcc.exe'
    $readelf = Join-Path $qnxBin 'ntoarmv7-readelf.exe'
    foreach ($tool in @($make, $qcc, $readelf)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw "QNX tool missing: $tool"
        }
    }

    Set-Location -LiteralPath $PSScriptRoot
    foreach ($library in @('liba2i_client.so', 'libssl.so', 'libcrypto.so', 'libscreen.so')) {
        $libraryPath = Join-Path $PSScriptRoot "lib/$library"
        if (-not (Test-Path -LiteralPath $libraryPath -PathType Leaf)) {
            throw "Project library missing: $libraryPath"
        }
    }
    $buildName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $relativeDirectory = "output/6wb-manual/$buildName"
    $buildDirectory = Join-Path $PSScriptRoot $relativeDirectory
    New-Item -ItemType Directory -Path $buildDirectory | Out-Null
    $previousBinary = Join-Path $PSScriptRoot 'nbt-aad'
    if (Test-Path -LiteralPath $previousBinary -PathType Leaf) {
        Copy-Item -LiteralPath $previousBinary -Destination (Join-Path $buildDirectory 'previous-nbt-aad')
    }

    $relativeTarget = "$relativeDirectory/nbt-aad"
    $buildLog = Join-Path $buildDirectory 'build.log'
    $arguments = @('-B', "-j$Jobs", 'QCC=qcc', 'TARGET_ARCH=-Vgcc_ntoarmv7le_cpp',
                   "TARGET=$relativeTarget", $relativeTarget)
    Write-Host "Building QNX ARMv7LE in $buildDirectory"
    # Windows PowerShell 5.1 wraps native stderr as ErrorRecord, including GCC warnings.
    $ErrorActionPreference = 'Continue'
    & $make @arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $buildLog
    $makeExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($makeExit -ne 0) { throw "QNX make failed ($makeExit). See $buildLog" }

    $binary = Join-Path $buildDirectory 'nbt-aad'
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Output missing: $binary" }
    $bytes = [System.IO.File]::ReadAllBytes($binary)
    if ($bytes.Length -lt 52 -or $bytes[0] -ne 127 -or $bytes[1] -ne 69 -or
        $bytes[2] -ne 76 -or $bytes[3] -ne 70 -or $bytes[4] -ne 1 -or $bytes[5] -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 16) -ne 2 -or [BitConverter]::ToUInt16($bytes, 18) -ne 40) {
        throw 'The output must be ELF32 little-endian, ET_EXEC, ARM.'
    }
    $ErrorActionPreference = 'Continue'
    $elf = & $readelf '-h' '-l' '-d' $binary 2>&1
    $readelfExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $elf | Set-Content -LiteralPath (Join-Path $buildDirectory 'elf.txt') -Encoding UTF8
    if ($readelfExit -ne 0) { throw 'QNX readelf failed.' }
    if (($elf -join "`n") -notmatch '/usr/lib/ldqnx\.so\.2') {
        throw 'The QNX interpreter /usr/lib/ldqnx.so.2 was not found in the program headers.'
    }
    $hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    @("Binary $binary", "Bytes $($bytes.Length)", "SHA256 $hash",
      'ELF32 little-endian, ET_EXEC, ARM, QNX ldqnx.so.2',
      "QNX_HOST=$env:QNX_HOST", "QNX_TARGET=$env:QNX_TARGET") |
        Set-Content -LiteralPath (Join-Path $buildDirectory 'build-info.txt') -Encoding UTF8
    Write-Host "Verified binary $binary"
    Write-Host "SHA256 $hash"
    $buildExit = 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    $env:QNX_HOST = $previousHost
    $env:QNX_TARGET = $previousTarget
    $env:PATH = $previousPath
}
exit $buildExit
