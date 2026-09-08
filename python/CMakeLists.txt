cmake_minimum_required(VERSION 3.24)

# pybind11 は pip 経由（`pip install pybind11`）。Python は modern FindPython で探す
# （psdparse / clipparse と同じ構成）。
if(NOT TARGET pybind11::module)
    set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
    if(NOT Python3_EXECUTABLE)
        find_program(_ts_python NAMES python python3 REQUIRED)
        set(Python3_EXECUTABLE "${_ts_python}" CACHE FILEPATH "")
    endif()
    # 拡張モジュールのビルドに必要なのは Development.Module だけ
    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
    if(NOT pybind11_DIR)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
            OUTPUT_VARIABLE _pybind11_cmakedir
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _pybind11_rv)
        if(NOT _pybind11_rv EQUAL 0)
            message(FATAL_ERROR
                "Could not locate pybind11 via '${Python3_EXECUTABLE} -m pybind11 --cmakedir'. "
                "Install with: pip install pybind11")
        endif()
        set(pybind11_DIR "${_pybind11_cmakedir}" CACHE PATH "pybind11 cmake dir")
    endif()
    find_package(pybind11 CONFIG REQUIRED)
endif()

pybind11_add_module(typeset_py MODULE typeset_module.cpp)
set_target_properties(typeset_py PROPERTIES OUTPUT_NAME "typeset")
target_link_libraries(typeset_py PRIVATE typeset)

# scikit-build-core は install() されたものを wheel に詰める
install(TARGETS typeset_py
        LIBRARY DESTINATION "."
        RUNTIME DESTINATION ".")
