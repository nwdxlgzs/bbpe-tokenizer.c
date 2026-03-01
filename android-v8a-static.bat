@echo off
setlocal EnableDelayedExpansion

:: 设置 NDK 路径（修改为你的路径）
if not defined NDK_HOME (
    if not defined ANDROID_NDK_HOME (
        set "NDK=D:\OLLVM\ndk-r27c-ollvm"
    ) else (
        set "NDK=%ANDROID_NDK_HOME%"
    )
) else (
    set "NDK=%NDK_HOME%"
)

if not exist "%NDK%" (
    echo Error: NDK not found at %NDK%
    echo Please set NDK_HOME or ANDROID_NDK_HOME environment variable
    exit /b 1
)

:: 目标架构
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=arm64-v8a"

:: 设置架构参数
if /I "%ARCH%"=="armeabi-v7a" goto :armv7
if /I "%ARCH%"=="armv7" goto :armv7
if /I "%ARCH%"=="v7a" goto :armv7
if /I "%ARCH%"=="arm64-v8a" goto :arm64
if /I "%ARCH%"=="arm64" goto :arm64
if /I "%ARCH%"=="v8a" goto :arm64
if /I "%ARCH%"=="aarch64" goto :arm64

echo Usage: build_android.bat [armeabi-v7a^|arm64-v8a]
exit /b 1

:armv7
set "TARGET_ARCH=armeabi-v7a"
set "TARGET_TRIPLE=arm-linux-androideabi"
set "TARGET_TRIPLE_CLANG=armv7a-linux-androideabi"
set "API_LEVEL=21"
set "ARCH_FLAGS=-march=armv7-a -mfpu=neon -mfloat-abi=softfp"
goto :build

:arm64
set "TARGET_ARCH=arm64-v8a"
set "TARGET_TRIPLE=aarch64-linux-android"
set "TARGET_TRIPLE_CLANG=aarch64-linux-android"
set "API_LEVEL=21"
set "ARCH_FLAGS=-march=armv8.2-a+fp16+dotprod"
goto :build

:build
echo Building for: %TARGET_ARCH%
echo NDK: %NDK%

:: 设置工具链
set "TOOLCHAIN=%NDK%\toolchains\llvm\prebuilt\windows-x86_64"

set "CC=%TOOLCHAIN%\bin\%TARGET_TRIPLE_CLANG%%API_LEVEL%-clang.cmd"
set "CXX=%TOOLCHAIN%\bin\%TARGET_TRIPLE_CLANG%%API_LEVEL%-clang++.cmd"
set "AR=%TOOLCHAIN%\bin\llvm-ar.exe"
set "RANLIB=%TOOLCHAIN%\bin\llvm-ranlib.exe"

:: 检查编译器
if not exist "%CC%" (
    echo Error: Compiler not found: %CC%
    exit /b 1
)

echo CC: %CC%

:: 编译标志
set "CFLAGS=-g -O3 -fPIC -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -Ithirdparty/cJSON -Ithirdparty/uthash -Ithirdparty/pcre2 -I. %ARCH_FLAGS%"

:: 创建临时目录
set "TMPDIR=build_tmp_%TARGET_ARCH%_%RANDOM%"
mkdir "%TMPDIR%\cJSON" "%TMPDIR%\pcre2"

echo === Compiling ===

:: 核心文件
call %CC% -c %CFLAGS% bbpe_tokenizer.c -o "%TMPDIR%\bbpe_tokenizer.o"

:: cJSON
for %%f in (thirdparty\cJSON\*.c) do (
    call %CC% -c %CFLAGS% "%%f" -o "%TMPDIR%\cJSON\%%~nf.o"
)

:: pcre2
for %%f in (thirdparty\pcre2\*.c) do (
    call %CC% -c %CFLAGS% "%%f" -o "%TMPDIR%\pcre2\%%~nf.o"
)

echo === Creating static library ===

:: 收集所有 .o 文件
set "OBJ_FILES="
for %%f in ("%TMPDIR%\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"
for %%f in ("%TMPDIR%\cJSON\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"
for %%f in ("%TMPDIR%\pcre2\*.o") do set "OBJ_FILES=!OBJ_FILES! %%f"

:: 创建静态库
call %AR% rcs "libbbpe_%TARGET_ARCH%.a" %OBJ_FILES%
call %RANLIB% "libbbpe_%TARGET_ARCH%.a"

echo === Cleaning up ===

:: 删除临时目录
rmdir /S /Q "%TMPDIR%"

echo === Done ===
echo Output: libbbpe_%TARGET_ARCH%.a