# BardZip

BardZip turns any file into a Russian poem and back, byte for byte.

This archive contains the Windows x64 command-line tool. No installation and no Visual C++ redistributable are needed: unpack it anywhere and run `bardzip.exe` from a terminal.

## Try it

```text
bardzip --version
bardzip decode examples\hello.poem -o hello.txt
type hello.txt
```

Your own files:

```text
bardzip encode my-archive.zip -o my-archive.poem
bardzip verify my-archive.poem
bardzip decode my-archive.poem -o restored.zip
```

Run `bardzip --help` for all commands and options.

## Verify the download

Next to the archive there is a `.sha256` file. In PowerShell:

```text
Get-FileHash bardzip-v<version>-windows-x64.zip -Algorithm SHA256
```

The hash must match the one in the `.sha256` file.

## Limitations

- **Windows x64 only.**
- **A poem is about 54 times larger than the data.** `encode` refuses inputs above 16 MiB by default (`--max-input <bytes>` raises the limit, `0` removes it). Make sure you have enough disk space first.
- `decode` and `verify` refuse poems that would restore more than 256 MiB (`--max-output <bytes>`).
- **This is not encryption.** Anyone can turn a poem back into data.
- **CRC32 detects accidental damage only.** It does not authenticate the data; a deliberately edited poem can carry matching checksums.
- Existing output files are never overwritten.

## More

Documentation, source code and issue tracker: https://github.com/Viole1/bard-zip

Files in this archive: `bardzip.exe`, `README.md`, `LICENSE` (MIT), `THIRD-PARTY-NOTICES.md` (zstd), `CHANGELOG.md`, `examples\hello.poem`.
