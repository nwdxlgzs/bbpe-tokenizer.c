# BBPE Tokenizer

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, maybe high-performance C implementation of Byte-Level Byte Pair Encoding (BBPE) tokenization, compatible with HuggingFace's `tokenizer.json` format.
一个轻量级可能算是高性能的字节级字节对编码（BBPE）分词器，使用 C 语言实现，兼容 HuggingFace 的 `tokenizer.json` 格式。

Designed for fast inference with minimal memory footprint, ideal for embedding in C/C++ projects or language bindings.  
专为快速推理和极小内存占用而设计，适合嵌入到 C/C++ 项目或作为语言绑定的底层引擎。

---

## Features / 特性

- ✅ **Full BBPE inference** – encode (text → token IDs) and decode (token IDs → text)  
  ✅ **完整的 BBPE 推理** – 编码（文本 → token ID）和解码（token ID → 文本）
- ✅ **Loads `tokenizer.json`** – vocabulary, merge rules, pre‑tokenizers, special tokens  
  ✅ **加载 `tokenizer.json`** – 词汇表、合并规则、预分词器、特殊 token
- ✅ **ByteLevel pre‑tokenization** – optional `add_prefix_space`  
  ✅ **ByteLevel 预分词** – 可选添加前缀空格
- ✅ **Regex‑based splitting** – using PCRE2 (UTF‑8 support)  
  ✅ **基于正则表达式的分割** – 使用 PCRE2（支持 UTF‑8）
- ✅ **Special token handling** – longest‑match extraction  
  ✅ **特殊 token 处理** – 最长匹配提取
- ✅ **Optimized BPE merging** – priority queue (min‑heap) with O(n log n) complexity  
  ✅ **优化的 BPE 合并** – 优先队列（最小堆），复杂度 O(n log n)
- ✅ **Correct tie‑breaking** – when priorities are equal, leftmost merge is chosen (matches original linear scan)  
  ✅ **正确的优先级平局处理** – 优先级相同时选择最左边的合并（与原始线性扫描结果一致）
- ✅ **Clean C API** – opaque pointer, simple error codes  
  ✅ **简洁的 C API** – 不透明指针，简单错误码
- ✅ **No global state** – thread‑safe when each tokenizer is used exclusively  
  ✅ **无全局状态** – 每个分词器独占使用时线程安全

---

## Dependencies / 依赖

