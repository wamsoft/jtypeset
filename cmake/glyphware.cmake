# glyphware の取り込み
#
#   開発中:  cmake -DGLYPHWARE_DIR=d:/work/kirikiri/glyphware ...
#            （ローカルツリーを add_subdirectory。glyphware 側の変更を即反映できる）
#   公開時:  GLYPHWARE_DIR を空にすると GitHub から FetchContent で取る
#
# 親プロジェクトが既に glyphware target を持っていればそれを使う。

set(GLYPHWARE_DIR "" CACHE PATH "Path to a local glyphware source tree (empty = FetchContent)")
set(GLYPHWARE_GIT_REPOSITORY "https://github.com/wamsoft/glyphware.git" CACHE STRING "")
set(GLYPHWARE_GIT_TAG "master" CACHE STRING "")

if(TARGET glyphware)
    message(STATUS "typeset: using existing glyphware target")
    if(NOT TARGET glyphware::glyphware)
        add_library(glyphware::glyphware ALIAS glyphware)
    endif()
    return()
endif()

set(GLYPHWARE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLYPHWARE_INSTALL OFF CACHE BOOL "" FORCE)

if(GLYPHWARE_DIR)
    if(NOT EXISTS "${GLYPHWARE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "GLYPHWARE_DIR does not point to a glyphware tree: ${GLYPHWARE_DIR}")
    endif()
    message(STATUS "typeset: glyphware from local tree ${GLYPHWARE_DIR}")
    add_subdirectory(${GLYPHWARE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glyphware)
else()
    message(STATUS "typeset: glyphware via FetchContent (${GLYPHWARE_GIT_REPOSITORY} @ ${GLYPHWARE_GIT_TAG})")
    include(FetchContent)
    FetchContent_Declare(glyphware
        GIT_REPOSITORY ${GLYPHWARE_GIT_REPOSITORY}
        GIT_TAG ${GLYPHWARE_GIT_TAG}
    )
    FetchContent_MakeAvailable(glyphware)
endif()
