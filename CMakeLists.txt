cmake_minimum_required(VERSION 3.20)

project(reconstructOneFrame
    VERSION 0.1.0
    LANGUAGES CXX CUDA
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

add_library(reconstructOneFrame SHARED
    src/calibration_model/CalibrationJsonReader.cpp
    src/calibration_model/CalibrationModel.cpp
    src/config/ReconsConfig.cpp
    src/dssi/DssiThreeScanCompat.cpp
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

target_include_directories(reconstructOneFrame
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${OpenCV_INCLUDE_DIRS}
)

target_compile_definitions(reconstructOneFrame
    PRIVATE
        RECONSTRUCT_ONE_FRAME_BUILDING_LIBRARY
)

target_link_libraries(reconstructOneFrame
    PRIVATE
        ${OpenCV_LIBS}
)

if(MSVC)
    target_compile_options(reconstructOneFrame
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
    )
else()
    target_compile_options(reconstructOneFrame
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra>
    )
endif()

set_source_files_properties(
    src/phase/PhaseUnwrapperCuda.cu
    src/phase/WrappedPhaseComputerCuda.cu
    src/reconstruction/PointCloudReconstructorCuda.cu
    PROPERTIES LANGUAGE CUDA
)
set_target_properties(reconstructOneFrame PROPERTIES
    CUDA_RUNTIME_LIBRARY Shared
    WINDOWS_EXPORT_ALL_SYMBOLS ON
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
        reconstructOneFrame
)

include(CTest)

if(BUILD_TESTING)
    function(rof_add_test name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${name} PRIVATE reconstructOneFrame)
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
endif()
