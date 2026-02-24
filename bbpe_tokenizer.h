#ifndef BBPE_TOKENIZER_H
#define BBPE_TOKENIZER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 错误码定义
     */
    typedef enum
    {
        BBPE_OK = 0,                   /* 成功 */
        BBPE_ERR_MEMORY = -1,          /* 内存分配失败 */
        BBPE_ERR_JSON_PARSE = -2,      /* JSON 解析失败 */
        BBPE_ERR_VOCAB_MISSING = -3,   /* 词汇表缺失 */
        BBPE_ERR_REGEX_COMPILE = -4,   /* 正则表达式编译失败 */
        BBPE_ERR_TOKEN_NOT_FOUND = -5, /* Token 未找到 */
        BBPE_ERR_INVALID_INPUT = -6,   /* 输入参数无效 */
        BBPE_ERR_UNSUPPORTED_TYPE = -7 /* 不支持的预分词器类型 */
    } BBPEStatus;

    /**
     * @brief 分词结果结构
     */
    typedef struct
    {
        int32_t *ids; /* token ID 数组，由调用者通过 bbpe_free_output 释放 */
        size_t count; /* ID 数量 */
    } BBPEOutput;

    /**
     * @brief 分词器句柄 (不透明指针)
     */
    typedef struct BBPETokenizer BBPETokenizer;

    /**
     * @brief 从 JSON 字符串初始化分词器
     * @param json_content tokenizer.json 的完整内容字符串 (UTF-8)
     * @param out_tokenizer 输出分词器句柄的指针，成功时指向新创建的对象
     * @return BBPEStatus 状态码
     */
    BBPEStatus bbpe_init(const char *json_content, BBPETokenizer **out_tokenizer);

    /**
     * @brief 执行分词推理 (文本 → token IDs)
     * @param tokenizer 分词器句柄
     * @param text 输入文本 (UTF-8)
     * @param out_output 输出结果结构，包含 ids 数组和数量，使用后需调用 bbpe_free_output 释放
     * @return BBPEStatus 状态码
     */
    BBPEStatus bbpe_encode(BBPETokenizer *tokenizer, const char *text, BBPEOutput *out_output);

    /**
     * @brief 将 token ID 序列解码回原始文本 (token IDs → 文本)
     * @param tokenizer 分词器句柄
     * @param ids 输入的 ID 数组
     * @param count ID 数量
     * @param out_text 输出文本字符串 (UTF-8)，调用者需使用 free() 释放
     * @return BBPEStatus 状态码
     */
    BBPEStatus bbpe_decode(BBPETokenizer *tokenizer, const int32_t *ids, size_t count, char **out_text);

    /**
     * @brief 释放分词结果内存
     * @param output 分词结果结构，ids 成员将被释放，结构本身不释放
     */
    void bbpe_free_output(BBPEOutput *output);

    /**
     * @brief 释放分词器资源
     * @param tokenizer 分词器句柄
     */
    void bbpe_destroy(BBPETokenizer *tokenizer);

#ifdef __cplusplus
}
#endif

#endif // BBPE_TOKENIZER_H