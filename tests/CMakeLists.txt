find_package(doctest CONFIG REQUIRED)

add_executable(typeset_tests
    main.cpp
    test_geom.cpp
    test_raster.cpp
    test_inline.cpp
    test_page.cpp
    test_flow3.cpp
    test_obj.cpp
)
target_link_libraries(typeset_tests PRIVATE typeset doctest::doctest)
target_include_directories(typeset_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME typeset_tests COMMAND typeset_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
