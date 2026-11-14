# Benchmarks

[Docs index](../README.md) · [Streaming](streaming.md) · [Compression](compression.md) · 🇷🇺 [Русская версия](../ru/benchmarks.md)

This page keeps the **measured baseline** of the single‑threaded streaming implementation. After every performance change (multithreading, faster stanza encoding, new codecs, encryption...) re‑run the same scenarios and add a new column/section so results can be compared.

## How to reproduce

```text
cmake --preset x64-release
cmake --build out/build/x64-release
powershell -ExecutionPolicy Bypass -File scripts\benchmark.ps1 -SizeMiB 100 -Data Random -Compression none
```

[scripts/benchmark.ps1](../../scripts/benchmark.ps1) generates the input file, runs `encode`, `verify` and `decode`, checks that the restored file is identical and prints Markdown rows (wall‑clock seconds, input throughput, peak working set). Parameters: `-Exe`, `-SizeMiB`, `-Data Random|Text`, `-Compression none|zstd|auto`.

To **compare several builds** (before/after an optimization) use [scripts/compare.ps1](../../scripts/compare.ps1): keep a Release binary of each variant and pass them as an ordered dictionary, e.g.
`$b = [ordered]@{ A = "a\bardzip.exe"; B = "b\bardzip.exe" }; .\scripts\compare.ps1 -Builds $b -SizeMiB 32 -Runs 5`.
It uses one shared input and one pre‑made poem, a warm‑up run, rotated run order, and reports median and range. Do not call this a cold‑SSD measurement: the file cache is warm.

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

## Optimization log

Same scenario every time: Release, 100 MiB random data, `--compression none` (poem 5,369 MiB). Throughput is input MiB/s.

| Step | Commit | encode s (MiB/s) | verify s (MiB/s) | decode s (MiB/s) | Peak memory MiB |
|---|---|---|---|---|---:|
| Baseline 1 | `fbf540a` + streaming | 20.8 (4.80) | 28.2 (3.55) | 25.7 (3.89) | 5.4 |
| 1. Line tables for all 256 byte values (`PoemEncoder` appends precomputed lines) | – | 14.3 (7.01) | 17.8 (5.63) | 18.5 (5.40) | 5.9 |

Step 1 gives encode **−31 %** time. verify/decode also dropped (−37 % / −28 %) although the decoder was not touched, so most of that is run‑to‑run noise (disk cache, background load): compare only encode for this step and repeat runs before drawing conclusions. The controlled A/B comparison below replaces this rough estimate.

### Controlled comparison of steps 1 and 2

Method ([scripts/compare.ps1](../../scripts/compare.ps1)): three Release binaries built from the same sources, one shared input and **one pre‑made poem** used by every build, 1 warm‑up run + 5 measured runs per build with the build order rotated, median and range reported. Results are for a **warm file cache** (not cold SSD reads). Before measuring, the script checks that every build's encode output is byte‑identical to the shared poem and that every build's decode restores the input.

| Build | Change |
|---|---|
| A | before encoding tables (a `std::string` per stanza) |
| B | encoding tables (step 1) |
| C | encoding tables + decoding tables (step 2: `unordered_map<std::string, uint8_t>` of all 256 first lines and all 256 second lines) |

32 MiB random input, `--compression none` (poem 1,718 MiB):

| Command | Build | Median s | Min–max s | MiB/s | Peak memory MiB |
|---|---|---:|---|---:|---:|
| encode | A | 6.21 | 6.11–6.65 | 5.15 | 6.0 |
| encode | B | 2.50 | 2.42–5.31 | 12.78 | 5.0 |
| encode | C | 2.52 | 2.44–3.25 | 12.72 | 6.0 |
| verify | A | 6.40 | 6.18–6.49 | 5.00 | 6.0 |
| verify | B | 6.54 | 6.33–6.62 | 4.89 | 5.0 |
| verify | C | **5.84** | 5.73–5.97 | 5.48 | 5.0 |
| decode | A | 6.44 | 6.27–6.56 | 4.97 | 5.0 |
| decode | B | 6.53 | 6.51–6.70 | 4.90 | 5.0 |
| decode | C | **5.82** | 5.72–5.88 | 5.50 | 5.0 |

