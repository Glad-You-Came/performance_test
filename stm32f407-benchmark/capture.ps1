[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^COM[0-9]+$')][string]$Port,
    [string]$JLink = 'D:\SEGGER\JLink_V960\JLink.exe',
    [ValidatePattern('^[0-9]+$')][string]$ProbeSerial,
    [ValidateSet('armcc-speed', 'gcc-speed', 'armcc-size', 'gcc-size')][string[]]$Images =
        @('armcc-speed', 'gcc-speed', 'armcc-size', 'gcc-size'),
    [ValidateRange(1, 3)][int[]]$RunNumbers = @(1, 2, 3),
    [ValidateRange(13000, 240000)][int]$HoldMilliseconds = 90000,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Get-Command node -ErrorAction Stop | Out-Null

if (!$OutputDir) {
    $OutputDir = Join-Path $PSScriptRoot ('runs/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (Test-Path $OutputDir) { throw "Choose a new capture directory: $OutputDir" }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

function Start-Probe([string]$Name, [string[]]$Commands) {
    $scriptFile = Join-Path $OutputDir "$Name.jlink"
    $log = Join-Path $OutputDir "$Name.jlink.log"
    $errorLog = Join-Path $OutputDir "$Name.jlink.stderr.log"
    # Keep the probe attached during DWT measurements, UART and both CoreMark runs.
    Set-Content -LiteralPath $scriptFile -Value ($Commands + @("sleep $HoldMilliseconds", 'q')) -Encoding ascii
    $options = '-Device STM32F407IG -If SWD -Speed 4000 -AutoConnect 1 -ExitOnError 1 -NoGui 1'
    if ($ProbeSerial) { $options += " -USB $ProbeSerial" }
    $options += " -CommandFile `"$scriptFile`""
    Start-Process -FilePath $JLink -ArgumentList $options -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError $errorLog
}

function Confirm-Probe([Diagnostics.Process]$Process, [string]$Name, [bool]$VerifyFlash) {
    $log = Join-Path $OutputDir "$Name.jlink.log"
    if (!$Process.WaitForExit($HoldMilliseconds + 5000)) { throw "J-Link timed out. See $log" }
    $result = Get-Content -LiteralPath $log -Raw
    if ($Process.ExitCode -ne 0 -or $result -match '(?im)can\s*not (connect|attach)|could not connect|failed to (connect|open)|verif(y|ication) failed|no emulator|no j-link') {
        throw "J-Link failed. See $log"
    }
    if ($VerifyFlash -and $result -notmatch '(?im)^Verify successful\.\s*$') {
        throw "J-Link did not confirm binary verification. See $log"
    }
}

function Read-Board([IO.Ports.SerialPort]$Serial, [string]$Path, [string]$Image,
                    [Diagnostics.Process]$Probe, [string]$Name, [bool]$VerifyFlash) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $writer = [IO.StreamWriter]::new($Path, $false, [Text.Encoding]::ASCII)
    $started = $false
    try {
        while ($watch.ElapsedMilliseconds -lt $HoldMilliseconds + 20000) {
            try { $line = $Serial.ReadLine().TrimEnd("`r") }
            catch [TimeoutException] {
                if ($Probe.HasExited) {
                    Confirm-Probe $Probe $Name $VerifyFlash
                    throw "UART stopped before END,$Image"
                }
                continue
            }
            $writer.WriteLine($line)
            if ($line.StartsWith('BEGIN,')) {
                if (!$line.StartsWith("BEGIN,$Image,")) { throw "Unexpected firmware: $line" }
                if ($started) { throw 'Unexpected second BEGIN in one run.' }
                $started = $true
            }
            if ($started -and $line -eq "END,$Image") { return }
        }
        throw "UART timed out before END,$Image"
    } finally {
        $writer.Dispose()
    }
}

$availablePorts = [IO.Ports.SerialPort]::GetPortNames()
if ($Port -notin $availablePorts) {
    $message = "Serial port $Port is unavailable. Detected: $($availablePorts -join ', ')"
    Set-Content -LiteralPath (Join-Path $OutputDir 'connection.error.txt') -Value $message
    throw $message
}

$reportTool = Join-Path $PSScriptRoot 'tools/report.cjs'
$selectedRuns = @($RunNumbers | Sort-Object -Unique)
$failed = $false
foreach ($image in $Images) {
    $bin = Join-Path $PSScriptRoot "build/$image/$image.bin"
    if (!(Test-Path -LiteralPath $bin)) {
        Set-Content -LiteralPath (Join-Path $OutputDir "$image.error.txt") -Value "Missing $bin"
        $failed = $true
        continue
    }
    $hash = (Get-FileHash -LiteralPath $bin -Algorithm SHA256).Hash
    Set-Content -LiteralPath (Join-Path $OutputDir "$image.sha256") -Value $hash -Encoding ascii
    $programmed = $false
    foreach ($run in $selectedRuns) {
        $name = "$image-$run"
        $serial = [IO.Ports.SerialPort]::new($Port, 115200, [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
        $probe = $null
        try {
            $serial.ReadTimeout = 250
            $serial.ReadBufferSize = 65536
            $serial.Open()
            $serial.DiscardInBuffer()
            $verifyFlash = !$programmed
            if ($verifyFlash) {
                $commands = @('r', 'h', "loadfile `"$bin`" 0x08000000 noreset",
                              "verifybin `"$bin`" 0x08000000", 'r', 'g')
            } else {
                $commands = @('r', 'g')
            }
            $probe = Start-Probe $name $commands
            $capture = Join-Path $OutputDir "$name.csv"
            Read-Board $serial $capture $image $probe $name $verifyFlash
            Confirm-Probe $probe $name $verifyFlash
            $programmed = $true
            $validation = & node $reportTool --check-run $capture $image
            if ($LASTEXITCODE -ne 0) { throw ($validation -join ' ') }
            Write-Host "Captured $name"
        } catch {
            $failed = $true
            Set-Content -LiteralPath (Join-Path $OutputDir "$name.error.txt") -Value $_.Exception.Message
            Write-Warning "$name $($_.Exception.Message)"
        } finally {
            if ($probe) {
                if (!$probe.HasExited) { $probe.Kill() }
                $probe.Dispose()
            }
            $serial.Dispose()
        }
    }
}

& node $reportTool $OutputDir $selectedRuns.Count
if ($LASTEXITCODE -ne 0) { throw 'Report generation failed.' }
Write-Host "Capture directory $OutputDir"
if ($failed) { throw "One or more runs failed. See $OutputDir" }
