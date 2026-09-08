# MicroTeX ハンドラ（任意ターゲット。本体 typeset には依存を足さない）
#
#   cmake -DTYPESET_HANDLER_MICROTEX=ON -DVCPKG_MANIFEST_FEATURES=microtex ...
#   開発中に再取得を避けるなら -DFETCHCONTENT_SOURCE_DIR_MICROTEX=<ローカルの MicroTeX ツリー>
#
# MicroTeX（MIT。res/ のフォントは OFL / Knuth ライセンス）の CMakeLists は使わず、
# src/ のうち platform/ と samples/ を除いたものを静的ライブラリにする。描画抽象
# （tex::Font / tex::TextLayout / tex::Graphics2D）は microtex_handler.cpp が実装する。

include(FetchContent)

set(TYPESET_MICROTEX_GIT_REPOSITORY "https://github.com/NanoMichael/MicroTeX.git" CACHE STRING "")
set(TYPESET_MICROTEX_GIT_TAG "0e3707f6dafebb121d98b53c64364d16fefe481d" CACHE STRING "")

FetchContent_Declare(microtex
    GIT_REPOSITORY ${TYPESET_MICROTEX_GIT_REPOSITORY}
    GIT_TAG ${TYPESET_MICROTEX_GIT_TAG}
    GIT_SHALLOW OFF
    SOURCE_SUBDIR "cmake-not-used"     # MicroTeX 側の CMakeLists.txt を読まない
)
FetchContent_MakeAvailable(microtex)

file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS ${microtex_SOURCE_DIR}/src/*.cpp)
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/(platform|samples)/")

find_package(tinyxml2 CONFIG REQUIRED)

add_library(microtex_core STATIC ${MICROTEX_SOURCES})
target_include_directories(microtex_core PUBLIC ${microtex_SOURCE_DIR}/src)
target_link_libraries(microtex_core PUBLIC tinyxml2::tinyxml2)
target_compile_features(microtex_core PUBLIC cxx_std_17)
target_compile_definitions(microtex_core PUBLIC NDEBUG)   # CLATEX_DEBUG（デバッグ描画）を切る
if(MSVC)
    target_compile_options(microtex_core PRIVATE /utf-8 /W0 /wd4996)
    target_compile_definitions(microtex_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(microtex_core PRIVATE -w)
endif()

add_library(typeset_microtex STATIC microtex_handler.cpp)
target_include_directories(typeset_microtex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typeset_microtex PUBLIC typeset microtex_core)
target_compile_definitions(typeset_microtex PRIVATE
    TYPESET_MICROTEX_RES_DIR="${microtex_SOURCE_DIR}/res")
if(MSVC)
    target_compile_options(typeset_microtex PRIVATE /utf-8)
endif()

if(TYPESET_BUILD_SAMPLES)
    add_executable(sample_microtex sample_microtex.cpp)
    target_link_libraries(sample_microtex PRIVATE typeset_microtex)
    target_include_directories(sample_microtex PRIVATE ${CMAKE_SOURCE_DIR}/samples)
    if(MSVC)
        target_compile_options(sample_microtex PRIVATE /utf-8)
    endif()
endif()
