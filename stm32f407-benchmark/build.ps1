[CmdletBinding()]
param(
    [ValidateSet('all', 'ac6', 'gcc')][string]$Toolchain = 'all',
    [string]$Ac6Bin = 'D:\Keil_v5\ARM\ARMCLANG\bin',
    [string]$GccBin = $env:F407_GCC_BIN
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Compiler([string]$Exe, [string[]]$Arguments) {
    $command = (ConvertTo-Json -Compress -InputObject (@($Exe) + $Arguments))
    Add-Content -LiteralPath "$output/commands.jsonl" -Value $command -Encoding utf8
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe exited with $LASTEXITCODE" }
}

Push-Location $PSScriptRoot
try {
    $device = 'Drivers/CMSIS/Device/ST/STM32F4xx'
    $includes = @('-IDrivers', '-IDrivers/CMSIS/Include', "-I$device/Include",
                  '-Ibench', '-Ibench/coremark', '-Ithird_party/coremark')
    $sources = @('bench/main.c', 'bench/kernels.c', 'bench/algorithms.c',
                 'bench/peripherals.c', 'bench/integer_kernels.c', 'bench/integer_workloads.c',
                 'bench/coremark/core_portme.c', 'Drivers/SYSTEM/sys/sys.c',
                 'third_party/coremark/core_main.c', 'third_party/coremark/core_list_join.c',
                 'third_party/coremark/core_matrix.c', 'third_party/coremark/core_state.c',
                 'third_party/coremark/core_util.c')
    if ($Toolchain -ne 'armcc' -and !$GccBin) {
        $portable = Join-Path $PSScriptRoot '.tools/arm-gnu-toolchain-14.3.rel1-mingw-w64-i686-arm-none-eabi/bin'
        if (Test-Path "$portable/arm-none-eabi-gcc.exe") {
            $GccBin = $portable
        } else {
            $onPath = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
            if ($onPath) { $GccBin = Split-Path $onPath.Source }
        }
    }
    foreach ($family in @('ac6', 'gcc')) {
        if ($Toolchain -ne 'all' -and $Toolchain -ne $family) { continue }
        if ($family -eq 'ac6') {
            $compiler = Join-Path $Ac6Bin 'armclang.exe'
            $version = & $compiler --version
            if ($LASTEXITCODE -ne 0 -or ($version -join ' ') -notmatch 'Arm Compiler.*6\.\d+') {
                throw 'Expected Arm Compiler for Embedded 6.x.'
            }
            # -ffp-contract=off matches GCC -std=c99 and the former armcc --fpmode=std.
            $cpu = @('--target=arm-arm-none-eabi', '-mcpu=cortex-m4', '-mthumb',
                     '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard')
            $flags = $cpu + @('-std=c99', '-ffreestanding', '-ffunction-sections',
                              '-fdata-sections', '-ffp-contract=off', '-Wall')
        } else {
            if (!$GccBin) { throw 'Set -GccBin or F407_GCC_BIN to the Arm GNU Toolchain 14.3.Rel1 bin directory.' }
            $compiler = Join-Path $GccBin 'arm-none-eabi-gcc.exe'
            $version = & $compiler --version
            if ($LASTEXITCODE -ne 0 -or ($version -join ' ') -notmatch '14\.3\.Rel1') {
                throw 'Expected Arm GNU Toolchain 14.3.Rel1.'
            }
            $cpu = @('-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard')
            $flags = $cpu + @('-std=c99', '-ffreestanding', '-ffunction-sections',
                              '-fdata-sections', '-fno-lto', '-fno-fast-math', '-Wall', '-Wextra')
        }
        foreach ($profile in @('speed', 'size')) {
            $name = "$family-$profile"
            $output = "build/$name"
            New-Item -ItemType Directory -Path $output -Force | Out-Null
            Set-Content -LiteralPath "$output/commands.jsonl" -Value @() -Encoding utf8
            Set-Content -LiteralPath "$output/version.txt" -Value $version -Encoding utf8
            $defines = @('-DSTM32F407xx')
            if ($profile -eq 'size') { $defines += '-DBENCH_SIZE=1' }
            $buildSources = $sources
            if ($family -eq 'ac6') {
                $opt = @($(if ($profile -eq 'size') { '-Oz' } else { '-O2' }))
            } else {
                $opt = @($(if ($profile -eq 'size') { '-Os' } else { '-O2' }))
                $buildSources += "$device/Source/Templates/system_stm32f4xx.c"
                $defines += '-DHSE_VALUE=8000000u'
            }
            $objects = @()
            foreach ($source in $buildSources) {
                $obj = "$output/$([IO.Path]::GetFileNameWithoutExtension($source)).o"
                Invoke-Compiler $compiler ($flags + $opt + $includes + $defines + @('-g', '-c', $source, '-o', $obj))
                $objects += $obj
            }
            $startup = "$output/startup.o"
            $elf = "$output/$name.elf"
            $bin = "$output/$name.bin"
            $map = "$output/$name.map"
            $asm = "$output/$name.asm"
            if ($family -eq 'ac6') {
                # armasm (legacy syntax) predates armclang and does not take --target.
                Invoke-Compiler (Join-Path $Ac6Bin 'armasm.exe') (@('--cpu=Cortex-M4.fp', '--pd', '__MICROLIB SETA 1',
                    '-g', '-o', $startup, "$device/Source/Templates/arm/startup_stm32f407xx.s"))
                Invoke-Compiler (Join-Path $Ac6Bin 'armlink.exe') (@('--cpu=Cortex-M4.fp', '--library_type=microlib',
                    '--scatter=startup/flash.sct', '--map', '--symbols', '--xref',
                    '--info=sizes,totals,unused', "--list=$map", "--output=$elf", $startup) + $objects)
                $fromelf = Join-Path $Ac6Bin 'fromelf.exe'
                Invoke-Compiler $fromelf @('--bin', "--output=$bin", $elf)
                Invoke-Compiler $fromelf @('--text', '-c', "--output=$asm", $elf)
            } else {
                Invoke-Compiler $compiler ($cpu + @('-g', '-c', 'startup/startup_gcc.s', '-o', $startup))
                Invoke-Compiler $compiler ($cpu + @('-nostdlib', '-fno-lto', '-T', 'startup/flash.ld',
                    "-Wl,--gc-sections,-Map=$map,--cref", '-o', $elf, $startup) + $objects + @('-lgcc'))
                Invoke-Compiler (Join-Path $GccBin 'arm-none-eabi-objcopy.exe') @('-O', 'binary', $elf, $bin)
                Invoke-Compiler (Join-Path $GccBin 'arm-none-eabi-objdump.exe') @('-d', $elf) |
                    Set-Content -LiteralPath $asm -Encoding ascii
            }
            & node tools/verify-isa.cjs $asm "$output/isa.json"
            if ($LASTEXITCODE -ne 0) { throw "Hardware instruction check failed for $name" }
            Write-Host "Built $name"
        }
    }
} finally {
    Pop-Location
}
