<#
.SYNOPSIS
    Repeatable BardZip benchmark: encode, verify and decode of a generated file.

.EXAMPLE
    .\scripts\benchmark.ps1 -Exe out\build\x64-release\bardzip.exe -SizeMiB 32 -Data Random -Compression none

.NOTES
    Prints one Markdown table row per command, ready to paste into docs/benchmarks.md.
    Time is wall-clock seconds; memory is the peak working set of the bardzip process.
    Run it on a quiet machine and compare only runs made with the same build type.
#>
param(
    [string]$Exe = "out\build\x64-release\bardzip.exe",
    [int]$SizeMiB = 32,
    [ValidateSet("Random", "Text")]
    [string]$Data = "Random",
    [ValidateSet("none", "zstd", "auto")]
    [string]$Compression = "auto",
    [string]$WorkDir = (Join-Path $env:TEMP "bardzip-benchmark")
)

$ErrorActionPreference = "Stop"

$exePath = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$sourceFile = Join-Path $WorkDir "input.bin"
$poem = Join-Path $WorkDir "input.poem"
$restored = Join-Path $WorkDir "restored.bin"
Remove-Item $sourceFile, $poem, $restored -Force -ErrorAction SilentlyContinue

function New-TestFile([string]$path, [int]$sizeMiB, [string]$kind) {
    $stream = [IO.File]::Create($path)
    try {
        if ($kind -eq "Random") {
            $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
            $block = New-Object byte[] (1MB)
            for ($i = 0; $i -lt $sizeMiB; $i++) {
                $rng.GetBytes($block)
                $stream.Write($block, 0, $block.Length)
            }
        }
        else {
            # Repetitive text: compresses well, like source code or logs.
            $words = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda".Split(" ")
            $random = New-Object System.Random 42
            $text = New-Object System.Text.StringBuilder
            while ($text.Length -lt 1MB) {
                [void]$text.Append($words[$random.Next($words.Length)]).Append(" ")
            }
            $block = [Text.Encoding]::ASCII.GetBytes($text.ToString().Substring(0, 1MB))
            for ($i = 0; $i -lt $sizeMiB; $i++) {
                $stream.Write($block, 0, $block.Length)
            }
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Measure-CommandLine([string[]]$arguments) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process $exePath -ArgumentList $arguments -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $WorkDir "stdout.txt") `
        -RedirectStandardError (Join-Path $WorkDir "stderr.txt")

    # Touching Handle keeps the exit code available after the process exits.
    $null = $process.Handle

    $peak = 0
    while (-not $process.HasExited) {
        try {
            $process.Refresh()
            $peak = [Math]::Max($peak, $process.PeakWorkingSet64)
        }
        catch { }
        Start-Sleep -Milliseconds 100
    }
    $process.WaitForExit()
    $timer.Stop()

    if ($process.ExitCode -ne 0) {
        throw "bardzip $($arguments -join ' ') failed: $(Get-Content (Join-Path $WorkDir 'stderr.txt') -Raw)"
    }

    [pscustomobject]@{ Seconds = $timer.Elapsed.TotalSeconds; PeakMiB = $peak / 1MB }
}

New-TestFile $sourceFile $SizeMiB $Data

$build = if ($exePath -match "release") { "Release" } else { "Debug" }
$inputBytes = (Get-Item $sourceFile).Length

# --max-input 0: benchmarks may exceed the default 32 MiB input limit.
$encode = Measure-CommandLine @("encode", $sourceFile, "-o", $poem, "--compression", $Compression, "--max-input", "0")
$poemBytes = (Get-Item $poem).Length
$verify = Measure-CommandLine @("verify", $poem)
$decode = Measure-CommandLine @("decode", $poem, "-o", $restored)

if ((Get-FileHash $sourceFile).Hash -ne (Get-FileHash $restored).Hash) {
    throw "Round trip FAILED: restored file differs from the input"
}

$mib = $inputBytes / 1MB
"| Build | Data | Compression | Input MiB | Poem MiB | Command | Seconds | MiB/s (input) | Peak memory MiB |"
"|---|---|---|---:|---:|---|---:|---:|---:|"
foreach ($row in @(@("encode", $encode), @("verify", $verify), @("decode", $decode))) {
    $r = $row[1]
    "| {0} | {1} | {2} | {3:N0} | {4:N0} | {5} | {6:N1} | {7:N2} | {8:N1} |" -f `
        $build, $Data.ToLower(), $Compression, $mib, ($poemBytes / 1MB), $row[0], $r.Seconds, ($mib / $r.Seconds), $r.PeakMiB
}
"Round trip: OK (hashes match). Poem is {0:N1}x the input." -f ($poemBytes / $inputBytes)

Remove-Item $WorkDir -Recurse -Force
