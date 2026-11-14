# Main flow

[Docs index](../../README.md) · [Overview](overview.md) · [RAII](raii.md) · [CRC32](crc32.md) · [Compression](compression.md) · [Streaming](streaming.md) · 🇷🇺 [Русская версия](../ru/main-flow.md)

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

Encoding makes **two passes** over the data in small 64 KiB blocks (see [Streaming](streaming.md)): the header sits at the start of the container, but its sizes and CRCs are only known after the whole file has been processed.

```mermaid
flowchart LR
    A["Pass 1: read input in 64 KiB blocks<br/>→ size + CRC32"] --> B["compress (zstd) into a<br/>scratch payload file<br/>none: nothing to store"]
    B --> C["auto: compressed ≥ original?<br/>→ keep original instead"]
    C --> D["header: sizes + CRC32s<br/>+ header CRC32"]
    D --> E["Pass 2: header, then payload<br/>→ PoemEncoder (2 bytes → 1 stanza)"]
    E --> F["TemporaryFile<br/>→ rename on success"]
```

```mermaid
sequenceDiagram
    actor U as User
    participant FO as file_operations
    participant CP as ZstdCompressor
    participant SC as ScratchFile
    participant PC as PoemEncoder
    participant IO as TemporaryFile
    U->>FO: encode_file(in, out, mode)
    loop pass 1: every 64 KiB block
        FO->>FO: update original size + CRC32
        FO->>CP: write(block)
        CP->>SC: compressed bytes (+ payload CRC32)
    end
    FO->>FO: choose method, build header
    FO->>IO: create temp file next to out
    FO->>PC: header bytes
    loop pass 2: every 64 KiB block of the payload
        FO->>PC: payload block (scratch file, or the input again)
        PC->>IO: poem text in ~64 KiB pieces
    end
    FO->>FO: check size + CRC32 of what was streamed
    FO->>IO: commit() (atomic rename)
    IO-->>U: file appears only when complete
    FO-->>U: report (sizes, stanzas)
```

Details:
- Odd container length → one `0x00` byte is appended for the last stanza.
- If the data is stored (`none`, or `auto` decided compression does not help), pass 2 re-reads the input file instead of a scratch copy. While doing so it re-checks size and CRC32, so a file modified during encoding is detected ("Input file changed...") and nothing is published.
- The report printed to stderr: method, original / payload / container / poem sizes, stanza count.
- An already existing output file is rejected immediately, before any work is done.

## 2. Decode: poem → file

```mermaid
flowchart LR
    A["open poem<br/>(read in blocks)"] --> B["decode_poem_stream<br/>stanzas → payload bytes"]
    B --> C["scratch payload file<br/>+ CRC32 check"]
    C --> D["decompress in 64 KiB blocks<br/>+ original size and CRC32 check"]
    D --> E["TemporaryFile<br/>→ rename on success"]
```

### Inside `decode_poem_stream`

```mermaid
flowchart TD
    S["poem text, read line by line<br/>(a line may not exceed 4096 bytes)"] --> H{"first non-empty line<br/>== BARDZIP-POEM 1 DICT 1 ?"}
    H -->|no| X["error"]
    H -->|yes| ST["read 18 stanzas = 36 header bytes<br/>(refrain ok? period ok? words known?)"]
    ST -->|fail| X2["error: 'Failed to decode stanza N'"]
    ST --> HD["parse_header (magic, CRC, version...)<br/>→ on_header: output limit check"]
    HD --> PAY["read exactly payload_size bytes<br/>in stanzas, pass blocks to the next stage"]
    PAY --> LEN{"poem ended too early?"}
    LEN -->|yes| X
    LEN --> PAD{"odd payload → last stanza padded with 0?"}
    PAD -->|no| X
    PAD -->|yes| EX{"any more lines after the payload?"}
    EX -->|yes| X
    EX -->|no| R["header + payload delivered"]
```

### Inside the extraction stage

```mermaid
flowchart TD
    A["header (as soon as it is read)"] --> C{"original_size ≤ max_output?"}
    C -->|no| X["error: output limit exceeded<br/>(before reading the payload)"]
    C --> P["payload blocks → scratch file + running CRC32"]
    P --> E{"CRC32(payload) == payload_crc32?"}
    E -->|no| X3["error: Payload CRC mismatch"]
    E --> F["decompress (ZstdDecompressor)<br/>output never exceeds original_size"]
    F --> G{"size and CRC32(result)<br/>== header values?"}
    G -->|no| X4["error: size / Original CRC mismatch"]
    G --> OK["commit: output file is published"]
```

The decompressed bytes go straight into the temporary output file, which is published only after every check has passed.

## 3. Verify

Same as decode, but the decompressed bytes are only counted and checksummed, not written (the scratch payload file lives in the system temp folder and is deleted afterwards). Then it prints `Verification successful.`

## 4. Inspect

Reads the poem in a streaming way, validating its structure, and prints version, compression, sizes, CRC32 values and the stanza count taken from the header. It does **not** decompress or check the payload — the output ends with `Payload integrity: not checked`. Use `verify` for that.

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
