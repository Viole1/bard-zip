<img src="resources/bardzip.png" alt="BardZip icon" width="96" align="right">

# BardZip

BardZip turns **any file into a poem** and back, byte for byte. Every two bytes become one three-line stanza built from four dictionaries of 16 words, so a ZIP archive can travel as a (slightly absurd) text file.

```text
BARDZIP-POEM 1 DICT 1

бледный протокол
оплакивает квитанцию.
четверг не наступил.
```

("pale protocol / mourns the receipt / Thursday has not come" — these three lines are the bytes `B` `Z` of the format signature.)

## Quick start

```text
bardzip encode photo.zip -o photo.poem
bardzip inspect photo.poem
bardzip verify  photo.poem
bardzip decode  photo.poem -o photo_restored.zip
```

Try the bundled example:

```text
bardzip decode examples\hello.poem -o hello.txt
```

| Command | What it does |
|---|---|
| `encode <input> -o <output>` | file → poem (`--compression none\|zstd\|auto`, default `auto`) |
| `decode <input> -o <output>` | poem → file (`--force` replaces an existing output) |
| `verify <input>` | checks every checksum, writes nothing |
| `inspect <input>` | prints the container header (does not check the payload) |
| `completion powershell` | prints a tab-completion script for PowerShell |
| `--help`, `--version` | usage and version |

## Things to know

- **Windows x64 only** for now.
- **A poem is about 54 times larger than the data.** A 16 MiB file becomes roughly 0.9 GB of text. `encode` therefore refuses inputs above **16 MiB** by default; raise it with `--max-input <bytes>` (`0` = no limit).
- `decode` and `verify` refuse poems that would restore more than **256 MiB** (`--max-output <bytes>`); the declared size is checked before anything is decompressed.
- **Existing files are never overwritten** unless you pass `--force`. Results are written to a temporary file and appear only when complete, and the output can never be the input itself.
- **This is not encryption.** Anyone can read the poem back into data.
- **CRC32 detects accidental damage only.** It does not authenticate the data: someone who edits a poem can recompute the checksums.
- **Works in pipelines:** `-` stands for standard input/output (`type f.zip | bardzip encode - -o - | bardzip decode - -o - > copy.zip`). A progress line is shown on a console; `--quiet` / `--verbose` change how much is printed; exit codes tell what went wrong (2 usage, 3 file/system, 4 bad data, 5 limit). Details: [docs/en/cli.md](docs/en/cli.md).
- Memory use stays small (about 5-12 MiB) regardless of file size.

## Build from source

Requirements: CMake 3.25+, a C++20 compiler (MSVC), Windows. zstd is downloaded by CMake automatically.

```text
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Visual Studio users can also use the presets in `CMakePresets.json` (`x64-debug`, `x64-release`).

## Documentation

Detailed documentation with diagrams, in English and Russian: [docs/README.md](docs/README.md) — overview, main flow, RAII, CRC32, compression, streaming and benchmarks. Release history: [CHANGELOG.md](CHANGELOG.md). Ideas and roadmap: [plan.md](plan.md).

## License

[MIT](LICENSE). Third-party components: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