Same comparison of A and B for encode at 100 MiB (poem 5,369 MiB, 2 runs): A 18.72 s (5.34 MiB/s), B 12.79 s (7.82 MiB/s).

Conclusions:

- **Step 1 (encoding tables)**: encode is **2.5× faster** (−60 %) on 32 MiB. With 100 MiB it is only −32 %, because the 5.4 GB output then hits the limits of disk writes: B is no longer CPU‑bound there. Build A was CPU‑bound (~290 MiB/s of poem text), B reaches ~420 MiB/s of disk writes.
- **Step 2 (decoding tables)**: verify and decode are **~11 % faster** (6.54 → 5.84 s and 6.53 → 5.82 s); the min–max ranges of B and C do not overlap, so the gain is stable but modest. The decoder was not the main cost: most of the time goes elsewhere (see ideas below). Decision: **keep** the tables.
- Encode C = encode B (the decoder is not involved) and the peak memory is the same (5–6 MiB) in all builds.
- The earlier rough numbers in the table above (made with [scripts/benchmark.ps1](../../scripts/benchmark.ps1), 100 MiB, one run) are not directly comparable with these medians.

### Step 3 — reuse the stanza strings in the decoder

Change (build D): `StanzaReader` keeps one `Stanza` as a member and reads the three lines into it, so the capacity of the strings is reused instead of allocating three new strings per stanza. Hash tables, CRC32 and block sizes were not changed. Builds C and D, same method as above, 32 MiB random input, 5 runs after warm-up:

| Command | Build | Median s | Min–max s | MiB/s | Peak memory MiB |
|---|---|---:|---|---:|---:|
| verify | C | 5.73 | 5.49–9.19 | 5.58 | 5.0 |
| verify | D | **4.06** | 3.64–4.13 | 7.88 | 5.0 |
| decode | C | 5.82 | 5.58–5.90 | 5.50 | 5.0 |
| decode | D | **3.85** | 3.74–4.30 | 8.32 | 5.0 |

verify is **−29 %** and decode **−34 %**; the ranges do not overlap and memory is unchanged. Decision: **keep**. This confirms the suspicion that string allocation, not the dictionary search, was the main decoder cost. Decoding is now about 8 MiB/s, still behind encode (~12.8 MiB/s on 32 MiB).

## Ideas to test against this baseline

| Idea | Expected effect |
|---|---|
| ~~Avoid per‑stanza `std::string` allocations in `PoemEncoder`~~ **done (step 1)** | encode ↑ |
| ~~Replace linear dictionary search with a hash map~~ **done (step 2, +11 %)**; next: look up `string_view` keys / perfect hash | decode / verify ↑ |
| ~~`LineReader` / `decode_poem_stream` create a `std::string` per line~~ **partly done (step 3, −29 % / −34 %)**: strings are reused; reading lines as `string_view` could remove the remaining copies | decode / verify ↑ |
| Table‑driven (slice‑by‑8) CRC32 instead of bit‑by‑bit | all commands ↑ (CRC runs 2–3 times per byte) |
| Pipeline in threads: read + CRC / compress / poem formatting / write | encode ↑ up to the slowest stage |
| Parallel zstd (`ZSTD_c_nbWorkers`) | encode of compressible data ↑ |
| Avoid the scratch payload file for decode when the payload is small | decode ↓ disk I/O |
| New codecs / encryption | measure the cost per MiB against these rows |

When you add rows, keep the old ones and write the commit hash and what changed, e.g. `| Release | random | none | 100 | encode | 9.1 | 10.99 | ... | table CRC32 (abc1234) |`.
