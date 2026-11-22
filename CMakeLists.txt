cmake_minimum_required(VERSION 3.28)
project(facepalm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FACEPALM_COVERAGE "Instrument for source-based coverage" OFF)
option(FACEPALM_TIDY "Run clang-tidy on every source as it compiles" OFF)
option(FACEPALM_LINUX_TRAY "Build the optional Linux Qt 6 tray app and enable Debian packaging" OFF)
# Fuzzes the parsers that read untrusted input and property-tests the pure
# rules (#120), with Google FuzzTest, fetched only when this is on so the
# default build and the jobs that do not set it are unaffected. Replayed
# briefly, as ordinary GoogleTest tests, unless FUZZTEST_FUZZING_MODE is
# also on, which asks FuzzTest to instrument for real coverage-guided
# fuzzing (CI's `fuzzing` job, Linux and clang only; see docs/decisions.md).
option(FACEPALM_FUZZTEST "Fetch Google FuzzTest and build its fuzz and property tests" OFF)

# Moved ahead of FACEPALM_FUZZTEST below, which needs BUILD_TESTING (set
# here) and GTest::gtest already a target before it fetches anything
# (#120): FuzzTest's own CMakeLists.txt fetches and builds its own copy of
# GoogleTest when GTest::gtest does not exist yet, and find_package(GTest)
# later leaves that copy in place rather than importing Homebrew's or
# apt's, silently swapping every other test executable in this build onto
# a GoogleTest it built itself, with Abseil support that one std::visit
# call in its own printers triggers a deprecated-declaration warning on.
include(CTest)
if(BUILD_TESTING)
  find_package(GTest REQUIRED)
  include(GoogleTest)
endif()

# Fetched, and built, before this project's own -Werror exists, just below:
# FetchContent_MakeAvailable's add_subdirectory would otherwise pass it down
# to Abseil and RE2, FuzzTest's own dependencies, whose headers use Clang
# nullability extensions -Wpedantic rejects (#120; docs/decisions.md). This
# is also FuzzTest's own documented order: add_subdirectory first, so it and
# its dependencies build without the instrumentation flags below, then
# fuzztest_setup_fuzzing_flags, so facepalm's own units, defined later in
# this file, pick those up along with -Werror.
if(FACEPALM_FUZZTEST)
  include(FetchContent)
  # ANTLR's own C++ runtime, an unused transitive dependency of FuzzTest's
  # grammar-based domains (which this project does not use), otherwise
  # builds and registers its own ~60 ctest cases by default, and its
  # runtime libraries land in dist/ at the repository root rather than the
  # build directory.
  set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
  # Pinned past the 2026-06-29 release, to the commit that fixes a
  # self-instrumentation infinite recursion the release still has: under
  # FUZZTEST_FUZZING_MODE's -fsanitize-coverage=trace-cmp,
  # ExecutionCoverage::IsTracing's std::atomic<bool>::load (added for a
  # 2026-01-29 ThreadSanitizer fix) recurses into itself through
  # libstdc++'s own instrumented comparison on the memory-order argument,
  # overflowing the stack on the very first fuzzed input on Linux/GCC's
  # libstdc++ (found running this PR's own `fuzzing` CI job: every target
  # crashed the same way, GoogleTest's brief replay mode never hits this
  # path, so it was not seen locally). 39e5d587af63's own fix drops
  # std::atomic for the raw __atomic_load_n/__atomic_store_n builtins,
  # which compile to no instrumentable branch. No tagged release carries
  # it yet. With FetchContent's own checksum verification of the archive
  # (a GIT_REPOSITORY/GIT_TAG fetch has none): the commit a GIT_TAG names
  # can be force-pushed over, but this archive's bytes cannot change
  # without failing URL_HASH.
  FetchContent_Declare(
    fuzztest
    URL https://github.com/google/fuzztest/archive/39e5d587af63ba6209e0958a3141460ce003409c.tar.gz
    URL_HASH SHA256=32dd48a81d5d318110f9192ec52cdf191ebd403764aec64f62ba04b980297421
    # FuzzTest's own CMakeLists.txt hard-codes `set(CMAKE_CXX_STANDARD 17)`,
    # a plain (non-cache) variable that overrides this project's 20 for it
    # and the Abseil and RE2 it fetches, however this project's own
    # CMAKE_CXX_STANDARD is set: a normal variable set in a directory
    # always wins over a cache entry of the same name there, including one
    # forced from the parent. absl::Status's SourceLocation-sensitive
    # symbols then differ from what a C++20 fuzz test target calling into
    # them expects, and fail to link. Patched to 20 so the whole fetched
    # tree matches facepalm's own standard; sed's own exit status does not
    # depend on finding a match, so a future release that already builds
    # at 20 patches harmlessly.
    PATCH_COMMAND sed -i.bak "s/set(CMAKE_CXX_STANDARD 17)/set(CMAKE_CXX_STANDARD 20)/"
                  CMakeLists.txt && rm -f CMakeLists.txt.bak)
  FetchContent_MakeAvailable(fuzztest)
  # A no-op unless the top-level configure also passed
  # -DFUZZTEST_FUZZING_MODE=on (FuzzTest's own option, CI's `fuzzing` job):
  # it then instruments every target configured from here on for real
  # coverage-guided fuzzing instead of the brief GoogleTest replay every
  # other job gets.
  fuzztest_setup_fuzzing_flags()
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(FACEPALM_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()
if(FACEPALM_TIDY)
  find_program(CLANG_TIDY NAMES clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY} --warnings-as-errors=*)
