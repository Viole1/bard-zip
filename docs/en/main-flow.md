# Main flow

[Docs index](../README.md) · [Overview](overview.md) · [RAII](raii.md) · [CRC32](crc32.md) · [Compression](compression.md) · 🇷🇺 [Русская версия](../ru/main-flow.md)

All four commands live in [file_operations.cpp](../../src/core/file_operations.cpp). Each one is a short pipeline of steps; every step is a separate, testable function.

## 0. Program start

[main.cpp](../../src/main.cpp): on Windows it switches the console to UTF‑8 (so Russian text prints correctly), parses arguments, runs the chosen command, and turns any exception into `Error: <message>` with exit code `1`.

```mermaid
flowchart TD
    A["bardzip.exe args..."] --> B["Set console to UTF-8"]
    B --> C["parse_arguments"]
    C -->|invalid| E["Error: ... → exit 1"]
    C --> D{"command?"}
    D -->|encode| EN["encode_file → print report"]
    D -->|decode| DE["decode_file"]
    D -->|inspect| IN["inspect_file"]
    D -->|verify| VE["verify_file"]
    EN --> OK["exit 0"]
    DE --> OK
    IN --> OK
    VE --> OK
    EN -.exception.-> E
    DE -.exception.-> E
    IN -.exception.-> E
    VE -.exception.-> E
```

## 1. Encode: file → poem

```mermaid
flowchart LR
    A["read_binary_file<br/>(≤ 64 MiB)"] --> B["compress<br/>none / zstd / auto"]
    B --> C["header: sizes + CRC32s<br/>+ header CRC32"]
    C --> D["container =<br/>header + payload"]
    D --> E["encode_poem<br/>2 bytes → 1 stanza"]
    E --> F["write_text_file<br/>(temp file → rename)"]
```

```mermaid
sequenceDiagram
    actor U as User
    participant FO as file_operations
    participant CT as container
    participant CP as compression
    participant PC as poem_codec
    participant IO as file_writer / TemporaryFile
    U->>FO: encode_file(in, out, mode)
    FO->>FO: read_binary_file(in)
    FO->>CT: build_container(original, mode)
    CT->>CP: compress(original, mode)
    CP-->>CT: method + payload bytes
    CT->>CT: crc32(original), crc32(payload)
    CT->>CT: serialize_header (adds header CRC)
    CT-->>FO: header + payload
    FO->>PC: encode_poem(container)
    PC-->>FO: poem text
    FO->>IO: write_text_file(out, poem)
    IO-->>U: file appears only when complete
    FO-->>U: report (sizes, stanzas)
```

Details:
- Odd container length → one `0x00` byte is appended for the last stanza.
- The report printed to stderr: method, original / payload / container / poem sizes, stanza count.

## 2. Decode: poem → file

```mermaid
flowchart LR
    A["read_text_file"] --> B["decode_poem<br/>text → bytes"]
    B --> C["extract_container<br/>checks + decompress"]
    C --> D["write_binary_file<br/>(temp file → rename)"]
```

### Inside `decode_poem`

```mermaid
flowchart TD
    S["poem text"] --> H{"first non-empty line<br/>== BARDZIP-POEM 1 DICT 1 ?"}
    H -->|no| X["error"]
    H -->|yes| L["collect non-empty lines<br/>(CRLF tolerated)"]
    L --> M{"line count % 3 == 0 ?"}
    M -->|no| X
    M -->|yes| ST["each stanza: refrain ok?<br/>period ok? words known?"]
    ST -->|fail| X2["error: 'Failed to decode stanza N'"]
    ST --> HD["bytes ≥ 36?<br/>parse_header (magic, CRC, version...)"]
    HD --> LEN{"stanza count matches<br/>36 + payload_size?"}
    LEN -->|no| X
    LEN --> PAD{"odd length → last byte is 0 padding?"}
    PAD -->|no| X
    PAD -->|yes| R["container bytes"]
```

### Inside `extract_container`

```mermaid
flowchart TD
    A["container bytes"] --> B["parse_header<br/>(magic, header CRC, fields)"]
    B --> C{"original_size ≤ max_output?"}
    C -->|no| X["error: output limit exceeded<br/>(before any decompression)"]
    C --> D{"payload_size == remaining bytes?"}
    D -->|no| X2["error: size mismatch"]
    D --> E{"CRC32(payload) == payload_crc32?"}
    E -->|no| X3["error: Payload CRC mismatch"]
    E --> F["decompress(payload, method, original_size)"]
    F --> G{"CRC32(result) == original_crc32?"}
    G -->|no| X4["error: Original CRC mismatch"]
    G --> OK["original file bytes"]
```

## 3. Verify

Same as decode, but the result is thrown away: `read_text_file → decode_poem → extract_container`, then prints `Verification successful.` Nothing is written to disk.

## 4. Inspect

`read_text_file → decode_poem → parse_header`, then prints version, compression, sizes, CRC32 values and the stanza count. It does **not** decompress or check the payload — the output ends with `Payload integrity: not checked`. Use `verify` for that.

## 5. Error handling in one picture

```mermaid
flowchart LR
    Any["any step fails"] --> T["throws an exception<br/>with a clear message"]
    T --> U["stack unwinds:<br/>RAII objects clean up"]
    U --> M["main prints 'Error: ...'<br/>exit code 1"]
```

Because partial results are only published at the very end (see [RAII](raii.md)), a failure never leaves a half‑written output file.

## 6. Which command touches what?

| Command | Reads | Writes | Checks all CRCs | Decompresses |
|---|---|---|:---:|:---:|
| encode | any file | `.poem` | – (creates them) | – |
| decode | `.poem` | original file | ✅ | ✅ |
| verify | `.poem` | nothing | ✅ | ✅ (in memory) |
| inspect | `.poem` | nothing | header only | ❌ |
