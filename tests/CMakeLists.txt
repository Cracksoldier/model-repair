include(Catch)

# Generate test meshes before building tests.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/meshes/valid_cube.stl
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/generate_test_meshes.py
                ${CMAKE_CURRENT_SOURCE_DIR}/meshes
        COMMENT "Generating test meshes"
        VERBATIM
    )
    add_custom_target(generate_test_meshes ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/meshes/valid_cube.stl)
endif()

add_executable(model-repair-tests
    test_mesh_io.cpp
    test_repairs.cpp
    test_pipeline.cpp
    test_remesh.cpp
)

target_link_libraries(model-repair-tests
    PRIVATE
        modelrepair
        Catch2::Catch2WithMain
)

target_compile_definitions(model-repair-tests PRIVATE
    TEST_MESH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/meshes"
)

if(Python3_FOUND)
    add_dependencies(model-repair-tests generate_test_meshes)
endif()

# PRE_TEST defers discovery to CTest run time, avoiding ASan startup ordering
# issues that occur when the binary is run during the build phase.
catch_discover_tests(model-repair-tests DISCOVERY_MODE PRE_TEST)