endif()

include(GNUInstallDirs)

# Each source file gets a library and a test executable of its own, so that
# its coverage is measured from its own tests alone. A unit's tests write
# their profiles to profiles/<unit>-<pid>.profraw for scripts/coverage.sh.
# A unit in a folder of src, such as linux, names it with DIRECTORY; its test
# is in the same folder of test. TARGET names the library itself something
# other than `name` when a fetched dependency's own CMakeLists already
# claims `name` as a raw (non-namespaced) target: FACEPALM_FUZZTEST's
# Abseil does, for "config" (#120). The executable stays `<name>_test`
# either way, so scripts/coverage.sh's own naming is unaffected.
function(facepalm_unit name)
  cmake_parse_arguments(PARSE_ARGV 1 unit "" "DIRECTORY;TARGET" "")
  set(path ${name})
  if(unit_DIRECTORY)
    set(path ${unit_DIRECTORY}/${name})
  endif()
  set(library ${name})
  if(unit_TARGET)
    set(library ${unit_TARGET})
  endif()
  add_library(${library} src/${path}.cc)
  target_include_directories(${library} PUBLIC ${PROJECT_SOURCE_DIR})
  if(BUILD_TESTING)
    add_executable(${name}_test test/${path}_test.cc)
    target_link_libraries(${name}_test PRIVATE ${library} GTest::gmock GTest::gtest_main)
    gtest_discover_tests(${name}_test
      DISCOVERY_MODE PRE_TEST
      PROPERTIES ENVIRONMENT
        "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profiles/${name}-%p.profraw")
  endif()
endfunction()

