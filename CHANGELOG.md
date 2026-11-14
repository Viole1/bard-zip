# Changelog

All notable changes to BardZip are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/) (while the version is 0.x, minor releases may still change behaviour).

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
