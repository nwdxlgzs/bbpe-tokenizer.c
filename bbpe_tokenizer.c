/**
 * @file bbpe_tokenizer.c
 * @brief BBPE (Byte-Level Byte Pair Encoding) 分词器完整实现
 *
 * 该文件基于 HuggingFace tokenizer.json 格式，实现了 ByteLevel BPE 分词器推理时的大部份功能（部分功能仅实现未做验证）。
 * 功能包括：
 * - 从 JSON 加载词汇表、合并规则、预分词器配置、特殊 token
 * - 支持 ByteLevel 预分词（可选添加前缀空格）和正则分割预分词
 * - 正确处理特殊 token（提取并单独编码）
 * - BPE 合并规则查找与优先级排序
 * - 编码（文本 → token IDs）与解码（token IDs → 文本）
 * - 内存管理与错误处理
 *
 * 依赖库：
 * - cJSON：JSON 解析
 * - pcre2：正则引擎
 * - uthash：哈希表实现
 *
 */

#include "bbpe_tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "cJSON.h"
#include "pcre2.h"
#include "uthash.h"

// ============================================================================
// 常量定义
// ============================================================================
#define STRING_TEMP_SIZE 0xff /* 用于合并规则解析的临时缓冲区大小 */
#define UNICODE_MAP_SIZE 512  /* unicode_to_byte 映射表大小，必须大于最大 Unicode 码点 */

// ============================================================================
// uthash 结构定义
// ============================================================================

/**
 * @brief 词汇表项：token 字符串 → id
 */
typedef struct
{
    char *token;       /* token 字符串 (UTF-8) */
    int id;            /* token ID */
    UT_hash_handle hh; /* uthash 句柄 */
} VocabEntry;

/**
 * @brief 特殊 token 表项
 */
typedef struct
{
    char *token; /* 特殊 token 字符串 */
    int id;      /* 对应的 ID */
    UT_hash_handle hh;
} SpecialEntry;

/**
 * @brief 单个合并规则信息
 */
typedef struct
{
    int32_t right_id; /* 右 token ID */
    int32_t new_id;   /* 合并后产生的新 token ID */
    int32_t priority; /* 优先级 (通常为合并顺序索引，越小优先级越高) */
} MergeRuleItem;

/**
 * @brief 每个 left 对应的规则行 (按 right_id 排序)
 */
typedef struct
{
    MergeRuleItem *items; /* 规则项数组，按 right_id 升序排列 */
    uint32_t count;       /* 规则数量 */
} MergeRuleRow;

// ============================================================================
// 预分词器配置 (链表节点)
// ============================================================================

/**
 * @brief 预分词器类型
 */
typedef enum
{
    PRE_TOKENIZER_NONE,       /* 无操作 */
    PRE_TOKENIZER_BYTE_LEVEL, /* ByteLevel 预分词器 (添加前缀空格) */
    PRE_TOKENIZER_REGEX_SPLIT /* 正则分割预分词器 */
} PreTokenizerType;

/**
 * @brief 预分词器链表节点
 */
typedef struct PreTokenizerNode
{
    PreTokenizerType type; /* 类型 */
    union
    {
        struct
        {
            int add_prefix_space; /* 是否在文本前添加空格 (ByteLevel) */
        } byte_level;
        struct
        {
            char *regex_pattern;        /* 正则表达式字符串，由节点负责释放 */
            pcre2_code *regex_compiled; /* 编译后的 pcre2 正则代码 */
        } split;
    } config;                      /* 类型相关的配置 */
    struct PreTokenizerNode *next; /* 下一个预分词器节点 (构成链) */
} PreTokenizerNode;

// ============================================================================
// 分词器主结构
// ============================================================================

/**
 * @brief BBPE 分词器主结构 (不透明指针的具体定义)
 */
struct BBPETokenizer
{
    VocabEntry *vocab_map;                     /* 词汇表哈希表 (token→id) */
    size_t merge_count;                        /* 合并规则总数 (仅用于统计) */
    MergeRuleRow *rule_rows;                   /* 规则行数组，索引为 left_id，每行按 right_id 排序 */
    uint32_t vocab_size;                       /* 词汇表大小 (最大 id + 1) */
    SpecialEntry *special_tokens_map;          /* 特殊 token 哈希表 (token→id) */
    uint32_t byte_to_unicode[256];             /* 字节 → Unicode 码点映射 (ByteLevel) */
    uint8_t unicode_to_byte[UNICODE_MAP_SIZE]; /* Unicode 码点 → 字节映射 (用于解码) */
    char **id_to_token;                        /* id → token 字符串数组 (指向 vocab 或 special 中的字符串) */
    PreTokenizerNode *pre_tokenizers;          /* 预分词器链表头 */
    char *byte_vocab_strs[256];                /* 预计算的字节对应字符串 (UTF-8)，用于快速查找字节 token */
};

// ============================================================================
// 预分词中间结果
// ============================================================================

/**
 * @brief 预分词产生的多个文本块
 */
typedef struct
{
    char **chunks; /* 文本块数组，每个块以 '\0' 结尾，由本结构负责释放 */
    size_t count;  /* 块数量 */
} PreTokenizedResult;

/**
 * @brief 分段结构：普通文本段或特殊 token 段
 */
typedef struct
{
    int is_special; /* 1 表示特殊 token 段，0 表示普通文本段 */
    char *text;     /* 当 is_special==0 时有效，为普通文本段 (需要释放) */
    int special_id; /* 当 is_special==1 时有效，为特殊 token 的 ID */
} TokenSegment;

// ============================================================================
// 辅助函数 (内存安全复制)
// ============================================================================

/**
 * @brief 安全的 strndup 实现
 * @param s 源字符串
 * @param n 最大复制字符数
 * @return 新分配的字符串，需调用 free 释放，失败返回 NULL
 */
