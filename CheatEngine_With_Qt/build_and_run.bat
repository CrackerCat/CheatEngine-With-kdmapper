@echo off
chcp 65001 >nul

:: 接收参数：Debug（默认）或 Release
set BUILD_TYPE=%~1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug
if /i "%BUILD_TYPE%"=="Debug"   set BUILD_TYPE=Debug
if /i "%BUILD_TYPE%"=="Release" set BUILD_TYPE=Release
if /i "%BUILD_TYPE%"=="debug"   set BUILD_TYPE=Debug
if /i "%BUILD_TYPE%"=="release" set BUILD_TYPE=Release

:: 根据构建类型选择 CMake preset
if /i "%BUILD_TYPE%"=="Release" (
    set CMAKE_PRESET=release
) else (
    set CMAKE_PRESET=default
)

title CheatEngineQt 一键构建 [%BUILD_TYPE%]

echo ========================================
echo     CheatEngineQt \%BUILD_TYPE% 构建
echo ========================================
echo.

REM 绕过 Qt 许可证检查（开源版 Qt 需要此环境变量）
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1

:: 1. 加载 MSVC 环境
set VCVARSPATH=D:\VS2022\VS2022\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARSPATH%" (
    echo [错误] 找不到 vcvars64.bat: %VCVARSPATH%
    pause
    exit /b 1
)
echo [1/4] 加载 MSVC 编译环境...
call "%VCVARSPATH%"

REM 把 Qt 的 bin 目录加到 PATH（让 windeployqt 能找到）
set PATH=E:\Qt\6.8.2\msvc2022_64\bin;%PATH%

:: 2. CMake 配置（VS 多配置生成器，Debug/Release 可共存，无需清空 build 目录）
echo [2/4] 配置 CMake (%CMAKE_PRESET%)...
cmake --preset %CMAKE_PRESET%
if %errorlevel% neq 0 (
    echo [错误] CMake 配置失败!
    pause
    exit /b 1
)

:: 3. 编译
echo [3/4] 编译项目 (%BUILD_TYPE%)...
cmake --build build --config %BUILD_TYPE%
if %errorlevel% neq 0 (
    echo [错误] 编译失败!
    pause
    exit /b 1
)

echo.
echo [%BUILD_TYPE%] 构建成功! 按任意键关闭此窗口，然后按 F5 调试启动程序。
echo.
::pause
