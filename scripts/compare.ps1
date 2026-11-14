<#
.SYNOPSIS
    Compares several bardzip builds on the same input: warm-up run, then N
    measured runs with the build order rotated, reporting median and range.

.EXAMPLE
    .\scripts\compare.ps1 -Builds @{ A = "a\bardzip.exe"; B = "b\bardzip.exe" } -SizeMiB 32

.NOTES
    * The input file and the poem are created once and shared by every build
      (the poem is made by the first build), so all builds decode the same text.
    * Measurements are made with a warm file cache. They are not cold SSD reads.
    * encode: poems of all builds are compared by hash after the warm-up run.
      decode: restored files are compared with the input after the warm-up run.
#>
param(
    [Parameter(Mandatory)]
    [System.Collections.Specialized.OrderedDictionary]$Builds,
    [int]$SizeMiB = 32,
    [int]$Runs = 5,
    [ValidateSet("encode", "verify", "decode")]
    [string[]]$Commands = @("encode", "verify", "decode"),
    [string]$WorkDir = (Join-Path $env:TEMP "bardzip-compare")
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force $WorkDir | Out-Null
$inputFile = Join-Path $WorkDir "input.bin"
$sharedPoem = Join-Path $WorkDir "shared.poem"
$names = @($Builds.Keys)

if (-not (Test-Path $inputFile)) {
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $block = New-Object byte[] (1MB)
    $stream = [IO.File]::Create($inputFile)
    for ($i = 0; $i -lt $SizeMiB; $i++) { $rng.GetBytes($block); $stream.Write($block, 0, $block.Length) }
    $stream.Dispose()
}

function Invoke-Bardzip([string]$exe, [string[]]$arguments) {
    $psi = New-Object Diagnostics.ProcessStartInfo $exe
    $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($psi)
    $null = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()

    $peak = 0
    while (-not $process.WaitForExit(20)) {
        try { $process.Refresh(); $peak = [Math]::Max($peak, $process.PeakWorkingSet64) } catch { }
    }
    $timer.Stop()

    if ($process.ExitCode -ne 0) {
        throw "$exe $($arguments -join ' ') failed: $($errors.Result)"
    }
    [pscustomobject]@{ Seconds = $timer.Elapsed.TotalSeconds; PeakMiB = $peak / 1MB }
}

function Get-Median([double[]]$values) {
    $sorted = $values | Sort-Object
    $mid = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { $sorted[$mid] } else { ($sorted[$mid - 1] + $sorted[$mid]) / 2 }
}

function Get-Arguments([string]$command, [string]$name) {
    $output = Join-Path $WorkDir "$name.$command.out"
    Remove-Item $output -Force -ErrorAction SilentlyContinue
    switch ($command) {
        "encode" { @("encode", $inputFile, "-o", $output, "--compression", "none", "--max-input", "0") }
        "verify" { @("verify", $sharedPoem) }
        "decode" { @("decode", $sharedPoem, "-o", $output) }
    }
}

# Shared poem, produced by the first build.
if (-not (Test-Path $sharedPoem)) {
    $null = Invoke-Bardzip ($Builds[$names[0]]) @("encode", $inputFile, "-o", $sharedPoem, "--compression", "none", "--max-input", "0")
}

$inputHash = (Get-FileHash $inputFile).Hash
$sharedHash = (Get-FileHash $sharedPoem).Hash
$results = @()

foreach ($command in $Commands) {
    $times = @{}; $peaks = @{}
    foreach ($name in $names) { $times[$name] = @(); $peaks[$name] = 0 }

    # Warm-up (also the correctness check).
    foreach ($name in $names) {
        $arguments = Get-Arguments $command $name
        $null = Invoke-Bardzip ($Builds[$name]) $arguments
        $output = Join-Path $WorkDir "$name.$command.out"
        if ($command -eq "decode" -and (Get-FileHash $output).Hash -ne $inputHash) { throw "$name decode: restored file differs from input" }
        if ($command -eq "encode" -and (Get-FileHash $output).Hash -ne $sharedHash) { throw "$name encode: poem differs from the shared poem" }
        Remove-Item $output -Force -ErrorAction SilentlyContinue
    }

    for ($run = 0; $run -lt $Runs; $run++) {
        # Rotate the order so no build always runs first or last.
        $shift = $run % $names.Count
        $order = @($names[$shift..($names.Count - 1)])
        if ($shift -gt 0) { $order += @($names[0..($shift - 1)]) }
        foreach ($name in $order) {
            $arguments = Get-Arguments $command $name
            $m = Invoke-Bardzip ($Builds[$name]) $arguments
            $times[$name] += $m.Seconds
            $peaks[$name] = [Math]::Max($peaks[$name], $m.PeakMiB)
            Remove-Item (Join-Path $WorkDir "$name.$command.out") -Force -ErrorAction SilentlyContinue
        }
    }

    foreach ($name in $names) {
        $median = Get-Median $times[$name]
        $results += [pscustomobject]@{
            Command = $command; Build = $name; Median = $median
            Min = ($times[$name] | Measure-Object -Minimum).Minimum
            Max = ($times[$name] | Measure-Object -Maximum).Maximum
            MiBs = $SizeMiB / $median; Peak = $peaks[$name]
        }
    }
}

"Input: $SizeMiB MiB random, compression none, $Runs runs after 1 warm-up, warm file cache."
"| Command | Build | Median s | Min s | Max s | MiB/s (median) | Peak memory MiB |"
"|---|---|---:|---:|---:|---:|---:|"
foreach ($r in $results) {
    "| {0} | {1} | {2:N2} | {3:N2} | {4:N2} | {5:N2} | {6:N1} |" -f $r.Command, $r.Build, $r.Median, $r.Min, $r.Max, $r.MiBs, $r.Peak
}

Remove-Item $WorkDir -Recurse -Force
