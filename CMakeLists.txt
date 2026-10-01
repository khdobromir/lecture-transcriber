cmake_minimum_required(VERSION 3.20)
project(transcribe VERSION 0.1.0 DESCRIPTION "Транскрипция русской речи на CPU" LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Эта версия предназначена для Linux (Omarchy/Arch)")
endif()
add_executable(transcribe src/main.cpp)
target_compile_features(transcribe PRIVATE cxx_std_23)
set_target_properties(transcribe PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(transcribe PRIVATE TRANSCRIBE_VERSION="${PROJECT_VERSION}")
target_compile_options(transcribe PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS transcribe RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    find_program(FFMPEG_EXECUTABLE ffmpeg REQUIRED)
    add_test(NAME pipeline COMMAND "${CMAKE_COMMAND}" -E env
        "TRANSCRIBE_TEST_BINARY=$<TARGET_FILE:transcribe>"
        "TRANSCRIBE_TEST_VERSION=${PROJECT_VERSION}"
        "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_pipeline.py")
    add_test(NAME installation COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/test_installation.py")
    add_test(NAME smoke_preflight COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/test_smoke_real.py")
    set_tests_properties(pipeline installation smoke_preflight PROPERTIES TIMEOUT 120)
endif()