facepalm_unit(approved_feed)
facepalm_unit(settings_document)
target_link_libraries(settings_document PUBLIC facepalm_config)
facepalm_unit(client_token)
facepalm_unit(client_approvals)
facepalm_unit(daemon_control)
facepalm_unit(preview_lease)
facepalm_unit(preview_publisher)
target_link_libraries(preview_publisher PUBLIC preview_lease frame)
facepalm_unit(preview_request)
facepalm_unit(daemon_status)
facepalm_unit(daemon_options)
facepalm_unit(feed_clients)
facepalm_unit(user_paths)
facepalm_unit(user_service)
facepalm_unit(single_instance)
facepalm_unit(camera_controller)
# The tab-separated text facepalm-daemon's `cameras` command prints (item
# 10 of #61, #13), extracted from tools/menu/main.mm and
# tools/menu/main_linux.cc, which had no pure entry point of their own to
# fuzz (#120): both now call this instead of parsing it twice by hand.
facepalm_unit(cameras_listing)
facepalm_unit(config TARGET facepalm_config)
facepalm_unit(expressions)
facepalm_unit(face_analyzer)
facepalm_unit(face_model)
facepalm_unit(feelings)
facepalm_unit(preview_readings)
target_link_libraries(preview_readings PUBLIC feelings)
facepalm_unit(tuning)
# The feelings are pure computation, so the tuning's tests run them for real.
target_link_libraries(tuning PUBLIC feelings)
# The tuning is pure computation, so the store's tests run it for real; its
# POSIX calls are its own, and its tests run them on a real folder too.
facepalm_unit(feeling_store)
target_link_libraries(feeling_store PUBLIC tuning)
facepalm_unit(frame)
facepalm_unit(frame_pipeline)
facepalm_unit(frame_recorder)
facepalm_unit(frame_times)
# ToRgba is pure computation, so the pipeline's tests run it for real.
target_link_libraries(frame_pipeline PUBLIC frame)
facepalm_unit(expression_reader)
facepalm_unit(gesture_reader)
facepalm_unit(look_away)
facepalm_unit(look_away_reader)
facepalm_unit(screen_context)
# Pure computation (SideOf, CoarseGazeFrom), like look_away: head_pose for
# the rotation type and ReactionFrom's sign convention (#61 item 11).
target_link_libraries(screen_context PUBLIC head_pose)
facepalm_unit(screen_context_reporter)
# The gaze rule, ComposeGaze and protocol::Quoted are pure computation, so
# the reporter's tests run them for real; only ScreenSource, the OS seam,
# is mocked.
target_link_libraries(screen_context_reporter PUBLIC screen_context protocol calibration eye_gaze)
facepalm_unit(screen_context_reader)
# ScreenContextReader's tests run the real ExpressionTracker, EyeGazeTracker
# and head_pose (also pure computation), as expression_reader's and
# look_away_reader's do, for the same reason.
target_link_libraries(screen_context_reader PUBLIC expressions head_pose eye_gaze)
facepalm_unit(head_pose)
# The eye-in-head signal, its composition with head pose into a coarse
# gaze angle, and the gains step's own estimate (#137): pure computation,
# like head_pose and screen_context, over FpFace's blendshapes and
# head_pose's own HeadRotation.
facepalm_unit(eye_gaze)
target_link_libraries(eye_gaze PUBLIC head_pose face_model)
facepalm_unit(head_pose_reader)
facepalm_unit(head_gesture_model)
# Formats camera_probe's `eyes` line (#139): pure computation over FpFace's
# blendshapes and landmarks, so camera_probe.cc stays thin glue around it.
facepalm_unit(eyes_probe)
# Parses that line back, so a later issue's eye reader (#140 through #145)
# can test against test/fixtures/eyes's recordings, the way gesture_replay
# and look_away_replay do for their own lines.
facepalm_unit(eyes_replay)
# head_pose is pure computation, so the gesture model's tests run it for
# real (it needs RotationFrom, and reuses Rotation for its own test
# fixtures).
target_link_libraries(head_gesture_model PUBLIC head_pose)
facepalm_unit(reaction)
facepalm_unit(reaction_feed)
# The feelings and their rule are pure computation, so the feed's tests run
# them for real.
target_link_libraries(reaction_feed PUBLIC feelings)
facepalm_unit(protocol)
target_link_libraries(protocol PUBLIC feelings calibration)
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
# The look-away reader runs the real detector and tracker, pure computation
# like the expression reader's (#35).
target_link_libraries(look_away PUBLIC head_pose)
target_link_libraries(look_away_reader PUBLIC look_away expressions head_pose)
facepalm_unit(calibration)
target_link_libraries(calibration PUBLIC head_pose eye_gaze)
facepalm_unit(calibration_store)
target_link_libraries(calibration_store PUBLIC calibration)
facepalm_unit(calibration_reader)
target_link_libraries(calibration_reader PUBLIC calibration head_pose eye_gaze)
# The one place that decides calibration is unavailable, rather than a
# nullptr check repeated in tools/daemon/main.cc (#116, #64).
facepalm_unit(daemon_calibration)
target_link_libraries(daemon_calibration PUBLIC calibration)
facepalm_unit(calibrate_cli)
# A menu's own wizard, driving one session over CalibrateClientIo (#118,
# #64): the one tested place that decides what to send and how to read the
# daemon's reply, so tools/menu/main.mm and main_linux.cc stay thin glue.
facepalm_unit(calibration_flow)
target_link_libraries(calibration_flow PUBLIC calibration)
facepalm_unit(session_camera)
# The daemon's readers are pure computation, so their factory's tests make
# them for real. screen_context_reader (#61 item 11) is the one exception:
# its tests pass it a real ScreenContextReader by reference, as
# ScreenContextReaderForwarder (face_readers.cc) always wraps one given.
facepalm_unit(face_readers)
target_link_libraries(face_readers PUBLIC gesture_reader expression_reader
  look_away_reader screen_context_reader calibration_reader facepalm_config)