static char *my_strndup(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (p)
    {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

// ============================================================================
// UTF-8 编解码工具函数
// ============================================================================

/**
 * @brief 从字符串 s 解码一个 UTF-8 字符
 * @param s 输入字符串指针 (至少包含一个完整字符)
 * @param cp 输出 Unicode 码点
 * @return 使用的字节数 (1-4)，若编码错误返回 -1
 */
static int utf8_decode(const char *s, uint32_t *cp)
{
    unsigned char c = (unsigned char)*s;
    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        if ((s[1] & 0xC0) != 0x80)
            return -1;
        *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
            return -1;
        *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            return -1;
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    return -1;
}

/**
 * @brief 将 Unicode 码点编码为 UTF-8
 * @param cp Unicode 码点
 * @param out 输出缓冲区 (至少 5 字节)
 * @return 写入的字节数 (1-4)
 */
static int utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    else if (cp < 0x800)
    {
        out[0] = 0xC0 | (cp >> 6);
        out[1] = 0x80 | (cp & 0x3F);
        return 2;
    }
    else if (cp < 0x10000)
    {
        out[0] = 0xE0 | (cp >> 12);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        return 3;
    }
    else
    {
        out[0] = 0xF0 | (cp >> 18);
        out[1] = 0x80 | ((cp >> 12) & 0x3F);
        out[2] = 0x80 | ((cp >> 6) & 0x3F);
        out[3] = 0x80 | (cp & 0x3F);
        return 4;
    }
}

// ============================================================================
// 特殊 token 提取
// ============================================================================

/**
 * @brief 将输入文本按特殊 token 分割成多个段
 * @param tok 分词器句柄
 * @param text 输入文本
 * @param out_len 输出段数量
 * @return 新分配的 TokenSegment 数组，使用后需释放各段 text 及数组本身
 */
static TokenSegment *extract_special_tokens(BBPETokenizer *tok, const char *text, size_t *out_len)
{
    size_t capacity = 16;
    TokenSegment *segments = (TokenSegment *)malloc(capacity * sizeof(TokenSegment));
    if (!segments)
        return NULL;
    size_t count = 0;

    const char *start = text;
    const char *pos = text;

    while (*pos)
    {
        SpecialEntry *best_match = NULL;
        size_t best_len = 0;

        // 遍历所有特殊 token，找到最长匹配
        SpecialEntry *entry, *tmp;
        HASH_ITER(hh, tok->special_tokens_map, entry, tmp)
        {
            size_t token_len = strlen(entry->token);
            if (strncmp(pos, entry->token, token_len) == 0)
            {
                if (token_len > best_len)
                {
                    best_len = token_len;
                    best_match = entry;
                }
            }
        }

        if (best_match)
        {
            // 有普通文本段需要先保存
            if (pos > start)
            {
                size_t normal_len = pos - start;
                if (normal_len > 0)
                {
                    if (count >= capacity)
                    {
                        capacity *= 2;
                        TokenSegment *new_seg = (TokenSegment *)realloc(segments, capacity * sizeof(TokenSegment));
                        if (!new_seg)
                        {
                            for (size_t i = 0; i < count; i++)
                                if (!segments[i].is_special && segments[i].text)
                                    free(segments[i].text);
                            free(segments);
                            return NULL;
                        }
                        segments = new_seg;
                    }
                    segments[count].is_special = 0;
                    segments[count].text = my_strndup(start, normal_len);
                    if (!segments[count].text)
                    {
                        for (size_t i = 0; i < count; i++)
                            if (!segments[i].is_special && segments[i].text)
                                free(segments[i].text);
                        free(segments);
                        return NULL;
                    }
                    segments[count].special_id = -1;
                    count++;
                }
            }

            // 保存特殊 token 段
            if (count >= capacity)
            {
                capacity *= 2;
                TokenSegment *new_seg = (TokenSegment *)realloc(segments, capacity * sizeof(TokenSegment));
                if (!new_seg)
                {
                    for (size_t i = 0; i < count; i++)
                        if (!segments[i].is_special && segments[i].text)
                            free(segments[i].text);
                    free(segments);
                    return NULL;
                }
                segments = new_seg;
            }
            segments[count].is_special = 1;
            segments[count].text = NULL;
            segments[count].special_id = best_match->id;
            count++;

            start = pos + best_len;
            pos = start;
        }
        else
        {
            pos++;
        }
    }

    // 处理剩余普通文本
    if (pos > start)
    {
        size_t normal_len = pos - start;
        if (normal_len > 0)
        {
            if (count >= capacity)
            {
                capacity *= 2;
                TokenSegment *new_seg = (TokenSegment *)realloc(segments, capacity * sizeof(TokenSegment));
                if (!new_seg)
                {
                    for (size_t i = 0; i < count; i++)
                        if (!segments[i].is_special && segments[i].text)
                            free(segments[i].text);
                    free(segments);
                    return NULL;
                }
                segments = new_seg;
            }
            segments[count].is_special = 0;
            segments[count].text = my_strndup(start, normal_len);
            if (!segments[count].text)
            {
                for (size_t i = 0; i < count; i++)
                    if (!segments[i].is_special && segments[i].text)
                        free(segments[i].text);
                free(segments);
                return NULL;
            }
            segments[count].special_id = -1;
            count++;
        }
    }

    *out_len = count;
    return segments;
}

// ============================================================================
// ByteLevel 映射初始化
// ============================================================================

/**
 * @brief 初始化字节与 Unicode 码点的双向映射 (ByteLevel BPE 标准映射)
 * @param tok 分词器句柄
 */
static void init_byte_mappings(BBPETokenizer *tok)
{
    int n = 0;
    memset(tok->unicode_to_byte, 0, sizeof(tok->unicode_to_byte));
    for (int b = 0; b < 256; b++)
    {
        // 可打印字符映射到自身，其他映射到 256+ 的私有码点
        if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255))
        {
            tok->byte_to_unicode[b] = b;
        }
        else
        {
            tok->byte_to_unicode[b] = 256 + n;
            n++;
        }
        tok->unicode_to_byte[tok->byte_to_unicode[b]] = (uint8_t)b;
    }
}

/**
 * @brief 预计算所有字节对应的 UTF-8 字符串 (用于快速查找字节 token)
 * @param tok 分词器句柄
 */
