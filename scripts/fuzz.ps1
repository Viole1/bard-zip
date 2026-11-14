<#
.SYNOPSIS
    Runs the libFuzzer targets for a fixed time each and reports crashes.

.EXAMPLE
    # from a Visual Studio developer prompt (the ASan runtime must be on PATH)
    .\scripts\fuzz.ps1 -BuildDir out\build\fuzz -Seconds 120

.NOTES
    Build the targets first, see docs/en/fuzzing.md. Corpora and crash files
    are stored in <BuildDir>\corpus_<target> and <BuildDir>\artifacts.
    The exit code is non-zero if any target crashed, timed out or ran out of memory.
#>
param(
    [Parameter(Mandatory)]
    [string]$BuildDir,
    [int]$Seconds = 60,
    [string]$Fixtures
)

$ErrorActionPreference = "Stop"
if (-not $Fixtures) { $Fixtures = Join-Path (Split-Path -Parent $PSScriptRoot) "tests\fixtures" }
$BuildDir = (Resolve-Path $BuildDir).Path
$artifacts = Join-Path $BuildDir "artifacts"
New-Item -ItemType Directory -Force $artifacts | Out-Null

$targets = @(
    @{ Name = "poem";    MaxLen = 4096; Seed = $true  },
    @{ Name = "header";  MaxLen = 64;   Seed = $false },
    @{ Name = "zstd";    MaxLen = 4096; Seed = $false },
    @{ Name = "extract"; MaxLen = 4096; Seed = $false }
)

$failed = $false

foreach ($target in $targets) {
    $name = $target.Name
    $corpus = Join-Path $BuildDir "corpus_$name"
    New-Item -ItemType Directory -Force $corpus | Out-Null

    if ($target.Seed) {
        Copy-Item (Join-Path $Fixtures "*.poem") $corpus -Force
    }

    $exe = Join-Path $BuildDir "fuzz_$name.exe"
    $log = Join-Path $BuildDir "fuzz_$name.log"

    # cmd redirects stderr itself: libFuzzer prints harmless notes to stderr
    # that PowerShell would turn into errors.
    $arguments = "`"$corpus`" -max_total_time=$Seconds -max_len=$($target.MaxLen) -timeout=10 " +
        "-rss_limit_mb=1024 -print_final_stats=1 `"-artifact_prefix=$artifacts\$name-`""
    cmd /c "`"$exe`" $arguments > `"$log`" 2>&1"
    $code = $LASTEXITCODE

    $stats = Select-String -Path $log -Pattern "stat::number_of_executed_units|stat::average_exec_per_sec" |
        ForEach-Object { ($_.Line -split ":\s*")[-1] }
    $covLine = Select-String -Path $log -Pattern "cov: (\d+)" | Select-Object -Last 1
    $cov = if ($covLine) { $covLine.Matches[0].Groups[1].Value } else { "n/a" }

    "{0,-8} exit {1}  executions {2}  exec/s {3}  coverage {4}" -f $name, $code, $stats[0], $stats[1], $cov

    if ($code -ne 0) {
        $failed = $true
        "  FAILED, see $log"
        Get-Content $log | Select-String "ERROR|SUMMARY|crash-|timeout-|oom-" | Select-Object -First 5 | ForEach-Object { "  " + $_.Line }
    }
}

if ($failed) { exit 1 }
"All fuzz targets finished without findings."