- [cJSON](https://github.com/DaveGamble/cJSON) – JSON parsing / JSON 解析
- [PCRE2](https://github.com/PCRE2Project/pcre2) – regular expressions (UTF‑8, UCP enabled) / 正则表达式（启用 UTF‑8 和 UCP）
- [uthash](https://github.com/troydhanson/uthash) – hash table implementation (header‑only) / 哈希表实现（仅头文件）

---

## API Reference / API 参考

All public functions and types are defined in `bbpe_tokenizer.h`.  
所有公共函数和类型都定义在 `bbpe_tokenizer.h` 中。

### Initialization / 初始化

```c
BBPEStatus bbpe_init(const char *json_content, BBPETokenizer **out_tokenizer);
```
- `json_content`: null‑terminated string containing the whole `tokenizer.json`.  
  `json_content`：包含完整 `tokenizer.json` 内容的以 null 结尾的字符串。
- `out_tokenizer`: receives the opaque tokenizer handle.  
  `out_tokenizer`：接收不透明分词器句柄的指针。
- Returns `BBPE_OK` on success, otherwise an error code.  
  成功返回 `BBPE_OK`，否则返回错误码。

### Encoding (text → token IDs) / 编码（文本 → token ID）

```c
BBPEStatus bbpe_encode(BBPETokenizer *tokenizer, const char *text, BBPEOutput *out_output);
```
- `text`: UTF‑8 input string.  
  `text`：UTF‑8 输入字符串。
- `out_output`: pointer to a `BBPEOutput` structure that will hold the token IDs.  
  `out_output`：指向 `BBPEOutput` 结构的指针，用于存放 token ID。
  - `ids`: dynamically allocated array of `int32_t` (must be freed with `bbpe_free_output`).  
    `ids`：动态分配的 `int32_t` 数组（必须使用 `bbpe_free_output` 释放）。
  - `count`: number of tokens.  
    `count`：token 数量。
- Returns `BBPE_OK` on success.  
  成功返回 `BBPE_OK`。

### Decoding (token IDs → text) / 解码（token ID → 文本）

```c
BBPEStatus bbpe_decode(BBPETokenizer *tokenizer, const int32_t *ids, size_t count, char **out_text);
```
- `ids`: array of token IDs.  
  `ids`：token ID 数组。
- `count`: number of IDs.  
  `count`：ID 数量。
- `out_text`: receives a newly allocated UTF‑8 string (caller must `free()` it).  
  `out_text`：接收新分配的 UTF‑8 字符串（调用者必须使用 `free()` 释放）。
- Returns `BBPE_OK` on success.  
  成功返回 `BBPE_OK`。

### Memory management / 内存管理

```c
void bbpe_free_output(BBPEOutput *output);
void bbpe_destroy(BBPETokenizer *tokenizer);
```

### Error codes / 错误码

| Code / 代码                     | Value / 值 | Description (EN)                            | 描述 (ZH)                             |
|--------------------------------|------------|---------------------------------------------|---------------------------------------|
| `BBPE_OK`                      | 0          | Success                                     | 成功                                  |
| `BBPE_ERR_MEMORY`              | -1         | Memory allocation failed                    | 内存分配失败                          |
| `BBPE_ERR_JSON_PARSE`          | -2         | JSON parsing error                          | JSON 解析错误                         |
| `BBPE_ERR_VOCAB_MISSING`       | -3         | Vocabulary missing in JSON                  | JSON 中缺少词汇表                     |
| `BBPE_ERR_REGEX_COMPILE`       | -4         | PCRE2 regex compilation failed               | PCRE2 正则编译失败                    |
| `BBPE_ERR_TOKEN_NOT_FOUND`     | -5         | Token not found in vocabulary               | 词汇表中未找到 token                  |
| `BBPE_ERR_INVALID_INPUT`       | -6         | Invalid input parameter                      | 输入参数无效                          |
| `BBPE_ERR_UNSUPPORTED_TYPE`    | -7         | Unsupported pre‑tokenizer type               | 不支持的预分词器类型                  |

---

## Important Notes / 重要说明

- **UTF‑8 only** – Input text and all JSON strings must be valid UTF‑8.  
  **仅支持 UTF‑8** – 输入文本和所有 JSON 字符串必须是有效的 UTF‑8。
- **ByteLevel mapping** – The library uses the standard GPT‑2/BBPE mapping: printable bytes map to themselves, others map to private Unicode code points (`256 + n`).  
  **ByteLevel 映射** – 该库使用标准的 GPT‑2/BBPE 映射：可打印字节映射到自身，其他字节映射到私有 Unicode 码点（`256 + n`）。
- **Special tokens** – Extracted using longest‑match scanning. Overlapping special tokens are handled correctly.  
  **特殊 token** – 使用最长匹配扫描提取。重叠的特殊 token 会被正确处理。
- **Pre‑tokenizer chain** – The implementation supports a sequence of pre‑tokenizers as defined in `tokenizer.json` (e.g., `Sequence` of `Split` + `ByteLevel`).  
  **预分词器链** – 实现支持 `tokenizer.json` 中定义的预分词器序列（例如 `Split` + `ByteLevel` 的 `Sequence`）。
- **Memory ownership** – All output strings and arrays must be freed by the caller using the provided functions (`free()` for strings, `bbpe_free_output()` for `BBPEOutput`).  
  **内存所有权** – 所有输出的字符串和数组必须由调用者使用提供的函数释放（字符串用 `free()`，`BBPEOutput` 用 `bbpe_free_output()`）。
- **Thread safety** – The tokenizer handle is not thread‑safe for concurrent calls. Use separate handles or external locking.  
  **线程安全** – 分词器句柄不支持并发调用。请使用独立的句柄或外部锁。

---

## License / 许可证

See the `LICENSE` file for details.  
详情请参阅 `LICENSE` 文件。

---

*For issues, suggestions, or contributions, please visit the [GitHub repository](https://github.com/nwdxlgzs/bbpe-tokenizer.c).*  
*如有问题、建议或贡献，请访问 [GitHub 仓库](https://github.com/nwdxlgzs/bbpe-tokenizer.c)。*
