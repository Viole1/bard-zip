# Benchmarks

[Docs index](../../README.md) · [Streaming](streaming.md) · [Compression](compression.md) · 🇷🇺 [Русская версия](../ru/benchmarks.md)

This page keeps the **measured baseline** of the single‑threaded streaming implementation. After every performance change (multithreading, faster stanza encoding, new codecs, encryption...) re‑run the same scenarios and add a new column/section so results can be compared.

## How to reproduce

```text
cmake --preset x64-release
cmake --build out/build/x64-release
powershell -ExecutionPolicy Bypass -File scripts\benchmark.ps1 -SizeMiB 100 -Data Random -Compression none
```

[scripts/benchmark.ps1](../../scripts/benchmark.ps1) generates the input file, runs `encode`, `verify` and `decode`, checks that the restored file is identical and prints Markdown rows (wall‑clock seconds, input throughput, peak working set). Parameters: `-Exe`, `-SizeMiB`, `-Data Random|Text`, `-Compression none|zstd|auto`.

Rules for fair comparison: same machine, quiet system, same build type (Release), same scenario. Debug numbers are only useful against other Debug numbers.

Benchmarks pass `--max-input 0` because the default input limit is 16 MiB ([overview](overview.md)).

## Test machine (baseline)

| | |
|---|---|
| CPU | Intel Core i7‑12800H (20 logical cores; the code is single‑threaded) |
| RAM / disk | 32 GB / SSD |
| OS / compiler | Windows, MSVC (Visual Studio 18), C++20 |
| zstd | 1.5.7, level 3 |
| Commit | `fbf540a` + streaming implementation |

## Baseline 1 — Release (the number to beat)

Throughput is input MiB per second.

| Data | Compression | Input MiB | Poem MiB | Command | Seconds | MiB/s | Peak memory MiB |
|---|---|---:|---:|---|---:|---:|---:|
| random | none | 100 | 5,369 | encode | 20.8 | 4.80 | 5.4 |
| random | none | 100 | 5,369 | verify | 28.2 | 3.55 | 5.4 |
| random | none | 100 | 5,369 | decode | 25.7 | 3.89 | 5.4 |
| random | auto (→ stored) | 32 | 1,718 | encode | 7.0 | 4.60 | 8.8 |
| random | auto (→ stored) | 32 | 1,718 | verify | 6.0 | 5.31 | 5.4 |
| random | auto (→ stored) | 32 | 1,718 | decode | 6.8 | 4.72 | 5.4 |
| text (very repetitive) | zstd | 32 | 12 | encode | 0.5 | 65.50 | 8.6 |
| text (very repetitive) | zstd | 32 | 12 | verify | 0.5 | 67.64 | 7.8 |
| text (very repetitive) | zstd | 32 | 12 | decode | 0.6 | 51.72 | 7.8 |

## Baseline 2 — Debug, 100 MiB random, `none`

Measured once, right after streaming was implemented (the first stress test).

| Command | Seconds | MiB/s | Peak memory MiB |
|---|---:|---:|---:|
| encode | 362.5 | 0.28 | 12.0 |
| verify | 292.2 | 0.34 | 7.9 |
| decode | 221.9 | 0.45 | 7.9 |

Poem size: 5,629,547,537 bytes. Release is roughly **15–17× faster** than Debug.

## What the numbers say

- **Memory is constant**: 5–12 MiB regardless of input size (the previous in‑memory version would need many GB for the same file).
- **The poem is ≈ 53.7× the stored data** (random data, no compression): ≈ 54 bytes of UTF‑8 per data byte. This ratio is what the `--max-input` safety estimate uses.
- **Incompressible data is limited by text generation and disk writes**, not by CRC32 or zstd: ~4–5 MiB/s of input means ~250 MiB/s of poem text.
- **Compressible data is fast** because the poem is tiny (here 12 MiB for 32 MiB of input); the cost is dominated by zstd and CRC32.
- `verify` / `decode` are not faster than `encode` although they do less "creative" work: they parse each stanza from text (three lines, dictionary lookups with linear search) and write a scratch payload file.

## Ideas to test against this baseline

| Idea | Expected effect |
|---|---|
| Avoid per‑stanza `std::string` allocations in `PoemEncoder` (append words straight into the buffer) | encode ↑ |
| Replace linear dictionary search with a hash map / perfect hash in `decode_byte` | decode / verify ↑ |
| Table‑driven (slice‑by‑8) CRC32 instead of bit‑by‑bit | all commands ↑ (CRC runs 2–3 times per byte) |
| Pipeline in threads: read + CRC / compress / poem formatting / write | encode ↑ up to the slowest stage |
| Parallel zstd (`ZSTD_c_nbWorkers`) | encode of compressible data ↑ |
| Avoid the scratch payload file for decode when the payload is small | decode ↓ disk I/O |
| New codecs / encryption | measure the cost per MiB against these rows |

When you add rows, keep the old ones and write the commit hash and what changed, e.g. `| Release | random | none | 100 | encode | 9.1 | 10.99 | ... | table CRC32 (abc1234) |`.
