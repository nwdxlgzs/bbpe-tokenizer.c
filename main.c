#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbpe_tokenizer.h"

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
  printf("BBPETokenizer: %p\n", tokenizer);
  // 编码
  BBPEOutput output;
  // const char* RAWSTR = "\n \n\n \n\n\n \t \t\t \t\n  \n   \n    \n     \n🚀 (normal) 😶\u200d🌫️ (multiple emojis concatenated) ✅ 🦙🦙 3 33 333 3333 33333 333333 3333333 33333333 3.3 3..3 3...3 កាន់តែពិសេសអាច😁 ?我想在apple工作1314151天～ ------======= нещо на Български \'\'\'\'\'\'```````\"\"\"\"......!!!!!!?????? I\'ve been \'told he\'s there, \'RE you sure? \'M not sure I\'ll make it, \'D you like some tea? We\'Ve a\'lL";
  const char* RAWSTR = "你好<|endoftext|><<|endoftext|>";

  status = bbpe_encode(tokenizer, RAWSTR, &output);

  if (status == BBPE_OK)
  {
    printf("Token IDs: [");
    for (size_t i = 0; i < output.count; i++)
    {
      printf("%d%s", output.ids[i], i < output.count - 1 ? ", " : "");
    }
    printf("]\n");
    // 解码
    // BBPEStatus bbpe_decode(BBPETokenizer *tokenizer, const int32_t *ids, size_t count, char **out_text)
    char *text = NULL;
    status = bbpe_decode(tokenizer, output.ids, output.count, &text);
    if (status == BBPE_OK)
    {
      printf("Decoded text: %s OK? %s\n", text, strcmp(text, RAWSTR) == 0 ? "YES" : "NO");
      free(text);
    }
    else
    {
      fprintf(stderr, "Decoding failed: %d\n", status);
    }
    bbpe_free_output(&output);
  }
  else
  {
    fprintf(stderr, "Encoding failed: %d\n", status);
  }

  bbpe_destroy(tokenizer);
  return 0;
}
