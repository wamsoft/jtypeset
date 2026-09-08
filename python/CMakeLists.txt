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

# 拡張モジュールは jtypeset/_jtypeset、純 Python の部分（typeset/__init__.py, typeset/md）は隣に置く。
# ビルドツリーでは <build>/python/<Config>/jtypeset/ にまとめるので、sys.path に <build>/python/<Config> を足せば import できる
pybind11_add_module(typeset_py MODULE typeset_module.cpp)
set_target_properties(typeset_py PROPERTIES
    OUTPUT_NAME "_jtypeset"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/jtypeset"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/jtypeset")
target_link_libraries(typeset_py PRIVATE typeset)
# 純 Python の部分は毎回コピーする（.py を直しただけでも反映されるように）
add_custom_target(typeset_py_sources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/jtypeset" "$<TARGET_FILE_DIR:typeset_py>"
    COMMENT "copy python package sources next to _jtypeset")
add_dependencies(typeset_py_sources typeset_py)

# scikit-build-core は install() されたものを wheel に詰める（純 Python 側は pyproject の wheel.packages）
install(TARGETS typeset_py
        LIBRARY DESTINATION jtypeset
        RUNTIME DESTINATION jtypeset)

# MicroTeX（LaTeX 数式）ハンドラ: 拡張にリンクし、数式フォント（res）を jtypeset/microtex_res に同梱する
if(TARGET typeset_microtex)
    target_link_libraries(typeset_py PRIVATE typeset_microtex)
    target_compile_definitions(typeset_py PRIVATE TYPESET_HAS_MICROTEX)
    add_custom_command(TARGET typeset_py_sources POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${TYPESET_MICROTEX_RES_DIR}" "$<TARGET_FILE_DIR:typeset_py>/microtex_res"
        COMMENT "copy MicroTeX res into the python package")
    install(DIRECTORY "${TYPESET_MICROTEX_RES_DIR}/" DESTINATION jtypeset/microtex_res)
endif()
