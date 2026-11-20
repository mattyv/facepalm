cmake_minimum_required(VERSION 3.28)
project(facepalm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FACEPALM_COVERAGE "Instrument for source-based coverage" OFF)
option(FACEPALM_TIDY "Run clang-tidy on every source as it compiles" OFF)
option(FACEPALM_LINUX_TRAY "Build the optional Linux Qt 6 tray app and enable Debian packaging" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(FACEPALM_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()
if(FACEPALM_TIDY)
  find_program(CLANG_TIDY NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY} --warnings-as-errors=*)
endif()

include(CTest)
include(GNUInstallDirs)
if(BUILD_TESTING)
  find_package(GTest REQUIRED)
  include(GoogleTest)
endif()

# Each source file gets a library and a test executable of its own, so that
# its coverage is measured from its own tests alone. A unit's tests write
# their profiles to profiles/<unit>-<pid>.profraw for scripts/coverage.sh.
function(facepalm_unit name)
  add_library(${name} src/${name}.cc)
  target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR})
  if(BUILD_TESTING)
    add_executable(${name}_test test/${name}_test.cc)
    target_link_libraries(${name}_test PRIVATE ${name} GTest::gmock GTest::gtest_main)
    gtest_discover_tests(${name}_test
      DISCOVERY_MODE PRE_TEST
      PROPERTIES ENVIRONMENT
        "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profiles/${name}-%p.profraw")
  endif()
endfunction()

facepalm_unit(approved_feed)
facepalm_unit(settings_document)
target_link_libraries(settings_document PUBLIC config)
facepalm_unit(client_token)
facepalm_unit(daemon_control)
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
facepalm_unit(rating_log)
target_link_libraries(rating_log PUBLIC protocol)
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

# A normal install retains the MCP source and lockfile for user-managed Node 22.
# Debian packages select Runtime only and do not install or fetch MCP dependencies.
install(TARGETS gesture_replay_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
install(FILES LICENSE THIRDPARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/facepalm COMPONENT Runtime)
install(FILES mcp/main.js mcp/server.js mcp/session.js mcp/hook-broker.js
  mcp/package.json mcp/package-lock.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/facepalm/mcp COMPONENT MCP)

if(FACEPALM_LINUX_TRAY)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "FACEPALM_LINUX_TRAY requires Linux")
  endif()
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
  add_executable(facepalm_menu tools/menu/main_linux.cc)
  set_target_properties(facepalm_menu PROPERTIES OUTPUT_NAME facepalm-menu)
  target_compile_definitions(facepalm_menu PRIVATE FACEPALM_VERSION="${PROJECT_VERSION}")
  target_link_libraries(facepalm_menu PRIVATE settings_document Qt6::Widgets Qt6::Network)
  install(TARGETS facepalm_menu RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  install(FILES tools/menu/linux/facepalm.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT Runtime)
  install(FILES tools/menu/linux/facepalm.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps COMPONENT Runtime)
  install(FILES tools/menu/linux/NOTO-EMOJI-LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/facepalm COMPONENT Runtime)
  install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT Runtime)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright COMPONENT Runtime)

  if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    find_program(DBUS_RUN_SESSION NAMES dbus-run-session REQUIRED)
    find_program(XVFB_RUN NAMES xvfb-run REQUIRED)
    find_program(XDOTOOL NAMES xdotool REQUIRED)
    # Use the same sibling share directory that the installed tray searches.
    set_target_properties(facepalm_menu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    configure_file(tools/menu/linux/facepalm.png
      "${CMAKE_CURRENT_BINARY_DIR}/share/icons/hicolor/128x128/apps/facepalm.png" COPYONLY)
    add_executable(linux_tray_integration test/linux_tray_integration.cc)
    set_target_properties(linux_tray_integration PROPERTIES AUTOMOC ON)
    add_dependencies(linux_tray_integration facepalm_menu)
    target_link_libraries(linux_tray_integration PRIVATE Qt6::Core Qt6::DBus Qt6::Network Qt6::Widgets)
    add_test(NAME linux_tray_integration COMMAND ${DBUS_RUN_SESSION} --
      ${XVFB_RUN} -a $<TARGET_FILE:linux_tray_integration> $<TARGET_FILE:facepalm_menu>)
    set_tests_properties(linux_tray_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb" TIMEOUT 40)
  endif()

  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEB_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_ALL Runtime)
  set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Facepalm contributors")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mattyv/facepalm")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Facepalm tray status and offline gesture replay")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, qt6-wayland")
  include(CPack)