facepalm_unit(gesture_replay)
target_link_libraries(gesture_replay PUBLIC gesture_reader)
# The look-away replay runs the real tracker and detector (pure computation)
# over recorded head poses in test/fixtures (#35).
facepalm_unit(look_away_replay)
target_link_libraries(look_away_replay PUBLIC look_away expressions facepalm_config)
if(BUILD_TESTING)
  target_compile_definitions(look_away_replay_test PRIVATE
    FACEPALM_FIXTURES="${PROJECT_SOURCE_DIR}/test/fixtures")
endif()
add_executable(gesture_replay_cli tools/gesture_replay/main.cc)
set_target_properties(gesture_replay_cli PROPERTIES OUTPUT_NAME gesture_replay)
target_link_libraries(gesture_replay_cli PRIVATE gesture_replay look_away_replay)

# A minimal way to drive a calibration session for testing (#36, #35, item
# 12 of #61), and a model for the eventual menu entry (item 8).
add_executable(calibrate_cli_tool tools/calibrate_cli/main.cc)
set_target_properties(calibrate_cli_tool PROPERTIES OUTPUT_NAME calibrate_cli)
target_link_libraries(calibrate_cli_tool PRIVATE calibrate_cli client_token user_paths)

# A normal install retains the MCP source and lockfile for user-managed Node 22.
# Debian packages select Runtime only and do not install or fetch MCP dependencies.
install(TARGETS gesture_replay_cli calibrate_cli_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
install(FILES LICENSE THIRDPARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/facepalm COMPONENT Runtime)
install(FILES mcp/main.js mcp/server.js mcp/session.js mcp/hook-broker.js
  mcp/settings-launcher.js mcp/feeling-widget.js mcp/feeling-widget.html
  mcp/feeling-elicitation.js mcp/feeling-result.js mcp/package.json
  mcp/package-lock.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/facepalm/mcp COMPONENT MCP)

# Fuzz and property tests (#120): a GoogleTest executable per unit fuzzed
# or property-tested, named "<name>_fuzz", not "<name>_test", so
# scripts/coverage.sh's own "*_test" glob, which measures each unit's
# coverage floor from its own tests alone, skips these the way it already
# skips menu_store_checks, preview_jpeg_checks and linux_tray_integration:
# they hold a property over many generated inputs, not a floor of lines a
# handful of examples exercise (CLAUDE.md's Tests section, docs/decisions.md).
if(FACEPALM_FUZZTEST)
  if(NOT BUILD_TESTING)
    message(FATAL_ERROR "FACEPALM_FUZZTEST requires BUILD_TESTING")
  endif()

  # One GoogleTest/FuzzTest executable, test/fuzz/<name>_fuzz_test.cc,
  # fuzzing or property-testing the unit(s) named in LIBS. Seeds, when
  # `name` has any under test/fuzz/corpus/, reach the test as
  # FACEPALM_FUZZ_CORPUS, following look_away_replay_test's own
  # FACEPALM_FIXTURES convention.
  function(facepalm_fuzz_test name)
    cmake_parse_arguments(PARSE_ARGV 1 fuzz "" "" "LIBS")
    add_executable(${name}_fuzz test/fuzz/${name}_fuzz_test.cc)
    # Not GTest::gtest_main: link_fuzztest below supplies
    # fuzztest::fuzztest_gtest_main, whose own main calls
    # fuzztest::InitFuzzTest before GoogleTest's RUN_ALL_TESTS, which is
    # what registers a FUZZ_TEST as a test GoogleTest can see at all.
    # Linking GTest::gtest_main's plain main as well left it unresolved,
    # the executable quietly ran zero tests.
    target_link_libraries(${name}_fuzz PRIVATE ${fuzz_LIBS})
    target_compile_definitions(${name}_fuzz PRIVATE
      FACEPALM_FUZZ_CORPUS="${PROJECT_SOURCE_DIR}/test/fuzz/corpus/${name}")
    # Including fuzztest/fuzztest.h pulls in Abseil's headers into this
    # translation unit, and several use Clang extensions (nullability
    # attributes, enable_if, diagnose_if) -Wpedantic and -Wextra reject
    # here regardless of whose code declares them (#120); -Werror alone is
    # turned back off, so a warning from the handful of lines this file
    # itself adds would still show, not be silently dropped.
    target_compile_options(${name}_fuzz PRIVATE -Wno-error)
    link_fuzztest(${name}_fuzz)
    gtest_discover_tests(${name}_fuzz DISCOVERY_MODE PRE_TEST)
  endfunction()

  # In the order #120 lists the fuzz targets, then the property tests.
  facepalm_fuzz_test(protocol LIBS protocol)
  facepalm_fuzz_test(config LIBS facepalm_config)
  facepalm_fuzz_test(settings_document LIBS settings_document)
  facepalm_fuzz_test(tuning LIBS tuning feelings)
  facepalm_fuzz_test(frame_times LIBS frame_times)
  facepalm_fuzz_test(calibration_store LIBS calibration_store head_pose)
  facepalm_fuzz_test(cameras_listing LIBS cameras_listing)
  facepalm_fuzz_test(feelings LIBS feelings)
  facepalm_fuzz_test(head_pose LIBS head_pose)
  facepalm_fuzz_test(session_camera LIBS session_camera)
endif()

if(FACEPALM_LINUX_TRAY)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "FACEPALM_LINUX_TRAY requires Linux")
  endif()
  find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
  add_executable(facepalm_menu tools/menu/main_linux.cc tools/menu/calibration_window_linux.cc)
  set_target_properties(facepalm_menu PROPERTIES OUTPUT_NAME facepalm-menu)
  target_compile_definitions(facepalm_menu PRIVATE FACEPALM_VERSION="${PROJECT_VERSION}")
  target_link_libraries(facepalm_menu PRIVATE settings_document user_service client_approvals
    client_token user_paths daemon_status single_instance calibration_flow calibration
    cameras_listing Qt6::Widgets Qt6::Network)
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
    find_program(XDPYINFO NAMES xdpyinfo REQUIRED)
    # Use the same sibling share directory that the installed tray searches.
    set_target_properties(facepalm_menu PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    configure_file(tools/menu/linux/facepalm.png
      "${CMAKE_CURRENT_BINARY_DIR}/share/icons/hicolor/128x128/apps/facepalm.png" COPYONLY)
    add_executable(linux_tray_integration test/linux_tray_integration.cc)
    set_target_properties(linux_tray_integration PROPERTIES AUTOMOC ON)
    add_dependencies(linux_tray_integration facepalm_menu)
    target_link_libraries(linux_tray_integration PRIVATE Qt6::Core Qt6::DBus Qt6::Network Qt6::Widgets)
    # xvfb-run's own readiness check only waits for the server's lock file
    # to appear, which can be ready before the server accepts clients, so
    # wait_for_display.sh polls xdpyinfo first (#101).
    add_test(NAME linux_tray_integration COMMAND ${DBUS_RUN_SESSION} --
      ${XVFB_RUN} -a "${PROJECT_SOURCE_DIR}/scripts/wait_for_display.sh"
      $<TARGET_FILE:linux_tray_integration> $<TARGET_FILE:facepalm_menu>)
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
  add_executable(facepalm_menu tools/menu/main.mm tools/menu/calibration_window_macos.mm)
  set_target_properties(facepalm_menu PROPERTIES OUTPUT_NAME facepalm-menu)
  target_compile_options(facepalm_menu PRIVATE -fobjc-arc)
  target_link_libraries(facepalm_menu PRIVATE settings_document user_service client_approvals
    client_token user_paths daemon_status calibration_flow calibration cameras_listing
    "-framework AppKit")
  if(BUILD_TESTING)
    add_executable(menu_store_checks test/menu_store_check.mm tools/menu/calibration_window_macos.mm)
    target_compile_options(menu_store_checks PRIVATE -fobjc-arc)
    target_link_libraries(menu_store_checks PRIVATE settings_document user_service
      client_approvals client_token user_paths daemon_status calibration_flow calibration
      cameras_listing GTest::gtest_main "-framework AppKit")
    add_test(NAME menu_store_checks COMMAND menu_store_checks)
  endif()

  add_library(avfoundation_session src/macos/avfoundation_session.mm)
  target_compile_options(avfoundation_session PRIVATE -fobjc-arc)
  target_include_directories(avfoundation_session PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(avfoundation_session PUBLIC frame session_camera
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework Foundation")

  # StartWatching's own device-change observers (#121), through real
  # NSNotificationCenter posts: avfoundation_session_checks follows
  # preview_jpeg_checks' own pattern below for a real-framework round trip
  # rather than facepalm_unit's mocked, coverage-floored kind, since
  # AvFoundationSession has no seam to mock NSNotificationCenter behind.
  # It needs no camera, open or otherwise, so it has no coverage floor,
  # the same as preview_jpeg_checks and menu_store_checks above.
  if(BUILD_TESTING)
    add_executable(avfoundation_session_checks test/avfoundation_session_check.mm)
    target_compile_options(avfoundation_session_checks PRIVATE -fobjc-arc)
    target_link_libraries(avfoundation_session_checks PRIVATE avfoundation_session
      GTest::gmock GTest::gtest_main "-framework AVFoundation" "-framework Foundation")
    add_test(NAME avfoundation_session_checks COMMAND avfoundation_session_checks)
  endif()

  # AppKit calls alone (#61 item 11), needing a real desktop session:
  # unlike avfoundation_session above, which StartWatching's own check
  # exercises without one, screen_context_macos has no unit tests or
  # coverage floor for that reason. ScreenContextReporter, which is
  # tested, takes it behind ScreenSource.
  add_library(screen_context_macos src/macos/screen_context_macos.mm)
  target_compile_options(screen_context_macos PRIVATE -fobjc-arc)
  target_include_directories(screen_context_macos PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(screen_context_macos PUBLIC screen_context
    "-framework AppKit" "-framework CoreGraphics")

  add_executable(camera_probe tools/camera_probe/camera_probe.cc)
  target_link_libraries(camera_probe PRIVATE avfoundation_session camera_controller
    facepalm_config frame_pipeline frame_recorder face_analyzer expression_reader
    gesture_reader head_pose_reader look_away_reader eyes_probe
    "-framework CoreGraphics"
    "-framework ImageIO")

  # The camera preview's JPEG encoding (#37 item 8), through real ImageIO
  # calls: preview_jpeg_check is its test, following menu_store_checks'
  # own pattern for real AppKit/ImageIO round trips rather than
  # facepalm_unit's mocked, coverage-floored kind (it needs no camera, but
  # nor does menu_store_checks need a camera to earn that treatment; both
  # exercise a real framework end to end instead of a seam this file has
  # none of to mock).
  add_library(preview_jpeg src/macos/preview_jpeg.mm)
  target_compile_options(preview_jpeg PRIVATE -fobjc-arc)
  target_include_directories(preview_jpeg PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(preview_jpeg PUBLIC frame
    "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation"
    "-framework Foundation")
  if(BUILD_TESTING)
    add_executable(preview_jpeg_checks test/preview_jpeg_check.mm)
    target_compile_options(preview_jpeg_checks PRIVATE -fobjc-arc)
    target_link_libraries(preview_jpeg_checks PRIVATE preview_jpeg
      GTest::gmock GTest::gtest_main "-framework ImageIO")
    add_test(NAME preview_jpeg_checks COMMAND preview_jpeg_checks)
  endif()
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
  target_link_libraries(facepalm_daemon PRIVATE approved_feed calibration calibration_store
    calibration_reader daemon_calibration
    client_approvals client_token
    daemon_control daemon_options daemon_status face_readers feed_clients user_paths
    avfoundation_session camera_controller facepalm_config frame_pipeline face_analyzer
    expression_reader gesture_reader look_away_reader screen_context_reader
    screen_context_reporter screen_context_macos reaction_feed feeling_store service
    unix_socket face_model facepalm_mediapipe preview_lease preview_publisher preview_readings preview_request
    preview_jpeg "-framework CoreFoundation")
  add_executable(face_model_check tools/face_model_check/face_model_check.cc)
  target_link_libraries(face_model_check PRIVATE face_model frame_times
    head_gesture_model facepalm_mediapipe
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

# The Linux daemon uses the pinned native MediaPipe Tasks C library, which
# scripts/fetch_mediapipe_c.sh fetches with its C headers. Its two Linux
# sources are units of their own (#83): their tests link fakes of MediaPipe's
# C API and of libv4l2 in place of the real libraries, so they need the
# headers but never open a camera or load the model.
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
  # X11 and RandR for screen context (#61 item 11): CI's Linux job needs
  # libx11-dev and libxrandr-dev alongside its existing packages.
  find_package(X11 REQUIRED)
  find_library(XRANDR_LIBRARY NAMES Xrandr REQUIRED)
  facepalm_unit(mediapipe_c_shim DIRECTORY linux)
  target_include_directories(mediapipe_c_shim SYSTEM PUBLIC ${FACEPALM_MEDIAPIPE_C_INCLUDE})
  # Watches /dev for a camera connecting or disconnecting (#111), the
  # callbacks V4l2Session never received before; a seam of its own, so
  # V4l2SessionTest fakes it rather than linking against this unit at all.
  facepalm_unit(v4l2_hotplug DIRECTORY linux)
  target_include_directories(v4l2_hotplug SYSTEM PUBLIC ${V4L2_INCLUDE_DIRECTORY})
  target_link_libraries(v4l2_hotplug PUBLIC Threads::Threads)
  facepalm_unit(v4l2_session DIRECTORY linux)
  target_include_directories(v4l2_session SYSTEM PUBLIC ${V4L2_INCLUDE_DIRECTORY})
  target_link_libraries(v4l2_session PUBLIC Threads::Threads)
  # The camera preview's JPEG encoding on Linux (#37 item 8 follow-up,
  # #108), through real libjpeg calls: preview_jpeg_checks is its test,
  # following src/macos/preview_jpeg.mm's own preview_jpeg_checks (and,
  # behind that, menu_store_checks) for a real round trip through a
  # framework this file has no seam to mock, rather than facepalm_unit's
  # mocked, coverage-floored kind.
  find_package(JPEG REQUIRED)
  add_library(preview_jpeg src/linux/preview_jpeg.cc)
  target_include_directories(preview_jpeg PUBLIC ${PROJECT_SOURCE_DIR})
  target_link_libraries(preview_jpeg PUBLIC frame JPEG::JPEG)
  if(BUILD_TESTING)
    add_executable(preview_jpeg_checks test/linux/preview_jpeg_check.cc)
    target_link_libraries(preview_jpeg_checks PRIVATE preview_jpeg
      GTest::gmock GTest::gtest_main JPEG::JPEG)
    add_test(NAME preview_jpeg_checks COMMAND preview_jpeg_checks)
  endif()
  # screen_context_linux needs a real X server, like avfoundation_session
  # needs a real camera, so, unlike the two units above, it has no
  # facepalm_unit of its own: it goes straight into the daemon.
  add_executable(facepalm_daemon tools/daemon/main.cc
    src/linux/screen_context_linux.cc)
  set_target_properties(facepalm_daemon PROPERTIES OUTPUT_NAME facepalm-daemon
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/facepalm")
  # face_model calls the shim, and the shim MediaPipe, so each comes after
  # the one that calls it.
  target_link_libraries(facepalm_daemon PRIVATE approved_feed calibration calibration_store
    calibration_reader daemon_calibration
    client_approvals client_token
    daemon_control daemon_options daemon_status face_readers feed_clients user_paths
    camera_controller facepalm_config frame_pipeline face_analyzer expression_reader gesture_reader
    look_away_reader screen_context_reader screen_context_reporter screen_context
    reaction_feed feeling_store service session_camera unix_socket v4l2_hotplug v4l2_session
    face_model mediapipe_c_shim ${FACEPALM_MEDIAPIPE_C_LIBRARY} ${V4L2_LIBRARY}
    ${X11_LIBRARIES} ${XRANDR_LIBRARY} preview_lease preview_publisher preview_readings preview_request
    preview_jpeg)
  target_include_directories(facepalm_daemon SYSTEM PRIVATE ${X11_INCLUDE_DIR})
  install(TARGETS facepalm_daemon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT LinuxCamera)
  install(FILES "${FACEPALM_MEDIAPIPE_C_LIBRARY}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/facepalm COMPONENT LinuxCamera)
endif()

# Checks formatting against .clang-format without changing any file.
file(GLOB_RECURSE FACEPALM_CXX_FILES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/src/*.mm ${PROJECT_SOURCE_DIR}/test/*.cc
  ${PROJECT_SOURCE_DIR}/test/*.mm
  ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.cc ${PROJECT_SOURCE_DIR}/tools/gesture_replay/*.h
  ${PROJECT_SOURCE_DIR}/tools/calibrate_cli/*.cc ${PROJECT_SOURCE_DIR}/tools/*.h
  ${PROJECT_SOURCE_DIR}/tools/menu/*.cc ${PROJECT_SOURCE_DIR}/tools/menu/*.mm
  ${PROJECT_SOURCE_DIR}/tools/menu/*.h
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
  target_link_libraries(mcp_service_fixture PRIVATE approved_feed client_approvals client_token
    service reaction_feed feeling_store unix_socket)
endif()
