cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
endif()

project(typeset VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TYPESET_BUILD_SAMPLES "Build sample executables" ${PROJECT_IS_TOP_LEVEL})
option(TYPESET_BUILD_TESTS "Build unit tests (doctest)" ${PROJECT_IS_TOP_LEVEL})
option(TYPESET_BUILD_PYTHON "Build pybind11 Python bindings" OFF)

if(TYPESET_BUILD_PYTHON)
    # 拡張モジュールに静的ライブラリをリンクするので全部 PIC にする
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

#------------------------------------------------------------------------------
# 依存
#------------------------------------------------------------------------------
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libunibreak CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

# glyphware（フォントエンジン）。GLYPHWARE_DIR でローカルツリーを指すか、
# 未指定なら GitHub から FetchContent で取る（cmake/glyphware.cmake）
include(cmake/glyphware.cmake)

#------------------------------------------------------------------------------
# ライブラリ本体
#------------------------------------------------------------------------------
set(TYPESET_SOURCES
    src/geom.cpp
    src/font/font_set.cpp
    src/dl/display_list.cpp
    src/image/image.cpp
    src/obj/object.cpp
    src/obj/svg_import.cpp
    # text: Unicode / JLReq のテーブル
    src/text/utf.cpp
    src/text/char_class.cpp
    src/text/spacing_table.cpp
    src/text/orientation.cpp
    src/text/line_break.cpp
    # inl: インライン組版
    src/inl/shaper.cpp
    src/inl/annotation.cpp
    src/inl/item_builder.cpp
    src/inl/line_breaker.cpp
    src/inl/paragraph_layouter.cpp
    # page: ブロック／ページ
    src/page/page.cpp
    src/page/flow_layouter.cpp
    src/backend/raster.cpp
    src/backend/path_raster.cpp
    src/backend/sfnt_info.cpp
    src/backend/pdf_writer.cpp
    src/backend/svg_writer.cpp
)

add_library(typeset STATIC ${TYPESET_SOURCES})
add_library(typeset::typeset ALIAS typeset)

target_include_directories(typeset
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${STB_INCLUDE_DIRS}
)
target_link_libraries(typeset
    PUBLIC
        glyphware::glyphware
    PRIVATE
        Freetype::Freetype
        harfbuzz::harfbuzz
        harfbuzz::harfbuzz-subset
        ZLIB::ZLIB
        libunibreak::libunibreak
)

if(MSVC)
    target_compile_definitions(typeset PUBLIC NOMINMAX)
    target_compile_options(typeset PRIVATE /W3 /permissive-)
else()
    target_compile_options(typeset PRIVATE -Wall -Wextra)
endif()

#------------------------------------------------------------------------------
# サンプル
#------------------------------------------------------------------------------
if(TYPESET_BUILD_SAMPLES)
    add_executable(sample_dl samples/sample_dl.cpp)
    target_link_libraries(sample_dl PRIVATE typeset)

    add_executable(sample_inline samples/sample_inline.cpp)
    target_link_libraries(sample_inline PRIVATE typeset)

    add_executable(sample_script samples/sample_script.cpp)
    target_link_libraries(sample_script PRIVATE typeset)

    add_executable(sample_novel samples/sample_novel.cpp)
    target_link_libraries(sample_novel PRIVATE typeset)

    add_executable(sample_tech samples/sample_tech.cpp)
    target_link_libraries(sample_tech PRIVATE typeset)

    add_executable(sample_objects samples/sample_objects.cpp)
    target_link_libraries(sample_objects PRIVATE typeset)

    add_executable(sample_report samples/sample_report.cpp)
    target_link_libraries(sample_report PRIVATE typeset)
endif()

#------------------------------------------------------------------------------
# テスト
#------------------------------------------------------------------------------
if(TYPESET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#------------------------------------------------------------------------------
# Python バインド
#------------------------------------------------------------------------------
if(TYPESET_BUILD_PYTHON)
    add_subdirectory(python)
endif()
