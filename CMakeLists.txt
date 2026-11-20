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
facepalm_unit(config)
facepalm_unit(expressions)
facepalm_unit(face_analyzer)
facepalm_unit(face_model)
facepalm_unit(frame)
facepalm_unit(frame_pipeline)
# ToRgba is pure computation, so the pipeline's tests run it for real.
target_link_libraries(frame_pipeline PUBLIC frame)
facepalm_unit(expression_reader)
facepalm_unit(gesture_reader)
facepalm_unit(head_pose)
facepalm_unit(head_pose_reader)
facepalm_unit(head_gesture_model)
# head_pose is pure computation, so the gesture model's tests run it for
# real (it needs RotationFrom, and reuses Rotation for its own test
# fixtures).
target_link_libraries(head_gesture_model PUBLIC head_pose)
facepalm_unit(reaction)
facepalm_unit(reaction_feed)
facepalm_unit(protocol)
facepalm_unit(service)
# The protocol is pure computation, so the service's tests run it for real.
target_link_libraries(service PUBLIC protocol)
# The POSIX socket calls alone; its tests run them on real sockets.
facepalm_unit(unix_socket)
# head_pose is pure computation, so the reaction's tests run it for real.
target_link_libraries(reaction PUBLIC head_pose)
# head_pose_reader's tests run the real reaction (pure computation);
# gesture_reader's run the real head-gesture model, and expression_reader's
# the real ExpressionTracker and head_pose (also pure computation, used to
# derive the pose it hands the tracker, #46), for the same reason.
# face_analyzer's tests mock both the face model and the readers.
target_link_libraries(head_pose_reader PUBLIC reaction)
target_link_libraries(gesture_reader PUBLIC head_gesture_model)
target_link_libraries(expression_reader PUBLIC expressions head_pose)
facepalm_unit(session_camera)

facepalm_unit(gesture_replay)
target_link_libraries(gesture_replay PUBLIC gesture_reader)
add_executable(gesture_replay_cli tools/gesture_replay/main.cc)
set_target_properties(gesture_replay_cli PROPERTIES OUTPUT_NAME gesture_replay)
target_link_libraries(gesture_replay_cli PRIVATE gesture_replay)

# The macOS capture API, through AVFoundation, and camera_probe, which drives
# it. They need a real camera, so camera_probe is their smoke test (#11);
# SessionCamera, the logic around them, is unit-tested.
if(APPLE)
  enable_language(OBJCXX)
  add_library(avfoundation_session src/macos/avfoundation_session.mm)
  target_compile_options(avfoundation_session PRIVATE -fobjc-arc)
  target_include_directories(avfoundation_session PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(avfoundation_session PUBLIC frame session_camera
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework Foundation")
  add_executable(camera_probe tools/camera_probe/camera_probe.cc)
  target_link_libraries(camera_probe PRIVATE avfoundation_session camera_controller
    config frame_pipeline face_analyzer expression_reader gesture_reader
    head_pose_reader
    "-framework CoreGraphics"
    "-framework ImageIO")
endif()

# face_model_check runs the real face model through MediaPipeFaceModel. It
# needs libfacepalm_mediapipe.dylib, which tools/fixtures/build.sh builds with
# Bazel, so it is built only when FACEPALM_MEDIAPIPE_DYLIB names that file; the
# Fixtures workflow does, and holds it to MediaPipe's expected results.
set(FACEPALM_MEDIAPIPE_DYLIB "" CACHE FILEPATH
  "libfacepalm_mediapipe.dylib, to build face_model_check against")
if(APPLE AND FACEPALM_MEDIAPIPE_DYLIB)
  add_library(facepalm_mediapipe SHARED IMPORTED)
  set_target_properties(facepalm_mediapipe PROPERTIES
    IMPORTED_LOCATION ${FACEPALM_MEDIAPIPE_DYLIB}
    IMPORTED_SONAME "@rpath/libfacepalm_mediapipe.dylib")
  add_executable(face_model_check tools/face_model_check/face_model_check.cc)
  target_link_libraries(face_model_check PRIVATE face_model head_gesture_model
    facepalm_mediapipe
    "-framework CoreFoundation" "-framework CoreGraphics" "-framework ImageIO")
  get_filename_component(FACEPALM_MEDIAPIPE_DIR ${FACEPALM_MEDIAPIPE_DYLIB}
    DIRECTORY)
  set_target_properties(face_model_check PROPERTIES
    BUILD_RPATH ${FACEPALM_MEDIAPIPE_DIR})
  # camera_probe also runs the face model on each frame, given a model (#18).
  target_compile_definitions(camera_probe PRIVATE FACEPALM_WITH_FACE_MODEL)
  target_link_libraries(camera_probe PRIVATE face_model facepalm_mediapipe)
  set_target_properties(camera_probe PROPERTIES
    BUILD_RPATH ${FACEPALM_MEDIAPIPE_DIR})
endif()

# Checks formatting against .clang-format without changing any file.
file(GLOB_RECURSE FACEPALM_CXX_FILES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.mm ${PROJECT_SOURCE_DIR}/test/*.cc
  ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.cc ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.h
  ${PROJECT_SOURCE_DIR}/tools/camera_probe/*.cc
  ${PROJECT_SOURCE_DIR}/tools/face_model_check/*.cc
  ${PROJECT_SOURCE_DIR}/tools/fixtures/*.cc)
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
