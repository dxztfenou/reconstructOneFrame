cmake_minimum_required(VERSION 3.20)

project(reconstructOneFrame
    VERSION 0.1.0
    LANGUAGES C CXX CUDA
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 75)
set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)

set(THIRDPARTY "D:/code/ThirdPartyLibraries" CACHE PATH "Third-party dependency root")
if(WIN32 AND NOT OpenCV_DIR AND EXISTS "${THIRDPARTY}/opencv453/x64/vc15/lib/OpenCVConfig.cmake")
    set(OpenCV_DIR "${THIRDPARTY}/opencv453/x64/vc15/lib")
endif()
find_package(OpenCV 4.5.3 REQUIRED)
find_package(CUDAToolkit REQUIRED)

set(ROF_CORE_SOURCES
    src/calibration_model/CalibrationJsonReader.cpp
    src/calibration_model/CalibrationModel.cpp
    src/config/ReconsConfig.cpp
    src/diagnostics/DiagnosticSummary.cpp
    src/diagnostics/Status.cpp
    src/image/ImagePreprocessor.cpp
    src/image/InputManifest.cpp
    src/image/ImageValidator.cpp
    src/io/JsonFile.cpp
    src/logging/LogSession.cpp
    src/io/PlyIO.cpp
    src/phase/PhaseUnwrapperCuda.cu
    src/phase/WrappedPhaseComputer.cpp
    src/phase/WrappedPhaseComputerCuda.cu
    src/pipeline/ReconstructEngine.cpp
    src/pipeline/SingleFramePipeline.cpp
    src/quality/PointReliability.cpp
    src/reconstruction/PointCloudReconstructorCuda.cu
)

add_library(rofCore STATIC ${ROF_CORE_SOURCES})

target_include_directories(rofCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(rofCore
    PUBLIC
        RECONSTRUCT_ONE_FRAME_STATIC
)

target_link_libraries(rofCore
    PRIVATE
        ${OpenCV_LIBS}
)

add_library(reconstructOneFrame SHARED
    src/plugin/RofCApi.cpp
)

target_include_directories(reconstructOneFrame
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(reconstructOneFrame
    PRIVATE
        ROF_C_API_BUILDING_LIBRARY
)

target_link_libraries(reconstructOneFrame
    PRIVATE
        rofCore
        ${OpenCV_LIBS}
)

if(MSVC)
    target_compile_options(rofCore
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
    )
    target_compile_options(reconstructOneFrame PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(rofCore
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra>
    )
    target_compile_options(reconstructOneFrame PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_source_files_properties(
    src/phase/PhaseUnwrapperCuda.cu
    src/phase/WrappedPhaseComputerCuda.cu
    src/reconstruction/PointCloudReconstructorCuda.cu
    PROPERTIES LANGUAGE CUDA
)
set_target_properties(rofCore reconstructOneFrame PROPERTIES
    CUDA_RUNTIME_LIBRARY Shared
)

if(WIN32 AND EXISTS "${THIRDPARTY}/opencv453/x64/vc15/bin")
    add_custom_command(TARGET reconstructOneFrame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<$<CONFIG:Debug>:${THIRDPARTY}/opencv453/x64/vc15/bin/opencv_world453d.dll>$<$<NOT:$<CONFIG:Debug>>:${THIRDPARTY}/opencv453/x64/vc15/bin/opencv_world453.dll>"
            "$<TARGET_FILE_DIR:reconstructOneFrame>"
    )
endif()

if(WIN32 AND CUDAToolkit_BIN_DIR AND EXISTS "${CUDAToolkit_BIN_DIR}/cudart64_12.dll")
    add_custom_command(TARGET reconstructOneFrame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CUDAToolkit_BIN_DIR}/cudart64_12.dll"
            "$<TARGET_FILE_DIR:reconstructOneFrame>"
    )
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
        rofCore
)

include(CTest)

if(BUILD_TESTING)
    add_executable(rof_c_header_compile_test tests/rof_c_header_compile_test.c)
    target_include_directories(rof_c_header_compile_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rof_c_header_compile_test COMMAND rof_c_header_compile_test)
    set_tests_properties(rof_c_header_compile_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    function(rof_add_test name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${name} PRIVATE rofCore)
        add_test(NAME ${name} COMMAND ${name})
        set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endfunction()

    rof_add_test(config_config_load_test tests/config_config_load_test.cpp)
    rof_add_test(image_validator_test tests/image_validator_test.cpp)
    rof_add_test(pipeline_smoke_test tests/pipeline_smoke_test.cpp)
    rof_add_test(calibration_model_test tests/calibration_model_test.cpp)
    rof_add_test(input_manifest_test tests/input_manifest_test.cpp)
    rof_add_test(image_validator_contract_test tests/image_validator_contract_test.cpp)
    rof_add_test(wrapped_phase_computer_test tests/wrapped_phase_computer_test.cpp)
    rof_add_test(phase_unwrapper_test tests/phase_unwrapper_test.cpp)
    rof_add_test(point_cloud_reconstructor_test tests/point_cloud_reconstructor_test.cpp)
    rof_add_test(point_reliability_test tests/point_reliability_test.cpp)
    rof_add_test(engine_descriptor_test tests/engine_descriptor_test.cpp)

    add_executable(rof_plugin_contract_test tests/rof_plugin_contract_test.cpp)
    target_link_libraries(rof_plugin_contract_test PRIVATE reconstructOneFrame)
    add_test(NAME rof_plugin_contract_test COMMAND rof_plugin_contract_test)
    set_tests_properties(rof_plugin_contract_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(rof_abi_layout_test tests/rof_abi_layout_test.cpp)
    target_include_directories(rof_abi_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rof_abi_layout_test COMMAND rof_abi_layout_test)
    set_tests_properties(rof_abi_layout_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    if(WIN32)
        add_executable(rof_dll_exports_test tests/rof_dll_exports_test.cpp)
        target_include_directories(rof_dll_exports_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        add_test(NAME rof_dll_exports_test
            COMMAND rof_dll_exports_test
                $<TARGET_FILE:reconstructOneFrame>
                "${CMAKE_CURRENT_SOURCE_DIR}/config/reconsAlgPara.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/phase2_valid_calibResult.json")
        set_tests_properties(rof_dll_exports_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()
