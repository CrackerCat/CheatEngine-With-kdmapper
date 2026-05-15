cmake_minimum_required(VERSION 3.20)

project(CheatEngineQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 让 clangd/clang-tidy 等工具能够分析代码
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ── 查找 Qt 翻译工具 ──
find_program(QT_LUPDATE_EXECUTABLE
    NAMES lupdate lupdate6
    HINTS "${Qt6Widgets_DIR}/../../../bin"
          "${Qt6_DIR}/../../../bin"
          "${CMAKE_PREFIX_PATH}/bin"
)

file(GLOB_RECURSE SOURCES
    *.cpp
)

file(GLOB_RECURSE HEADERS
    *.h
)

file(GLOB_RECURSE UI_FILES
    ui/*.ui
)

file(GLOB_RECURSE QRC_FILES
    resource/*.qrc
)

# 排除旧 VS 构建产物（x64/ 目录），避免与 AUTOMOC 生成的 moc 文件冲突
list(FILTER SOURCES EXCLUDE REGEX "/x64/")
list(FILTER HEADERS EXCLUDE REGEX "/x64/")
# 排除 build 目录中的文件（比如 CMakeCXXCompilerId.cpp）
list(FILTER SOURCES EXCLUDE REGEX "${CMAKE_BINARY_DIR}/.*")
list(FILTER HEADERS EXCLUDE REGEX "${CMAKE_BINARY_DIR}/.*")
# 排除任何路径中包含 moc_ 的 .cpp 文件（AUTOMOC 统一管理）
list(FILTER SOURCES EXCLUDE REGEX "/moc_.*\\.cpp$")

add_executable(CheatEngineQt
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${QRC_FILES}
)

# 设置 Windows 子系统（不显示控制台窗口）
set_target_properties(CheatEngineQt PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_include_directories(CheatEngineQt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(CheatEngineQt PRIVATE
    Qt6::Widgets
)

# ---- 生成后事件：自动拷贝 Qt DLL 到输出目录 ----
# 先通过 windeployqt 自动部署所有 Qt 依赖
find_program(QT_DEPLOY_EXECUTABLE windeployqt REQUIRED HINTS
    "${Qt6Widgets_DIR}/../../../bin"
    "${CMAKE_PREFIX_PATH}/bin"
)

# 添加一个自定义命令，在编译完成后自动运行 windeployqt
add_custom_command(TARGET CheatEngineQt POST_BUILD
    COMMAND "${QT_DEPLOY_EXECUTABLE}"
        \"$<TARGET_FILE:CheatEngineQt>\"
        --verbose 0
        --no-compiler-runtime
        --no-translations
        --no-opengl-sw
    COMMENT "正在部署 Qt 依赖的 DLL ..."
)

# ── 生成后事件：自动更新 .ts 翻译文件（仅在 lupdate 可用时执行） ──
if(QT_LUPDATE_EXECUTABLE)
    set(TRANSLATION_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/language_translations")
    set(TS_FILE "${TRANSLATION_OUTPUT_DIR}/Translation_en.ts")

    # 每次构建时自动运行 lupdate，扫描整个源码目录提取 tr() 字符串，更新 .ts 文件
    add_custom_command(TARGET CheatEngineQt POST_BUILD
        COMMAND "${QT_LUPDATE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            -ts "${TS_FILE}"
            -noobsolete
            -locations relative
        COMMENT "正在更新翻译文件 Translation_en.ts ..."
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

# 拷贝 MSVC 运行时库 (如果需要独立运行)
if (MSVC)
    include(InstallRequiredSystemLibraries)
endif()

if (MSVC)
    target_compile_options(CheatEngineQt PRIVATE
        /utf-8
        /MP
        /EHsc
        # 强制所有配置生成调试符号（PDB），Release 也能加断点
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(CheatEngineQt PRIVATE
        $<$<CONFIG:Release>:/DEBUG>
    )
endif()