<#
.SYNOPSIS
    Builds the Windows release archive: bardzip-v<version>-windows-x64.zip,
    its SHA-256 file and the release notes taken from CHANGELOG.md.

.EXAMPLE
    .\scripts\package.ps1 -Exe out\build\x64-release\bardzip.exe

.NOTES
    Run it on a Release build made with the static runtime (the default).
    The script unpacks the finished archive into a clean folder and checks
    that bardzip.exe starts, reports the right version and decodes the example.
#>
param(
    [string]$Exe = "out\build\x64-release\bardzip.exe",
    [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root

try {
    $exePath = (Resolve-Path $Exe).Path

    $cmake = Get-Content CMakeLists.txt -Raw
    if ($cmake -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw "Cannot find the project version in CMakeLists.txt" }
    $version = $Matches[1]

    $reported = (& $exePath --version).Trim()
    if ($reported -ne "bardzip $version") {
        throw "Executable reports '$reported' but CMakeLists.txt says $version. Rebuild first."
    }

    $name = "bardzip-v$version-windows-x64"
    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $outPath = (Resolve-Path $OutDir).Path
    $stage = Join-Path $outPath $name
    $zip = Join-Path $outPath "$name.zip"
    Remove-Item $stage, $zip, "$zip.sha256" -Recurse -Force -ErrorAction SilentlyContinue

    New-Item -ItemType Directory $stage, (Join-Path $stage "examples") | Out-Null
    Copy-Item $exePath (Join-Path $stage "bardzip.exe")
    Copy-Item release\README.md (Join-Path $stage "README.md")
    Copy-Item LICENSE, THIRD-PARTY-NOTICES.md, CHANGELOG.md $stage
    Copy-Item examples\hello.poem (Join-Path $stage "examples")

    # Entries are added by hand so their names use "/" (Compress-Archive in
    # Windows PowerShell 5.1 writes "\", which some unzip tools mishandle).
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem $stage -Recurse -File) {
            $entryName = $file.FullName.Substring($stage.Length + 1).Replace("\", "/")
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal)
        }
    }
    finally {
        $archive.Dispose()
    }

    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText("$zip.sha256", "$hash *$name.zip`n")

    # Release notes: the CHANGELOG section of this version.
    $changelog = Get-Content CHANGELOG.md -Raw
    $pattern = "(?ms)^## \[$([regex]::Escape($version))\]\s*\r?\n(.*?)(?=^## \[|\z)"
    if ($changelog -notmatch $pattern) { throw "CHANGELOG.md has no section for $version" }
    $notes = $Matches[1].Trim() + "`n`n**SHA-256** of ``$name.zip``: ``$hash```n"
    [IO.File]::WriteAllText((Join-Path $outPath "release-notes.md"), $notes)

    # Smoke test of the finished archive in a clean folder.
    $check = Join-Path $env:TEMP "bardzip-package-check"
    Remove-Item $check -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive $zip $check
    $tool = Join-Path $check "bardzip.exe"

    if ((& $tool --version).Trim() -ne "bardzip $version") { throw "Smoke test: wrong version" }
    & $tool decode (Join-Path $check "examples\hello.poem") -o (Join-Path $check "hello.txt")
    if ($LASTEXITCODE -ne 0) { throw "Smoke test: decoding the example failed" }
    if (-not (Get-Content (Join-Path $check "hello.txt") -Raw).StartsWith("Hello, BardZip!")) { throw "Smoke test: unexpected example content" }
    Remove-Item $check -Recurse -Force
    Remove-Item $stage -Recurse -Force

    "Created $zip"
    "SHA-256: $hash"
    "Release notes: $(Join-Path $outPath 'release-notes.md')"
}
finally {
    Pop-Location
}