static void precompute_byte_strings(BBPETokenizer *tok)
{
    for (int b = 0; b < 256; b++)
    {
        uint32_t cp = tok->byte_to_unicode[b];
        char *buf = (char *)malloc(5);
        int len = utf8_encode(cp, buf);
        buf[len] = '\0';
        tok->byte_vocab_strs[b] = buf;
    }
}

// ============================================================================
// 预分词相关函数
// ============================================================================

/**
 * @brief 释放预分词结果
 * @param res 预分词结果指针
 */
static void free_pre_tokenized(PreTokenizedResult *res)
{
    if (!res)
        return;
    for (size_t i = 0; i < res->count; i++)
        free(res->chunks[i]);
    free(res->chunks);
    res->chunks = NULL;
    res->count = 0;
}

/**
 * @brief 应用单个预分词器到文本
 * @param node 预分词器节点
 * @param text 输入文本
 * @param out 输出预分词结果 (调用者需使用 free_pre_tokenized 释放)
 * @return BBPEStatus
 */
static BBPEStatus apply_single_pre_tokenizer(PreTokenizerNode *node, const char *text, PreTokenizedResult *out)
{
    // 初始化输出结构为空
    out->chunks = NULL;
    out->count = 0;

    if (node->type == PRE_TOKENIZER_BYTE_LEVEL)
    {
        // ByteLevel: 可选添加前缀空格，整个文本作为一个块
        size_t len = strlen(text);
        size_t new_len = len + (node->config.byte_level.add_prefix_space ? 1 : 0);
        char *buf = (char *)malloc(new_len + 1);
        if (!buf)
            return BBPE_ERR_MEMORY;
        if (node->config.byte_level.add_prefix_space)
        {
            buf[0] = ' ';
            memcpy(buf + 1, text, len);
        }
        else
        {
            memcpy(buf, text, len);
        }
        buf[new_len] = '\0';
        out->chunks = (char **)malloc(sizeof(char *));
        if (!out->chunks)
        {
            free(buf);
            return BBPE_ERR_MEMORY;
        }
        out->chunks[0] = buf;
        out->count = 1;
        return BBPE_OK;
    }
    else if (node->type == PRE_TOKENIZER_REGEX_SPLIT && node->config.split.regex_compiled)
    {
        // 正则分割: 使用 pcre2 匹配，将文本按匹配到的部分分割成块
        pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(
            node->config.split.regex_compiled, NULL);
        if (!match_data)
            return BBPE_ERR_MEMORY;

        size_t text_len = strlen(text);
        PCRE2_SIZE offset = 0;
        PCRE2_SIZE last_end = 0;
        char **chunks = NULL;
        size_t count = 0;

        while (offset < text_len)
        {
            int rc = pcre2_match(node->config.split.regex_compiled,
                                 (PCRE2_SPTR)text, text_len, offset, 0,
                                 match_data, NULL);
            if (rc < 0)
            {
                if (rc == PCRE2_ERROR_NOMATCH)
                    break;
                else
                    break; // 其他错误，停止匹配
            }

            // 处理空匹配（返回 0 表示匹配了空字符串）
            if (rc == 0)
            {
                // 空匹配，推进一个字符以避免无限循环
                offset++;
                continue;
            }

            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
            PCRE2_SIZE start = ovector[0];
            PCRE2_SIZE end = ovector[1];

            // 保存匹配前的内容
            if (start > last_end)
            {
                size_t chunk_len = start - last_end;
                char *chunk = (char *)malloc(chunk_len + 1);
                if (!chunk)
                {
                    for (size_t i = 0; i < count; i++)
                        free(chunks[i]);
                    free(chunks);
                    pcre2_match_data_free(match_data);
                    out->chunks = NULL; // 确保输出为空
                    out->count = 0;
                    return BBPE_ERR_MEMORY;
                }
                memcpy(chunk, text + last_end, chunk_len);
                chunk[chunk_len] = '\0';

                char **tmp = (char **)realloc(chunks, sizeof(char *) * (count + 1));
                if (!tmp)
                {
                    free(chunk);
                    for (size_t i = 0; i < count; i++)
                        free(chunks[i]);
                    free(chunks);
                    pcre2_match_data_free(match_data);
                    out->chunks = NULL;
                    out->count = 0;
                    return BBPE_ERR_MEMORY;
                }
                chunks = tmp;
                chunks[count++] = chunk;
            }

            // 保存匹配到的部分本身
            size_t match_len = end - start;
            char *match_chunk = (char *)malloc(match_len + 1);
            if (!match_chunk)
            {
                for (size_t i = 0; i < count; i++)
                    free(chunks[i]);
                free(chunks);
                pcre2_match_data_free(match_data);
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            memcpy(match_chunk, text + start, match_len);
            match_chunk[match_len] = '\0';

            char **tmp = (char **)realloc(chunks, sizeof(char *) * (count + 1));
            if (!tmp)
            {
                free(match_chunk);
                for (size_t i = 0; i < count; i++)
                    free(chunks[i]);
                free(chunks);
                pcre2_match_data_free(match_data);
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            chunks = tmp;
            chunks[count++] = match_chunk;

            last_end = end;
            offset = end;
            if (start == end) // 避免无限循环
                offset++;
        }

        // 处理剩余文本
        if (last_end < text_len)
        {
            size_t rem_len = text_len - last_end;
            char *chunk = (char *)malloc(rem_len + 1);
            if (!chunk)
            {
                for (size_t i = 0; i < count; i++)
                    free(chunks[i]);
                free(chunks);
                pcre2_match_data_free(match_data);
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            memcpy(chunk, text + last_end, rem_len);
            chunk[rem_len] = '\0';

            char **tmp = (char **)realloc(chunks, sizeof(char *) * (count + 1));
            if (!tmp)
            {
                free(chunk);
                for (size_t i = 0; i < count; i++)
                    free(chunks[i]);
                free(chunks);
                pcre2_match_data_free(match_data);
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            chunks = tmp;
            chunks[count++] = chunk;
        }

        pcre2_match_data_free(match_data);

        // 如果没有产生任何块（例如正则不匹配），则返回原文本作为一个块
        if (count == 0)
        {
            chunks = (char **)malloc(sizeof(char *));
            if (!chunks)
            {
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            chunks[0] = strdup(text);
            if (!chunks[0])
            {
                free(chunks);
                out->chunks = NULL;
                out->count = 0;
                return BBPE_ERR_MEMORY;
            }
            count = 1;
        }

        out->chunks = chunks;
        out->count = count;
        return BBPE_OK;
    }
    // 如果类型不匹配或缺少编译好的正则，返回空结果（但通常不会发生）
    return BBPE_OK;
}

/**
 * @brief 对文本执行完整的预分词链
 * @param tok 分词器句柄
 * @param text 输入文本
 * @param out 输出预分词结果
 * @return BBPEStatus
 */
static BBPEStatus pre_tokenize(BBPETokenizer *tok, const char *text, PreTokenizedResult *out)
{
    PreTokenizedResult current;
    current.chunks = (char **)malloc(sizeof(char *));
    if (!current.chunks)
        return BBPE_ERR_MEMORY;
    current.chunks[0] = strdup(text);
    if (!current.chunks[0])
    {
        free(current.chunks);
        return BBPE_ERR_MEMORY;
    }
    current.count = 1;

    PreTokenizerNode *node = tok->pre_tokenizers;
    while (node)
    {
        PreTokenizedResult next = {NULL, 0};
        BBPEStatus status;

        for (size_t i = 0; i < current.count; i++)
        {
            PreTokenizedResult part = {NULL, 0}; // 初始化局部变量
            status = apply_single_pre_tokenizer(node, current.chunks[i], &part);
            if (status != BBPE_OK)
            {
                // 错误处理: 释放已有资源
                free_pre_tokenized(&part);
                for (size_t k = 0; k < current.count; k++)
                    free(current.chunks[k]);
                free(current.chunks);
                free_pre_tokenized(&next);
                return status;
            }

            if (part.count > 0)
            {
                // 合并 part 到 next
                if (next.count > SIZE_MAX / sizeof(char *) - part.count)
                {
                    free_pre_tokenized(&part);
                    for (size_t k = 0; k < current.count; k++)
                        free(current.chunks[k]);
                    free(current.chunks);
                    free_pre_tokenized(&next);
                    return BBPE_ERR_MEMORY;
                }

                size_t new_count = next.count + part.count;
                char **tmp = (char **)realloc(next.chunks, sizeof(char *) * new_count);
                if (!tmp)
                {
                    free_pre_tokenized(&part);
                    for (size_t k = 0; k < current.count; k++)
                        free(current.chunks[k]);
                    free(current.chunks);
                    free_pre_tokenized(&next);
                    return BBPE_ERR_MEMORY;
                }
                next.chunks = tmp;

                // 转移 part.chunks 中的指针到 next
                for (size_t j = 0; j < part.count; j++)
                {
                    next.chunks[next.count + j] = part.chunks[j];
                }
                next.count = new_count;
                // 释放 part.chunks 数组本身，但保留其中字符串 (已转移)
                free(part.chunks);
            }
            else
            {
                free_pre_tokenized(&part);
            }
        }

        // 释放 current 中的字符串和数组
        for (size_t i = 0; i < current.count; i++)
            free(current.chunks[i]);
        free(current.chunks);

        current = next;
        node = node->next;
    }

    *out = current;
    return BBPE_OK;
}

// ============================================================================
// BPE 合并相关函数
// ============================================================================

/**
 * @brief 查找是否存在 left+right 的合并规则
 * @param tok 分词器句柄
 * @param left 左 token ID
 * @param right 右 token ID
 * @param out_new_id 输出合并后的新 token ID
 * @param out_priority 输出规则优先级
 * @return 1 表示找到规则，0 表示未找到
 */
static int find_merge_rule(BBPETokenizer *tok, int32_t left, int32_t right,
                           int32_t *out_new_id, int32_t *out_priority)
{
    if (left < 0 || left >= tok->vocab_size)
        return 0;
    MergeRuleRow *row = &tok->rule_rows[left];
    if (row->count == 0)
        return 0;

    // 二分查找 right_id
    int lo = 0, hi = row->count - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        int32_t mid_right = row->items[mid].right_id;
        if (mid_right == right)
        {
            *out_new_id = row->items[mid].new_id;
            *out_priority = row->items[mid].priority;
            return 1;
        }
        else if (mid_right < right)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return 0;
}

/**
 * @brief 在 token 序列中查找优先级最高的可合并位置
 * @param ids token ID 数组
 * @param token_count 当前 token 数量
 * @param tok 分词器句柄
 * @param out_new_id 输出合并后的新 token ID
 * @return 最佳合并位置的索引 (从 0 开始)，若无可合并则返回 -1
 */
static int find_best_merge(int32_t *ids, size_t token_count, BBPETokenizer *tok, int32_t *out_new_id)
{
    int best_idx = -1;
    int min_priority = INT32_MAX;

    for (size_t i = 0; i < token_count - 1; i++)
    {
        int32_t left = ids[i];
        int32_t right = ids[i + 1];
        int32_t new_id, priority;
        if (find_merge_rule(tok, left, right, &new_id, &priority))
        {
            if (priority < min_priority)
            {
                min_priority = priority;
                best_idx = (int)i;
                *out_new_id = new_id;
            }
        }
    }
    return best_idx;
}

/**
 * @brief 将单个文本块编码为 token IDs 并追加到输出结构
 * @param tok 分词器句柄
 * @param chunk 输入文本块
 * @param out_accum 累积输出结构 (ids 数组会动态扩展)
 * @return BBPEStatus
 */
static BBPEStatus encode_chunk(BBPETokenizer *tok, const char *chunk, BBPEOutput *out_accum)
{
    size_t chunk_len = strlen(chunk);
    if (chunk_len == 0)
        return BBPE_OK;

    // 初始时，将每个字节映射为 token ID
    int32_t *ids = (int32_t *)malloc(sizeof(int32_t) * chunk_len);
    if (!ids)
        return BBPE_ERR_MEMORY;
    size_t token_count = 0;

    for (size_t i = 0; i < chunk_len; i++)
    {
        uint8_t byte = (uint8_t)chunk[i];
        const char *vocab_str = tok->byte_vocab_strs[byte];
        VocabEntry *vocab_entry = NULL;
        HASH_FIND_STR(tok->vocab_map, vocab_str, vocab_entry);
        // 如果预计算字符串未找到，回退到原始字节字符串 (极少情况)
        if (!vocab_entry)
        {
            char raw_byte[2] = {(char)byte, '\0'};
            HASH_FIND_STR(tok->vocab_map, raw_byte, vocab_entry);
        }
        if (!vocab_entry)
        {
            free(ids);
            return BBPE_ERR_TOKEN_NOT_FOUND;
        }
        ids[token_count++] = vocab_entry->id;
    }

    // 反复应用合并规则直到无法合并
    while (token_count > 1)
    {
        int32_t new_id;
        int best_idx = find_best_merge(ids, token_count, tok, &new_id);
        if (best_idx == -1)
            break;

        ids[best_idx] = new_id;
        // 左移删除后面的元素
        for (size_t k = best_idx + 1; k < token_count - 1; k++)
        {
            ids[k] = ids[k + 1];
        }
        token_count--;
    }

    // 将结果追加到 out_accum
    size_t old_count = out_accum->count;
    out_accum->count += token_count;
    int32_t *new_ids = (int32_t *)realloc(out_accum->ids, sizeof(int32_t) * out_accum->count);
    if (!new_ids)
    {
        free(ids);
        return BBPE_ERR_MEMORY;
    }
    out_accum->ids = new_ids;
    memcpy(out_accum->ids + old_count, ids, token_count * sizeof(int32_t));
    free(ids);
    return BBPE_OK;
}

// ============================================================================
// 预分词器节点解析 (JSON)
// ============================================================================

/**
 * @brief 从 cJSON 对象解析单个预分词器节点
 * @param obj JSON 对象
 * @param status 输出解析状态
 * @return 新分配的 PreTokenizerNode，失败返回 NULL
 */
static PreTokenizerNode *parse_pre_tokenizer_node(cJSON *obj, BBPEStatus *status)
{
    if (!obj)
        return NULL;
    cJSON *type = cJSON_GetObjectItem(obj, "type");
    if (!type || !type->valuestring)
    {
        *status = BBPE_ERR_INVALID_INPUT;
        return NULL;
    }

    PreTokenizerNode *node = (PreTokenizerNode *)calloc(1, sizeof(PreTokenizerNode));
    if (!node)
    {
        *status = BBPE_ERR_MEMORY;
        return NULL;
    }

    if (strcmp(type->valuestring, "ByteLevel") == 0)
    {
        node->type = PRE_TOKENIZER_BYTE_LEVEL;
        cJSON *add_space = cJSON_GetObjectItem(obj, "add_prefix_space");
        node->config.byte_level.add_prefix_space = (add_space && cJSON_IsTrue(add_space)) ? 1 : 0;
    }
    else if (strcmp(type->valuestring, "Split") == 0)
    {
        node->type = PRE_TOKENIZER_REGEX_SPLIT;
        cJSON *pattern_obj = cJSON_GetObjectItem(obj, "pattern");
        if (pattern_obj)
        {
            cJSON *regex = cJSON_GetObjectItem(pattern_obj, "Regex");
            if (regex && regex->valuestring)
            {
                node->config.split.regex_pattern = strdup(regex->valuestring);
                int err;
                PCRE2_SIZE err_off;
                node->config.split.regex_compiled = pcre2_compile(
                    (PCRE2_SPTR)node->config.split.regex_pattern,
                    PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP, &err, &err_off, NULL);
                if (!node->config.split.regex_compiled)
                {
                    free(node->config.split.regex_pattern);
                    free(node);
                    *status = BBPE_ERR_REGEX_COMPILE;
                    return NULL;
                }
            }
            else
            {
                free(node);
                *status = BBPE_ERR_INVALID_INPUT;
                return NULL;
            }
        }
        else
        {
            free(node);
            *status = BBPE_ERR_INVALID_INPUT;
            return NULL;
        }
    }
    else
    {
        free(node);
        *status = BBPE_ERR_UNSUPPORTED_TYPE;
        return NULL;
    }
    return node;
}

// ============================================================================
// 合并规则排序比较函数
// ============================================================================

/**
 * @brief 用于 qsort 比较两个 MergeRuleItem 的 right_id
 */
static int compare_merge_rule_items(const void *a, const void *b)
{
    int ra = ((const MergeRuleItem *)a)->right_id;
    int rb = ((const MergeRuleItem *)b)->right_id;
    return (ra > rb) - (ra < rb);
}

// ============================================================================
// 公共 API 实现
// ============================================================================

BBPEStatus bbpe_init(const char *json_content, BBPETokenizer **out_tokenizer)
{
    if (!json_content || !out_tokenizer)
        return BBPE_ERR_INVALID_INPUT;

    cJSON *root = cJSON_Parse(json_content);
    if (!root)
        return BBPE_ERR_JSON_PARSE;

    BBPETokenizer *tok = (BBPETokenizer *)calloc(1, sizeof(BBPETokenizer));
    if (!tok)
    {
        cJSON_Delete(root);
        return BBPE_ERR_MEMORY;
    }

    // 初始化 Byte 映射并预计算字符串
    init_byte_mappings(tok);
    precompute_byte_strings(tok);

    // ========== 1. 解析 vocab ==========
    cJSON *model = cJSON_GetObjectItem(root, "model");
    if (!model || !cJSON_HasObjectItem(model, "vocab"))
    {
        bbpe_destroy(tok);
        cJSON_Delete(root);
        return BBPE_ERR_VOCAB_MISSING;
    }

    cJSON *vocab = cJSON_GetObjectItem(model, "vocab");
    cJSON *item = NULL;
    int max_id = -1;
    cJSON_ArrayForEach(item, vocab)
    {
        if (item->string && cJSON_IsNumber(item))
        {
            VocabEntry *entry = (VocabEntry *)malloc(sizeof(VocabEntry));
            if (!entry)
            {
                bbpe_destroy(tok);
                cJSON_Delete(root);
                return BBPE_ERR_MEMORY;
            }
            entry->token = strdup(item->string);
            if (!entry->token)
            {
                free(entry);
                bbpe_destroy(tok);
                cJSON_Delete(root);
                return BBPE_ERR_MEMORY;
            }
            entry->id = (int)item->valueint;
            HASH_ADD_KEYPTR(hh, tok->vocab_map, entry->token, strlen(entry->token), entry);
            if (entry->id > max_id)
                max_id = entry->id;
        }
    }

    // 检查词汇表是否为空
    if (max_id < 0)
    {
        bbpe_destroy(tok);
        cJSON_Delete(root);
        return BBPE_ERR_VOCAB_MISSING;
    }

    tok->vocab_size = max_id + 1;

    // 分配 id_to_token 数组
    tok->id_to_token = (char **)calloc(tok->vocab_size, sizeof(char *));
    if (!tok->id_to_token)
    {
        bbpe_destroy(tok);
        cJSON_Delete(root);
        return BBPE_ERR_MEMORY;
    }

    // 填充 id_to_token
    VocabEntry *v_cur, *v_tmp;
    HASH_ITER(hh, tok->vocab_map, v_cur, v_tmp)
    {
        if (v_cur->id >= 0 && v_cur->id < tok->vocab_size)
        {
            tok->id_to_token[v_cur->id] = v_cur->token;
        }
    }

    // ========== 2. 解析 merges ==========
    cJSON *merges = cJSON_GetObjectItem(model, "merges");
    if (merges && cJSON_IsArray(merges))
    {
        size_t merge_count = cJSON_GetArraySize(merges);
        tok->merge_count = merge_count;

        typedef struct
        {
            int32_t left_id;
            int32_t right_id;
            int32_t new_id;
            int32_t priority;
        } MergeRecord;
        MergeRecord *temp_records = (MergeRecord *)malloc(merge_count * sizeof(MergeRecord));
        if (!temp_records)
        {
            bbpe_destroy(tok);
            cJSON_Delete(root);
            return BBPE_ERR_MEMORY;
        }
        size_t record_cnt = 0;

        char LSTR_BUFFER[STRING_TEMP_SIZE + 1] = {0};
        char MSTR_BUFFER[STRING_TEMP_SIZE + 1] = {0};
        size_t i = 0;
        cJSON *merge_item;
        cJSON_ArrayForEach(merge_item, merges)
        {
            char *left_str = NULL;
            char *right_str = NULL;

            if (cJSON_IsString(merge_item))
            {
                char *combined = merge_item->valuestring;
                if (strlen(combined) > STRING_TEMP_SIZE)
                    continue;
                char *space = strchr(combined, ' ');
                if (!space)
                    continue;
                size_t left_len = space - combined;
                strncpy(LSTR_BUFFER, combined, left_len);
                LSTR_BUFFER[left_len] = '\0';
                left_str = LSTR_BUFFER;
                right_str = space + 1;
            }
            else if (cJSON_IsArray(merge_item) && cJSON_GetArraySize(merge_item) == 2)
            {
                cJSON *left_node = cJSON_GetArrayItem(merge_item, 0);
                cJSON *right_node = cJSON_GetArrayItem(merge_item, 1);
                if (cJSON_IsString(left_node) && cJSON_IsString(right_node))
                {
                    left_str = left_node->valuestring;
                    right_str = right_node->valuestring;
                    if (strlen(left_str) + strlen(right_str) > STRING_TEMP_SIZE)
                        continue;
                }
                else
                    continue;
            }
            else
                continue;

            VocabEntry *left_entry = NULL, *right_entry = NULL;
            HASH_FIND_STR(tok->vocab_map, left_str, left_entry);
            HASH_FIND_STR(tok->vocab_map, right_str, right_entry);
            if (!left_entry || !right_entry)
                continue;

            int32_t left_id = left_entry->id;
            int32_t right_id = right_entry->id;

            if (strlen(left_str) + strlen(right_str) >= sizeof(MSTR_BUFFER))
                continue;

            MSTR_BUFFER[0] = 0;
            strcat(MSTR_BUFFER, left_str);
            strcat(MSTR_BUFFER, right_str);
            VocabEntry *merged_entry = NULL;
            HASH_FIND_STR(tok->vocab_map, MSTR_BUFFER, merged_entry);
            if (!merged_entry)
                continue;

            int32_t new_id = merged_entry->id;

            temp_records[record_cnt].left_id = left_id;
            temp_records[record_cnt].right_id = right_id;
            temp_records[record_cnt].new_id = new_id;
            temp_records[record_cnt].priority = (int32_t)i;
            record_cnt++;
            i++;
        }

        uint32_t *counts = (uint32_t *)calloc(tok->vocab_size, sizeof(uint32_t));
        if (!counts)
        {
            free(temp_records);
            bbpe_destroy(tok);
            cJSON_Delete(root);
            return BBPE_ERR_MEMORY;
        }
        for (size_t j = 0; j < record_cnt; j++)
        {
            int left = temp_records[j].left_id;
            if (left >= 0 && left < tok->vocab_size)
                counts[left]++;
        }

        tok->rule_rows = (MergeRuleRow *)calloc(tok->vocab_size, sizeof(MergeRuleRow));
        if (!tok->rule_rows)
        {
            free(counts);
            free(temp_records);
            bbpe_destroy(tok);
            cJSON_Delete(root);
            return BBPE_ERR_MEMORY;
        }

        for (int left = 0; left < tok->vocab_size; left++)
        {
            if (counts[left] > 0)
            {
                tok->rule_rows[left].items = (MergeRuleItem *)malloc(counts[left] * sizeof(MergeRuleItem));
                if (!tok->rule_rows[left].items)
                {
                    // 分配失败，直接清理整个 tok（bbpe_destroy 会统一释放所有资源）
                    free(counts);
                    free(temp_records);
                    bbpe_destroy(tok);
                    cJSON_Delete(root);
                    return BBPE_ERR_MEMORY;
                }
                tok->rule_rows[left].count = counts[left];
            }
        }

        memset(counts, 0, tok->vocab_size * sizeof(uint32_t));
        for (size_t j = 0; j < record_cnt; j++)
        {
            int left = temp_records[j].left_id;
            uint32_t pos = counts[left]++;
            MergeRuleItem *item = &tok->rule_rows[left].items[pos];
            item->right_id = temp_records[j].right_id;
            item->new_id = temp_records[j].new_id;
            item->priority = temp_records[j].priority;
        }

        for (int left = 0; left < tok->vocab_size; left++)
        {
            if (tok->rule_rows[left].count > 0)
            {
                qsort(tok->rule_rows[left].items, tok->rule_rows[left].count,
                      sizeof(MergeRuleItem), compare_merge_rule_items);
            }
        }

        free(counts);
        free(temp_records);
    }
    else
    {
        tok->merge_count = 0;
        // 即使没有 merges，也分配一个零数组，以便安全访问
        tok->rule_rows = (MergeRuleRow *)calloc(tok->vocab_size, sizeof(MergeRuleRow));
        if (!tok->rule_rows)
        {
            bbpe_destroy(tok);
            cJSON_Delete(root);
            return BBPE_ERR_MEMORY;
        }
    }

    // ========== 3. 解析 pre_tokenizer ==========
    cJSON *pre_tok = cJSON_GetObjectItem(root, "pre_tokenizer");
    if (pre_tok)
    {
        cJSON *type = cJSON_GetObjectItem(pre_tok, "type");
        if (type && type->valuestring)
        {
            PreTokenizerNode *head = NULL;
            PreTokenizerNode **tail = &head;
            BBPEStatus parse_status = BBPE_OK;

            if (strcmp(type->valuestring, "Sequence") == 0)
            {
                cJSON *pretokenizers = cJSON_GetObjectItem(pre_tok, "pretokenizers");
                if (pretokenizers && cJSON_IsArray(pretokenizers))
                {
                    int size = cJSON_GetArraySize(pretokenizers);
                    for (int i = 0; i < size; i++)
                    {
                        cJSON *item = cJSON_GetArrayItem(pretokenizers, i);
                        PreTokenizerNode *node = parse_pre_tokenizer_node(item, &parse_status);
                        if (!node)
                        {
                            while (head)
                            {
                                PreTokenizerNode *next = head->next;
                                if (head->type == PRE_TOKENIZER_REGEX_SPLIT)
                                {
                                    free(head->config.split.regex_pattern);
                                    pcre2_code_free(head->config.split.regex_compiled);
                                }
                                free(head);
                                head = next;
                            }
                            bbpe_destroy(tok);
                            cJSON_Delete(root);
                            return parse_status;
                        }
                        *tail = node;
                        tail = &node->next;
                    }
                }
            }
            else
            {
                PreTokenizerNode *node = parse_pre_tokenizer_node(pre_tok, &parse_status);
                if (!node)
                {
                    bbpe_destroy(tok);
                    cJSON_Delete(root);
                    return parse_status;
                }
                head = node;
            }
            tok->pre_tokenizers = head;
        }
    }

    // ========== 4. 解析 added_tokens ==========
    cJSON *added_tokens = cJSON_GetObjectItem(root, "added_tokens");
    if (added_tokens && cJSON_IsArray(added_tokens))
    {
        cJSON *token_obj = NULL;
        cJSON_ArrayForEach(token_obj, added_tokens)
        {
            cJSON *content = cJSON_GetObjectItem(token_obj, "content");
            cJSON *id = cJSON_GetObjectItem(token_obj, "id");
            if (content && content->valuestring && id && cJSON_IsNumber(id))
            {
                int32_t sid = (int32_t)id->valueint;

                if (sid >= tok->vocab_size)
                {
                    uint32_t new_size = sid + 1;

                    // 扩展 id_to_token
                    char **new_id_to_token = (char **)realloc(tok->id_to_token, new_size * sizeof(char *));
                    if (!new_id_to_token)
                    {
                        bbpe_destroy(tok);
                        cJSON_Delete(root);
                        return BBPE_ERR_MEMORY;
                    }
                    for (uint32_t i = tok->vocab_size; i < new_size; i++)
                    {
                        new_id_to_token[i] = NULL;
                    }
                    tok->id_to_token = new_id_to_token;

                    // 同步扩展 rule_rows 数组
                    MergeRuleRow *new_rows;
                    if (tok->rule_rows)
                    {
                        new_rows = (MergeRuleRow *)realloc(tok->rule_rows, new_size * sizeof(MergeRuleRow));
                    }
                    else
                    {
                        new_rows = (MergeRuleRow *)calloc(new_size, sizeof(MergeRuleRow));
                    }
                    if (!new_rows)
                    {
                        bbpe_destroy(tok);
                        cJSON_Delete(root);
                        return BBPE_ERR_MEMORY;
                    }
                    if (tok->rule_rows)
                    {
                        // realloc 后新增部分未清零
                        memset(new_rows + tok->vocab_size, 0, (new_size - tok->vocab_size) * sizeof(MergeRuleRow));
                    }
                    tok->rule_rows = new_rows;

                    tok->vocab_size = new_size;
                }

                if (tok->id_to_token[sid] == NULL)
                {
                    SpecialEntry *entry = (SpecialEntry *)malloc(sizeof(SpecialEntry));
                    if (!entry)
                    {
                        bbpe_destroy(tok);
                        cJSON_Delete(root);
                        return BBPE_ERR_MEMORY;
                    }
                    entry->token = strdup(content->valuestring);
                    if (!entry->token)
                    {
                        free(entry);
                        bbpe_destroy(tok);
                        cJSON_Delete(root);
                        return BBPE_ERR_MEMORY;
                    }
                    entry->id = sid;
                    HASH_ADD_KEYPTR(hh, tok->special_tokens_map, entry->token, strlen(entry->token), entry);
                    tok->id_to_token[sid] = entry->token;
                }
            }
        }
    }

    *out_tokenizer = tok;
    cJSON_Delete(root);
    return BBPE_OK;
}

BBPEStatus bbpe_encode(BBPETokenizer *tokenizer, const char *text, BBPEOutput *out_output)
{
    if (!tokenizer || !text || !out_output)
        return BBPE_ERR_INVALID_INPUT;

    out_output->ids = NULL;
    out_output->count = 0;

    size_t seg_count = 0;
    TokenSegment *segments = extract_special_tokens(tokenizer, text, &seg_count);
    if (!segments)
        return BBPE_ERR_MEMORY;

    BBPEStatus status = BBPE_OK;

    for (size_t i = 0; i < seg_count; i++)
    {
        if (segments[i].is_special)
        {
            // 特殊 token 直接添加 ID
            size_t old_count = out_output->count;
            out_output->count += 1;
            int32_t *new_ids = (int32_t *)realloc(out_output->ids, sizeof(int32_t) * out_output->count);
            if (!new_ids)
            {
                status = BBPE_ERR_MEMORY;
                break;
            }
            out_output->ids = new_ids;
            out_output->ids[old_count] = segments[i].special_id;
        }
        else
        {
            // 普通文本段：预分词后分别编码
            PreTokenizedResult pre_res;
            status = pre_tokenize(tokenizer, segments[i].text, &pre_res);
            if (status != BBPE_OK)
                break;

            for (size_t j = 0; j < pre_res.count; j++)
            {
                status = encode_chunk(tokenizer, pre_res.chunks[j], out_output);
                if (status != BBPE_OK)
                {
                    free_pre_tokenized(&pre_res);
                    break;
                }
            }
            free_pre_tokenized(&pre_res);
            if (status != BBPE_OK)
                break;
        }
    }

    // 释放 segments 中的文本
    for (size_t i = 0; i < seg_count; i++)
    {
        if (!segments[i].is_special && segments[i].text)
            free(segments[i].text);
    }
    free(segments);

    if (status != BBPE_OK)
        bbpe_free_output(out_output);

    return status;
}

BBPEStatus bbpe_decode(BBPETokenizer *tokenizer, const int32_t *ids, size_t count, char **out_text)
{
    if (!tokenizer || !ids || count == 0 || !out_text)
        return BBPE_ERR_INVALID_INPUT;

    // 第一遍：计算解码后需要的总字节数
    size_t total_bytes = 0;
    for (size_t i = 0; i < count; i++)
    {
        int32_t id = ids[i];
        if (id < 0 || id >= tokenizer->vocab_size)
            return BBPE_ERR_TOKEN_NOT_FOUND;
        const char *token_str = tokenizer->id_to_token[id];
        if (!token_str)
            return BBPE_ERR_TOKEN_NOT_FOUND;

        const char *p = token_str;
        while (*p)
        {
            uint32_t cp;
            int len = utf8_decode(p, &cp);
            if (len < 0)
                return BBPE_ERR_INVALID_INPUT;

            if (cp < UNICODE_MAP_SIZE && tokenizer->unicode_to_byte[cp] != 0)
            {
                total_bytes += 1; // 映射回原始字节
            }
            else
            {
                total_bytes += len; // 保留原 UTF-8 字符
            }
            p += len;
        }
    }

    char *result = (char *)malloc(total_bytes + 1);
    if (!result)
        return BBPE_ERR_MEMORY;

    // 第二遍：实际解码并填充 result
    size_t pos = 0;
    for (size_t i = 0; i < count; i++)
    {
        int32_t id = ids[i];
        const char *token_str = tokenizer->id_to_token[id];
        const char *p = token_str;
        while (*p)
        {
            uint32_t cp;
            int len = utf8_decode(p, &cp);
            if (len < 0)
            {
                free(result);
                return BBPE_ERR_INVALID_INPUT;
            }

            if (cp < UNICODE_MAP_SIZE && tokenizer->unicode_to_byte[cp] != 0)
            {
                result[pos++] = (char)tokenizer->unicode_to_byte[cp];
            }
            else
            {
                memcpy(result + pos, p, len);
                pos += len;
            }
            p += len;
        }
    }
    result[pos] = '\0';
    *out_text = result;
    return BBPE_OK;
}

void bbpe_free_output(BBPEOutput *output)
{
    if (output)
    {
        free(output->ids);
        output->ids = NULL;
        output->count = 0;
    }
}

void bbpe_destroy(BBPETokenizer *tokenizer)
{
    if (!tokenizer)
        return;

    // 释放预计算的字节字符串
    for (int i = 0; i < 256; i++)
        free(tokenizer->byte_vocab_strs[i]);

    VocabEntry *v_cur, *v_tmp;
    HASH_ITER(hh, tokenizer->vocab_map, v_cur, v_tmp)
    {
        HASH_DEL(tokenizer->vocab_map, v_cur);
        free(v_cur->token);
        free(v_cur);
    }

    SpecialEntry *s_cur, *s_tmp;
    HASH_ITER(hh, tokenizer->special_tokens_map, s_cur, s_tmp)
    {
        HASH_DEL(tokenizer->special_tokens_map, s_cur);
        free(s_cur->token);
        free(s_cur);
    }

    PreTokenizerNode *p_cur = tokenizer->pre_tokenizers;
    while (p_cur)
    {
        PreTokenizerNode *p_next = p_cur->next;
        if (p_cur->type == PRE_TOKENIZER_REGEX_SPLIT)
        {
            free(p_cur->config.split.regex_pattern);
            if (p_cur->config.split.regex_compiled)
                pcre2_code_free(p_cur->config.split.regex_compiled);
        }
        free(p_cur);
        p_cur = p_next;
    }

    if (tokenizer->rule_rows)
    {
        for (uint32_t i = 0; i < tokenizer->vocab_size; i++)
        {
            free(tokenizer->rule_rows[i].items);
        }
        free(tokenizer->rule_rows);
    }

    free(tokenizer->id_to_token);
    free(tokenizer);
}
