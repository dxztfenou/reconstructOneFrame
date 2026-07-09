cmake_minimum_required(VERSION 3.20)

project(reconstructOneFrame
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(reconstructOneFrame
    src/calibration_model/CalibrationModel.cpp
    src/config/ReconsConfig.cpp
    src/diagnostics/Status.cpp
    src/image/ImageValidator.cpp
    src/io/JsonFile.cpp
    src/logging/LogSession.cpp
    src/pipeline/ReconstructEngine.cpp
    src/pipeline/SingleFramePipeline.cpp
)

target_include_directories(reconstructOneFrame
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(reconstructOneFrame
    PUBLIC
        RECONSTRUCT_ONE_FRAME_STATIC
    PRIVATE
        RECONSTRUCT_ONE_FRAME_BUILDING_LIBRARY
)

if(MSVC)
    target_compile_options(reconstructOneFrame PRIVATE /W4 /permissive-)
else()
    target_compile_options(reconstructOneFrame PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(reconstructSample
    src/sample/reconstructSample.cpp
)

target_include_directories(reconstructSample
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(reconstructSample
    PRIVATE
        reconstructOneFrame
)

include(CTest)

if(BUILD_TESTING)
    add_executable(config_config_load_test
        tests/config_config_load_test.cpp
    )
    target_include_directories(config_config_load_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(config_config_load_test PRIVATE reconstructOneFrame)
    add_test(NAME config_config_load_test COMMAND config_config_load_test)
    set_tests_properties(config_config_load_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(image_validator_test
        tests/image_validator_test.cpp
    )
    target_include_directories(image_validator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(image_validator_test PRIVATE reconstructOneFrame)
    add_test(NAME image_validator_test COMMAND image_validator_test)

    add_executable(pipeline_smoke_test
        tests/pipeline_smoke_test.cpp
    )
    target_include_directories(pipeline_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pipeline_smoke_test PRIVATE reconstructOneFrame)
    add_test(NAME pipeline_smoke_test COMMAND pipeline_smoke_test)
    set_tests_properties(pipeline_smoke_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
