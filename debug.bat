@echo off
setlocal enabledelayedexpansion
gcc -g -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -Ithirdparty/cJSON -Ithirdparty/uthash -Ithirdparty/pcre2 -I. -o bbpe.exe bbpe_tokenizer.c main.c thirdparty/cJSON/*.c thirdparty/pcre2/*.c
bbpe qwen3-tokenizer.json
pause