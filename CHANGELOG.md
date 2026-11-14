# Changelog

All notable changes to BardZip are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/) (while the version is 0.x, minor releases may still change behaviour).

## [0.1.1]

### Added

- The decoder accepts a UTF-8 byte order mark at the start of a poem (older Notepad adds one when a poem is re-saved). The "Invalid poem header" message now says the file must be the unchanged UTF-8 text produced by `bardzip encode`.
- A decode error for a poem larger than `--max-output` now suggests the option; `encode` prints a note when the original is larger than the default decode limit.
- Developer tools: stage profiler (`-DBARDZIP_BUILD_TOOLS=ON`), libFuzzer targets (`tools/fuzz`, `scripts/fuzz.ps1`), `scripts/compare.ps1` for A/B benchmarks; mutation fuzz tests in the normal test suite.

### Changed

- CRC32 uses slice-by-8 tables (about 20 times faster; identical results).
- Poem decoding is about **1.9 times faster** in total (`verify` and `decode` on 32 MiB: 3.7 s → 1.9 s): CRC32 tables, a cheaper line lookup table, and a background read-ahead thread for the poem file. Peak memory of `decode`/`verify` grows by about 2 MiB (5 → 7 MiB). `encode` is unchanged.

### Security

- The zstd decoder refuses frames that request a window larger than 16 MiB (BardZip's own encoder never uses more than 2 MiB), bounding memory use on hostile input.

## [0.1.0]

First public release.

### Added

- `encode`, `decode`, `verify` and `inspect` commands; `--help` and `--version`.
- Poem format version 1: every two bytes become a three-line stanza (`BARDZIP-POEM 1 DICT 1`) inside a `BZP1` container with a 36-byte header and CRC32 checksums for the header, the payload and the original data.
- Optional zstd compression (`--compression none|zstd|auto`, default `auto`: never makes the payload larger).
- Streaming processing in 64 KiB blocks: memory use does not depend on the file size (about 5-12 MiB measured).
- Safe output: results are written to a temporary file and published only when complete; existing files are never overwritten; temporary files are removed on every error.
- Limits: `--max-input` (default 16 MiB, `0` = unlimited) and `--max-output` (default 256 MiB); a poem line may not exceed 4096 bytes.
- Golden poems in the test suite that freeze the format, and an example poem (`examples/hello.poem`).

### Known limitations

- Windows x64 only.
- A poem is about 54 times larger than the data it carries.
- This is not encryption, and CRC32 detects accidental damage only; it does not authenticate the data.
- Single-threaded.
