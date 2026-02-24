/* src/config.h */
#ifndef PCRE2_CONFIG_H
#define PCRE2_CONFIG_H

/* 基本功能：启用 8 位库和 Unicode 支持 */
#define SUPPORT_PCRE2_8 1
/* #undef SUPPORT_PCRE2_16 */ /* 如需 16 位，改为 #define SUPPORT_PCRE2_16 1 */
/* #undef SUPPORT_PCRE2_32 */ /* 如需 32 位，改为 #define SUPPORT_PCRE2_32 1 */
#define SUPPORT_UNICODE 1

/* 禁用 JIT */
/* #undef SUPPORT_JIT */

/* 不启用 Valgrind、差分 fuzzing 等 */
/* #undef SUPPORT_VALGRIND */
/* #undef SUPPORT_DIFF_FUZZ */

/* ==================== 平台相关的头文件存在性 ==================== */
#ifdef _WIN32
/* Windows 特有 */
#define HAVE_WINDOWS_H 1
#define HAVE_WCHAR_H 1
#define HAVE_STDINT_H 1 /* VS2010+ 自带；也可用 pstdint.h */
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ASSERT_H 1
#define HAVE_SYS_STAT_H 1 /* MinGW 等环境可能有 */
#define HAVE_SYS_TYPES_H 1
/* Windows 通常没有以下头文件 */
#define HAVE_UNISTD_H 0
#define HAVE_STRINGS_H 0
#define HAVE_DIRENT_H 0
#define HAVE_SYS_WAIT_H 0
#define HAVE_SECURE_GETENV 0
#define HAVE_REALPATH 0
#else
/* 假设是 Android/Linux 或其他类 Unix 系统 */
#define HAVE_UNISTD_H 1
#define HAVE_STRINGS_H 1
#define HAVE_DIRENT_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SECURE_GETENV 0 /* Android Bionic 可能没有 */
#define HAVE_REALPATH 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ASSERT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WCHAR_H 1
/* Windows 特有头文件不存在 */
#define HAVE_WINDOWS_H 0
#endif

/* ==================== 扩展库支持（zlib、bz2 等）==================== */
/* 如需要，可取消注释并确保对应库已安装 */
/* #define HAVE_ZLIB_H 1 */
/* #define HAVE_BZLIB_H 1 */
/* #define SUPPORT_LIBZ 1 */
/* #define SUPPORT_LIBBZ2 1 */

/* ==================== 编译限制（通常无需修改）==================== */
#define HEAP_LIMIT 20000000
#define LINK_SIZE 2
#define MATCH_LIMIT 10000000
#define MATCH_LIMIT_DEPTH MATCH_LIMIT
#define MAX_NAME_COUNT 10000
#define MAX_NAME_SIZE 128
#define MAX_VARLOOKBEHIND 255
#define NEWLINE_DEFAULT 2 /* LF */
#define PARENS_NEST_LIMIT 250
#define PCRE2GREP_BUFSIZE 20480
#define PCRE2GREP_MAX_BUFSIZE 1048576

/* ==================== 包信息 ==================== */
#define PACKAGE "pcre2"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "PCRE2"
#define PACKAGE_STRING "PCRE2 10.47"
#define PACKAGE_TARNAME "pcre2"
#define PACKAGE_URL ""
#define VERSION "10.47"

/* ==================== 导出/导入符号 ==================== */
#ifdef _WIN32
/* Windows 下默认使用 __declspec(dllexport/import) 机制，PCRE2_EXPORT 可为空 */
#define PCRE2_EXPORT
/* 如果编译静态库，请取消下面一行的注释 */
/* #define PCRE2_STATIC 1 */
#else
/* Android/Unix 使用可见性属性 */
#define PCRE2_EXPORT __attribute__((visibility("default")))
#endif

/* ==================== 其他选项（保持默认）==================== */
/* #undef BSR_ANYCRLF */
/* #undef DISABLE_PERCENT_ZT */
/* #undef EBCDIC */
/* #undef EBCDIC_IGNORING_COMPILER */
/* #undef EBCDIC_NL25 */
/* #undef NEVER_BACKSLASH_C */
/* #undef PCRE2_DEBUG */
/* #undef SLJIT_PROT_EXECUTABLE_ALLOCATOR */

/* 为了在 Android 上启用必要的 GNU 扩展，可以定义 _GNU_SOURCE */
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#endif