endif()

# The macOS capture API, through AVFoundation, and camera_probe, which drives
# it. They need a real camera, so camera_probe is their smoke test (#11);
# SessionCamera, the logic around them, is unit-tested.
if(APPLE)
  enable_language(OBJCXX)
  add_executable(facepalm_menu tools/menu/main.mm)
  set_target_properties(facepalm_menu PROPERTIES OUTPUT_NAME facepalm-menu)
  target_compile_options(facepalm_menu PRIVATE -fobjc-arc)
  target_link_libraries(facepalm_menu PRIVATE settings_document "-framework AppKit")
  if(BUILD_TESTING)
    add_executable(menu_store_checks test/menu_store_check.mm)
    target_compile_options(menu_store_checks PRIVATE -fobjc-arc)
    target_link_libraries(menu_store_checks PRIVATE settings_document GTest::gtest_main "-framework AppKit")
    add_test(NAME menu_store_checks COMMAND menu_store_checks)
  endif()

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
  add_executable(facepalm_daemon tools/daemon/main.cc)
  set_target_properties(facepalm_daemon PROPERTIES OUTPUT_NAME facepalm-daemon
    INSTALL_RPATH "@executable_path/../Frameworks")
  target_link_libraries(facepalm_daemon PRIVATE approved_feed client_token daemon_control
    avfoundation_session camera_controller config frame_pipeline face_analyzer
    expression_reader gesture_reader reaction_feed rating_log service unix_socket face_model
    facepalm_mediapipe "-framework CoreFoundation")
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

# The Linux daemon uses the pinned native MediaPipe Tasks C library.
set(FACEPALM_MEDIAPIPE_C_INCLUDE "" CACHE PATH "Root of MediaPipe's native C headers")
set(FACEPALM_MEDIAPIPE_C_LIBRARY "" CACHE FILEPATH "Linux libmediapipe.so")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
    (FACEPALM_MEDIAPIPE_C_INCLUDE OR FACEPALM_MEDIAPIPE_C_LIBRARY))
  if(NOT EXISTS "${FACEPALM_MEDIAPIPE_C_INCLUDE}/mediapipe/tasks/c/vision/face_landmarker/face_landmarker.h" OR
      NOT EXISTS "${FACEPALM_MEDIAPIPE_C_LIBRARY}")
    message(FATAL_ERROR "Linux camera requires both the MediaPipe C headers and libmediapipe.so")
  endif()
  find_package(Threads REQUIRED)
  find_path(V4L2_INCLUDE_DIRECTORY NAMES libv4l2.h REQUIRED)
  find_library(V4L2_LIBRARY NAMES v4l2 libv4l2.so.0 REQUIRED)
  add_executable(facepalm_daemon tools/daemon/main.cc
    src/linux/v4l2_session.cc src/linux/mediapipe_c_shim.cc)
  set_target_properties(facepalm_daemon PROPERTIES OUTPUT_NAME facepalm-daemon
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/facepalm")
  target_include_directories(facepalm_daemon SYSTEM PRIVATE
    ${FACEPALM_MEDIAPIPE_C_INCLUDE} ${V4L2_INCLUDE_DIRECTORY})
  target_link_libraries(facepalm_daemon PRIVATE approved_feed client_token daemon_control
    camera_controller config frame_pipeline face_analyzer expression_reader gesture_reader
    reaction_feed rating_log service session_camera unix_socket face_model Threads::Threads
    ${FACEPALM_MEDIAPIPE_C_LIBRARY} ${V4L2_LIBRARY})
  install(TARGETS facepalm_daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT LinuxCamera)
  install(FILES "${FACEPALM_MEDIAPIPE_C_LIBRARY}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/facepalm COMPONENT LinuxCamera)
endif()

# Checks formatting against .clang-format without changing any file.
file(GLOB_RECURSE FACEPALM_CXX_FILES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.mm ${PROJECT_SOURCE_DIR}/test/*.cc
  ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.cc ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.h
  ${PROJECT_SOURCE_DIR}/tools/menu/*.cc ${PROJECT_SOURCE_DIR}/tools/menu/*.mm
  ${PROJECT_SOURCE_DIR}/tools/daemon/*.cc
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

# Test-only service for the cross-process MCP integration test.
if(BUILD_TESTING)
  add_executable(mcp_service_fixture EXCLUDE_FROM_ALL
    test/fixtures/mcp_service_fixture.cc)
  target_link_libraries(mcp_service_fixture PRIVATE service reaction_feed rating_log unix_socket)
endif()
