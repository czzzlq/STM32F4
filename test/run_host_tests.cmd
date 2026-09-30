@echo off
rem ============================================================
rem  Host-side regression tests for pure-logic modules (Windows)
rem  Equivalent to test/run_host_tests.sh, and immune to the
rem  PowerShell execution policy.
rem  Requires: mingw gcc (or any host gcc) on PATH
rem  Usage: test\run_host_tests.cmd
rem
rem  NOTE: this file is intentionally ASCII-only. cmd.exe parses
rem  batch files using the OEM code page, so UTF-8 Chinese text
rem  here would corrupt the parsing. Chinese notes live in
rem  test/README.txt and the project README instead.
rem ============================================================
setlocal

rem Move to the repository root (this script lives in test\)
pushd "%~dp0.." || exit /b 1

if "%GCC%"=="" set GCC=gcc
set CFLAGS=-std=c99 -O2 -Wall -Wextra

set BUILD_DIR=%TEMP%\hosttests_%RANDOM%%RANDOM%
mkdir "%BUILD_DIR%" >nul 2>&1

echo ===== 1) malloc =====
"%GCC%" %CFLAGS% -Itest/stubs -Icommon -Ios/os_wrapper -Imiddleware/MALLOC middleware/MALLOC/malloc.c test/test_malloc.c test/test_stubs.c -o "%BUILD_DIR%\test_malloc.exe" || goto :fail
"%BUILD_DIR%\test_malloc.exe" || goto :fail

echo ===== 2) ringbuf =====
"%GCC%" %CFLAGS% -Iutility utility/ringbuf.c test/test_ringbuf.c -o "%BUILD_DIR%\test_ringbuf.exe" || goto :fail
"%BUILD_DIR%\test_ringbuf.exe" || goto :fail

echo ===== 3) key =====
"%GCC%" %CFLAGS% -Itest/stubs -Icommon -Idriver/KEY -Idriver/TIMER -Ios/os_wrapper driver/KEY/drv_key.c driver/TIMER/timer_callback.c test/test_key.c test/test_stubs.c test/test_key_stubs.c -o "%BUILD_DIR%\test_key.exe" || goto :fail
"%BUILD_DIR%\test_key.exe" || goto :fail

rmdir /s /q "%BUILD_DIR%" >nul 2>&1
popd
echo ALL HOST TESTS PASSED
exit /b 0

:fail
echo.
echo [FAILED] compile error or test failure, see output above
rmdir /s /q "%BUILD_DIR%" >nul 2>&1
popd
exit /b 1
