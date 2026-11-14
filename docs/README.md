# BardZip documentation / Документация BardZip

[Project README](../README.md) · [Changelog](../CHANGELOG.md) · [plan](../plan.md)

BardZip turns bytes into poetry.

BardZip превращает байты в поэзию.

| Topic | English | Русский |
|---|---|---|
| Project overview, architecture, formats, CLI | [docs/en/overview.md](en/overview.md) | [docs/ru/overview.md](ru/overview.md) |
| Main flow (encode / decode / verify / inspect) | [docs/en/main-flow.md](en/main-flow.md) | [docs/ru/main-flow.md](ru/main-flow.md) |
| RAII (automatic cleanup) | [docs/en/raii.md](en/raii.md) | [docs/ru/raii.md](ru/raii.md) |
| CRC32 (checksums) | [docs/en/crc32.md](en/crc32.md) | [docs/ru/crc32.md](ru/crc32.md) |
| Compression (zstd, `auto` mode) | [docs/en/compression.md](en/compression.md) | [docs/ru/compression.md](ru/compression.md) |
| Streaming (constant memory, 64 KiB blocks) | [docs/en/streaming.md](en/streaming.md) | [docs/ru/streaming.md](ru/streaming.md) |
| Benchmarks (baseline to compare future optimizations) | [docs/en/benchmarks.md](en/benchmarks.md) | [docs/ru/benchmarks.md](ru/benchmarks.md) |
| Fuzzing (robustness against broken and hostile files) | [docs/en/fuzzing.md](en/fuzzing.md) | [docs/ru/fuzzing.md](ru/fuzzing.md) |

Suggested reading order: overview → main flow → the rest in any order.

Рекомендуемый порядок: обзор → основной поток → остальное в любом порядке.

> Diagrams are written in [Mermaid](https://mermaid.js.org/) and render on GitHub, in VS Code (Markdown Preview Mermaid Support) and in most modern Markdown viewers.

> Диаграммы написаны на Mermaid и отображаются на GitHub, в VS Code и в большинстве современных Markdown-просмотрщиков.
