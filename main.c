#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbpe_tokenizer.h"

static const char *SAVE_FILE = "tokenizer_saved.bin";

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s tokenizer.json\n", argv[0]);
    return 1;
  }

  const char *json_path = argv[1];
  printf("Loading tokenizer from %s...\n", json_path);
  // 读取 JSON 文件内容
  FILE *fp = fopen(json_path, "r");
  if (!fp)
  {
    perror("fopen");
    return 1;
  }
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *json_content = malloc(fsize + 1);
  if (!json_content)
  {
    perror("malloc");
    fclose(fp);
    return 1;
  }
  size_t read_size = fread(json_content, 1, fsize, fp);
  fclose(fp);
  if (read_size != (size_t)fsize)
  {
    fprintf(stderr, "Failed to read entire file\n");
    free(json_content);
    return 1;
  }
  json_content[fsize] = '\0';

  // 初始化 tokenizer
  BBPETokenizer *tokenizer = NULL;
  BBPEStatus status = bbpe_init(json_content, &tokenizer);
  free(json_content); // bbpe_init 内部已解析，可释放

  if (status != BBPE_OK)
  {
    fprintf(stderr, "Failed to init tokenizer: %d\n", status);
    return 1;
  }
  printf("BBPETokenizer initialized: %p\n", tokenizer);

  const char *RAWSTR = "\n \n\n \n\n\n \t \t\t \t\n  \n   \n    \n     \n🚀 (normal) 😶\u200d🌫️ (multiple emojis concatenated) ✅ 🦙🦙 3 33 333 3333 33333 333333 3333333 33333333 3.3 3..3 3...3 កាន់តែពិសេសអាច😁 ?我想在apple工作1314151天～ ------======= нещо на Български \'\'\'\'\'\'```````\"\"\"\"......!!!!!!?????? I\'ve been \'told he\'s there, \'RE you sure? \'M not sure I\'ll make it, \'D you like some tea? We\'Ve a\'lL";

  // ---------- 第一次编码 ----------
  BBPEOutput output;
  status = bbpe_encode(tokenizer, RAWSTR, &output);

  if (status != BBPE_OK)
  {
    fprintf(stderr, "Encoding failed: %d\n", status);
    bbpe_destroy(tokenizer);
    return 1;
  }

  printf("First encoding: Token IDs count = %zu\n", output.count);
  printf("Token IDs: [");
  for (size_t i = 0; i < output.count; i++)
  {
    printf("%d%s", output.ids[i], i < output.count - 1 ? ", " : "");
  }
  printf("]\n");

  // 解码验证
  char *text = NULL;
  status = bbpe_decode(tokenizer, output.ids, output.count, &text);
  if (status == BBPE_OK)
  {
    printf("Decoded text: %s\n", text);
    printf("Decoding matches original? %s\n", strcmp(text, RAWSTR) == 0 ? "YES" : "NO");
    free(text);
  }
  else
  {
    fprintf(stderr, "Decoding failed: %d\n", status);
    bbpe_free_output(&output);
    bbpe_destroy(tokenizer);
    return 1;
  }

  // 保存第一次的 ids 用于后续比较
  int32_t *first_ids = (int32_t *)malloc(output.count * sizeof(int32_t));
  if (!first_ids)
  {
    fprintf(stderr, "Memory allocation failed\n");
    bbpe_free_output(&output);
    bbpe_destroy(tokenizer);
    return 1;
  }
  memcpy(first_ids, output.ids, output.count * sizeof(int32_t));
  size_t first_count = output.count;
  bbpe_free_output(&output);

  // ---------- 序列化保存 ----------
  printf("\nSaving tokenizer to %s...\n", SAVE_FILE);
  status = bbpe_save(tokenizer, SAVE_FILE);
  if (status != BBPE_OK)
  {
    fprintf(stderr, "bbpe_save failed: %d\n", status);
    free(first_ids);
    bbpe_destroy(tokenizer);
    return 1;
  }
  printf("Save successful.\n");

  // 销毁原始 tokenizer
  bbpe_destroy(tokenizer);
  tokenizer = NULL;

  // ---------- 从保存的文件加载 ----------
  printf("Loading tokenizer from %s...\n", SAVE_FILE);
  status = bbpe_load(SAVE_FILE, &tokenizer);
  if (status != BBPE_OK)
  {
    fprintf(stderr, "bbpe_load failed: %d\n", status);
    free(first_ids);
    remove(SAVE_FILE);
    return 1;
  }
  printf("Load successful.\n");

  // 可选删除临时文件
  remove(SAVE_FILE);

  // ---------- 再次编码同一文本 ----------
  BBPEOutput output2;
  status = bbpe_encode(tokenizer, RAWSTR, &output2);
  if (status != BBPE_OK)
  {
    fprintf(stderr, "Second encoding failed: %d\n", status);
    free(first_ids);
    bbpe_destroy(tokenizer);
    return 1;
  }

  printf("\nSecond encoding: Token IDs count = %zu\n", output2.count);
  printf("Token IDs: [");
  for (size_t i = 0; i < output2.count; i++)
  {
    printf("%d%s", output2.ids[i], i < output2.count - 1 ? ", " : "");
  }
  printf("]\n");

  // 比较两次编码结果
  int match = (first_count == output2.count) &&
              (memcmp(first_ids, output2.ids, first_count * sizeof(int32_t)) == 0);
  printf("\nSerialization test: %s\n", match ? "PASS" : "FAIL");

  // 释放资源
  free(first_ids);
  bbpe_free_output(&output2);
  bbpe_destroy(tokenizer);

  return 0;
}
