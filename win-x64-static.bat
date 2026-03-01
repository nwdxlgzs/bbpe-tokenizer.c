@echo off
setlocal EnableDelayedExpansion

:: 架构选择
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"

:: 设置架构标志
if /I "%ARCH%"=="x86" (
    set "ARCH_FLAGS=-m32"
    set "TARGET=x86"
    goto :build
)
if /I "%ARCH%"=="i686" (
    set "ARCH_FLAGS=-m32"
    set "TARGET=x86"
    goto :build
)
if /I "%ARCH%"=="x64" (
    set "ARCH_FLAGS=-m64"
    set "TARGET=x64"
    goto :build
)
if /I "%ARCH%"=="x86_64" (
    set "ARCH_FLAGS=-m64"
    set "TARGET=x64"
    goto :build
)

echo Usage: build.bat [x86^|x64]
exit /b 1

:build
echo Building for: %TARGET%

set "CFLAGS=-g %ARCH_FLAGS% -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -Ithirdparty/cJSON -Ithirdparty/uthash -Ithirdparty/pcre2 -I."

:: 创建临时目录
set "TMPDIR=build_tmp_%RANDOM%"
mkdir "%TMPDIR%\cJSON" "%TMPDIR%\pcre2"

echo === Compiling ===

:: 核心文件
gcc -c %CFLAGS% bbpe_tokenizer.c -o "%TMPDIR%\bbpe_tokenizer.o"

:: cJSON
for %%f in (thirdparty\cJSON\*.c) do (
    gcc -c %CFLAGS% "%%f" -o "%TMPDIR%\cJSON\%%~nf.o"
)

:: pcre2
for %%f in (thirdparty\pcre2\*.c) do (
    gcc -c %CFLAGS% "%%f" -o "%TMPDIR%\pcre2\%%~nf.o"
)

echo === Creating static library ===

:: 收集所有 .o 文件
set "OBJ_FILES="
for %%f in ("%TMPDIR%\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"
for %%f in ("%TMPDIR%\cJSON\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"
for %%f in ("%TMPDIR%\pcre2\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"

:: 创建静态库
ar rcs "libbbpe_%TARGET%.a" %OBJ_FILES%

echo === Cleaning up ===

:: 删除临时目录
rmdir /S /Q "%TMPDIR%"

echo === Done ===
echo Output: libbbpe_%TARGET%.a