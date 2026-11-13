# Основной поток работы

[Оглавление](../README.md) · [Обзор](overview.md) · [RAII](raii.md) · [CRC32](crc32.md) · [Сжатие](compression.md) · 🇬🇧 [English version](../en/main-flow.md)

Все четыре команды находятся в [file_operations.cpp](../../src/core/file_operations.cpp). Каждая — короткий конвейер шагов; каждый шаг — отдельная, тестируемая функция.

## 0. Запуск программы

[main.cpp](../../src/main.cpp): в Windows переключает консоль на UTF‑8 (чтобы русский текст выводился верно), разбирает аргументы, выполняет выбранную команду, а любое исключение превращает в `Error: <сообщение>` и код выхода `1`.

```mermaid
flowchart TD
    A["bardzip.exe аргументы..."] --> B["Консоль в UTF-8"]
    B --> C["parse_arguments"]
    C -->|неверно| E["Error: ... → выход 1"]
    C --> D{"команда?"}
    D -->|encode| EN["encode_file → печать отчёта"]
    D -->|decode| DE["decode_file"]
    D -->|inspect| IN["inspect_file"]
    D -->|verify| VE["verify_file"]
    EN --> OK["выход 0"]
    DE --> OK
    IN --> OK
    VE --> OK
    EN -.исключение.-> E
    DE -.исключение.-> E
    IN -.исключение.-> E
    VE -.исключение.-> E
```

## 1. Encode: файл → поэма

```mermaid
flowchart LR
    A["read_binary_file<br/>(≤ 64 МиБ)"] --> B["compress<br/>none / zstd / auto"]
    B --> C["заголовок: размеры + CRC32<br/>+ CRC32 заголовка"]
    C --> D["контейнер =<br/>заголовок + payload"]
    D --> E["encode_poem<br/>2 байта → 1 строфа"]
    E --> F["write_text_file<br/>(временный файл → переименование)"]
```

```mermaid
sequenceDiagram
    actor U as Пользователь
    participant FO as file_operations
    participant CT as container
    participant CP as compression
    participant PC as poem_codec
    participant IO as file_writer / TemporaryFile
    U->>FO: encode_file(in, out, mode)
    FO->>FO: read_binary_file(in)
    FO->>CT: build_container(original, mode)
    CT->>CP: compress(original, mode)
    CP-->>CT: метод + байты payload
    CT->>CT: crc32(original), crc32(payload)
    CT->>CT: serialize_header (добавляет CRC заголовка)
    CT-->>FO: заголовок + payload
    FO->>PC: encode_poem(container)
    PC-->>FO: текст поэмы
    FO->>IO: write_text_file(out, poem)
    IO-->>U: файл появляется только когда готов целиком
    FO-->>U: отчёт (размеры, строфы)
```

Детали:
- Нечётная длина контейнера → для последней строфы добавляется один байт `0x00`.
- Отчёт в stderr: метод, размеры (исходный / payload / контейнер / поэма), число строф.

## 2. Decode: поэма → файл

```mermaid
flowchart LR
    A["read_text_file"] --> B["decode_poem<br/>текст → байты"]
    B --> C["extract_container<br/>проверки + распаковка"]
    C --> D["write_binary_file<br/>(временный файл → переименование)"]
```

### Внутри `decode_poem`

```mermaid
flowchart TD
    S["текст поэмы"] --> H{"первая непустая строка<br/>== BARDZIP-POEM 1 DICT 1 ?"}
    H -->|нет| X["ошибка"]
    H -->|да| L["собрать непустые строки<br/>(CRLF допускается)"]
    L --> M{"число строк % 3 == 0 ?"}
    M -->|нет| X
    M -->|да| ST["каждая строфа: припев верен?<br/>точка есть? слова известны?"]
    ST -->|сбой| X2["ошибка: 'Failed to decode stanza N'"]
    ST --> HD["байтов ≥ 36?<br/>parse_header (magic, CRC, версия...)"]
    HD --> LEN{"число строф соответствует<br/>36 + payload_size?"}
    LEN -->|нет| X
    LEN --> PAD{"длина нечётна → последний байт — нулевое заполнение?"}
    PAD -->|нет| X
    PAD -->|да| R["байты контейнера"]
```

### Внутри `extract_container`

```mermaid
flowchart TD
    A["байты контейнера"] --> B["parse_header<br/>(magic, CRC заголовка, поля)"]
    B --> C{"original_size ≤ max_output?"}
    C -->|нет| X["ошибка: лимит превышен<br/>(до любой распаковки)"]
    C --> D{"payload_size == оставшимся байтам?"}
    D -->|нет| X2["ошибка: размер не совпал"]
    D --> E{"CRC32(payload) == payload_crc32?"}
    E -->|нет| X3["ошибка: Payload CRC mismatch"]
    E --> F["decompress(payload, method, original_size)"]
    F --> G{"CRC32(результат) == original_crc32?"}
    G -->|нет| X4["ошибка: Original CRC mismatch"]
    G --> OK["байты исходного файла"]
```

## 3. Verify

То же, что decode, но результат выбрасывается: `read_text_file → decode_poem → extract_container`, затем печатается `Verification successful.` На диск ничего не пишется.

## 4. Inspect

`read_text_file → decode_poem → parse_header`, затем печать версии, сжатия, размеров, CRC32 и числа строф. Распаковки и проверки payload **нет** — вывод заканчивается строкой `Payload integrity: not checked`. Для проверки используйте `verify`.

## 5. Обработка ошибок одной картинкой

```mermaid
flowchart LR
    Any["любой шаг не удался"] --> T["бросается исключение<br/>с понятным сообщением"]
    T --> U["стек раскручивается:<br/>RAII-объекты убирают за собой"]
    U --> M["main печатает 'Error: ...'<br/>код выхода 1"]
```

Поскольку результат публикуется только в самом конце (см. [RAII](raii.md)), сбой никогда не оставляет наполовину записанный файл.

## 6. Кто что трогает

| Команда | Читает | Пишет | Проверяет все CRC | Распаковывает |
|---|---|---|:---:|:---:|
| encode | любой файл | `.poem` | – (создаёт их) | – |
| decode | `.poem` | исходный файл | ✅ | ✅ |
| verify | `.poem` | ничего | ✅ | ✅ (в памяти) |
| inspect | `.poem` | ничего | только заголовок | ❌ |
