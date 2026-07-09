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

add_library(reconstructOneFrame
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
set_target_properties(reconstructOneFrame PROPERTIES CUDA_RUNTIME_LIBRARY Shared)

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

    add_executable(calibration_model_test
        tests/calibration_model_test.cpp
    )
    target_include_directories(calibration_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(calibration_model_test PRIVATE reconstructOneFrame)
    add_test(NAME calibration_model_test COMMAND calibration_model_test)
    set_tests_properties(calibration_model_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(input_manifest_test
        tests/input_manifest_test.cpp
    )
    target_include_directories(input_manifest_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(input_manifest_test PRIVATE reconstructOneFrame)
    add_test(NAME input_manifest_test COMMAND input_manifest_test)
    set_tests_properties(input_manifest_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(image_validator_contract_test
        tests/image_validator_contract_test.cpp
    )
    target_include_directories(image_validator_contract_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(image_validator_contract_test PRIVATE reconstructOneFrame)
    add_test(NAME image_validator_contract_test COMMAND image_validator_contract_test)

    add_executable(image_preprocess_dry_run_test
        tests/image_preprocess_dry_run_test.cpp
    )
    target_include_directories(image_preprocess_dry_run_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(image_preprocess_dry_run_test PRIVATE reconstructOneFrame)
    add_test(NAME image_preprocess_dry_run_test COMMAND image_preprocess_dry_run_test)
    set_tests_properties(image_preprocess_dry_run_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(pipeline_phase2_smoke_test
        tests/pipeline_phase2_smoke_test.cpp
    )
    target_include_directories(pipeline_phase2_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pipeline_phase2_smoke_test PRIVATE reconstructOneFrame)
    add_test(NAME pipeline_phase2_smoke_test COMMAND pipeline_phase2_smoke_test)
    set_tests_properties(pipeline_phase2_smoke_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(wrapped_phase_computer_test
        tests/wrapped_phase_computer_test.cpp
    )
    target_include_directories(wrapped_phase_computer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(wrapped_phase_computer_test PRIVATE reconstructOneFrame)
    add_test(NAME wrapped_phase_computer_test COMMAND wrapped_phase_computer_test)
    set_tests_properties(wrapped_phase_computer_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(phase_unwrapper_test
        tests/phase_unwrapper_test.cpp
    )
    target_include_directories(phase_unwrapper_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(phase_unwrapper_test PRIVATE reconstructOneFrame)
    add_test(NAME phase_unwrapper_test COMMAND phase_unwrapper_test)
    set_tests_properties(phase_unwrapper_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(point_cloud_reconstructor_test
        tests/point_cloud_reconstructor_test.cpp
    )
    target_include_directories(point_cloud_reconstructor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(point_cloud_reconstructor_test PRIVATE reconstructOneFrame)
    add_test(NAME point_cloud_reconstructor_test COMMAND point_cloud_reconstructor_test)
    set_tests_properties(point_cloud_reconstructor_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(point_reliability_test
        tests/point_reliability_test.cpp
    )
    target_include_directories(point_reliability_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(point_reliability_test PRIVATE reconstructOneFrame)
    add_test(NAME point_reliability_test COMMAND point_reliability_test)
    set_tests_properties(point_reliability_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
