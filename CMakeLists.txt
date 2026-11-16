cmake_minimum_required(VERSION 3.28)
project(facepalm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FACEPALM_COVERAGE "Instrument for source-based coverage" OFF)
option(FACEPALM_TIDY "Run clang-tidy on every source as it compiles" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(FACEPALM_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()
if(FACEPALM_TIDY)
  find_program(CLANG_TIDY NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY} --warnings-as-errors=*)
endif()

find_package(GTest REQUIRED)
enable_testing()
include(GoogleTest)

# Each source file gets a library and a test executable of its own, so that
# its coverage is measured from its own tests alone. A unit's tests write
# their profiles to profiles/<unit>-<pid>.profraw for scripts/coverage.sh.
function(facepalm_unit name)
  add_library(${name} src/${name}.cc)
  target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR})
  add_executable(${name}_test test/${name}_test.cc)
  target_link_libraries(${name}_test PRIVATE ${name} GTest::gmock GTest::gtest_main)
  gtest_discover_tests(${name}_test
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT
      "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profiles/${name}-%p.profraw")
endfunction()

facepalm_unit(camera_controller)
facepalm_unit(head_pose)

# The macOS camera, through AVFoundation, and camera_probe, which drives it.
# They need a real camera, so camera_probe is their smoke test (#11).
if(APPLE)
  enable_language(OBJCXX)
  add_library(avfoundation_camera src/macos/avfoundation_camera.mm)
  # dispatch_queue_t must be the C type the C++ header declares, not an
  # Objective-C object, or the two sides mangle the constructor differently.
  target_compile_options(avfoundation_camera PRIVATE -fobjc-arc)
  target_compile_definitions(avfoundation_camera PRIVATE OS_OBJECT_USE_OBJC=0)
  target_include_directories(avfoundation_camera PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(avfoundation_camera PUBLIC camera_controller
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework Foundation")
  add_executable(camera_probe tools/camera_probe/camera_probe.cc)
  target_link_libraries(camera_probe PRIVATE avfoundation_camera)
endif()

# Checks formatting against .clang-format without changing any file.
file(GLOB_RECURSE FACEPALM_CXX_FILES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.mm ${PROJECT_SOURCE_DIR}/test/*.cc
  ${PROJECT_SOURCE_DIR}/tools/camera_probe/*.cc)
find_program(CLANG_FORMAT NAMES clang-format)
if(CLANG_FORMAT)
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${FACEPALM_CXX_FILES}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
else()
  # Fails rather than vanishing, so a run without clang-format cannot pass.
  add_custom_target(format-check
    COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found at configure time"
    COMMAND ${CMAKE_COMMAND} -E false)
endif()
