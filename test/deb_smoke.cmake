# Checks the archive and optionally installs it in a fresh, disposable Debian profile.
if(NOT DEFINED PACKAGE_FILE OR NOT EXISTS "${PACKAGE_FILE}" OR NOT DEFINED SMOKE_ROOT)
  message(FATAL_ERROR "PACKAGE_FILE and SMOKE_ROOT are required")
endif()
if(EXISTS "${SMOKE_ROOT}")
  message(FATAL_ERROR "SMOKE_ROOT must not already exist")
endif()
get_filename_component(package "${PACKAGE_FILE}" ABSOLUTE)
file(MAKE_DIRECTORY "${SMOKE_ROOT}")

function(fail reason)
  if(installed)
    execute_process(COMMAND apt-get purge -y facepalm)
  endif()
  file(REMOVE_RECURSE "${SMOKE_ROOT}")
  message(FATAL_ERROR "${reason}")
endfunction()

function(check result step)
  if(NOT result STREQUAL "0")
    fail("${step} failed: ${result}")
  endif()
endfunction()

execute_process(COMMAND dpkg-deb -f "${package}" Package OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE name RESULT_VARIABLE result)
check("${result}" "read package name")
if(NOT name STREQUAL "facepalm")
  fail("unexpected package name: ${name}")
endif()
execute_process(COMMAND dpkg-deb -f "${package}" Depends OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE depends RESULT_VARIABLE result)
check("${result}" "read runtime dependencies")
set(dependencies libc6 libqt6widgets qt6-qpa-plugins qt6-wayland)
if(UBUNTU_PACKAGE)
  list(APPEND dependencies nodejs libegl1 libgles2 curl ca-certificates)
endif()
foreach(dependency IN LISTS dependencies)
  if(NOT depends MATCHES "${dependency}")
    fail("missing runtime dependency ${dependency}: ${depends}")
  endif()
endforeach()
if(depends MATCHES "npm|qt6-base-dev" OR (NOT UBUNTU_PACKAGE AND depends MATCHES "nodejs"))
  fail("unexpected developer dependency: ${depends}")
endif()

execute_process(COMMAND dpkg-deb -x "${package}" "${SMOKE_ROOT}/payload" RESULT_VARIABLE result)
check("${result}" "extract archive")
execute_process(COMMAND dpkg-deb -e "${package}" "${SMOKE_ROOT}/control" RESULT_VARIABLE result)
check("${result}" "extract package control")
set(payload "${SMOKE_ROOT}/payload")
set(files
  usr/bin/facepalm-menu usr/bin/gesture_replay usr/bin/calibrate_cli
  usr/share/applications/facepalm.desktop
  usr/share/icons/hicolor/128x128/apps/facepalm.png
  usr/share/doc/facepalm/README.md usr/share/doc/facepalm/copyright
  usr/share/facepalm/LICENSE usr/share/facepalm/THIRDPARTY.md usr/share/facepalm/NOTO-EMOJI-LICENSE)
file(GLOB_RECURSE packaged_files RELATIVE "${payload}" "${payload}/*")
list(SORT files)
list(SORT packaged_files)
if(UBUNTU_PACKAGE)
  list(APPEND files usr/bin/facepalm-install-model usr/bin/facepalm-daemon usr/lib/facepalm/libmediapipe.so
    usr/share/facepalm/MEDIAPIPE-LICENSE usr/share/facepalm/mcp/main.js
    usr/share/facepalm/mcp/feeling-widget.html
    usr/share/facepalm/mcp/node_modules/@modelcontextprotocol/sdk/package.json)
  foreach(item IN LISTS files)
    if(NOT EXISTS "${payload}/${item}")
      fail("missing complete package file: ${item}")
    endif()
  endforeach()
elseif(NOT packaged_files STREQUAL files)
  fail("unexpected package contents: ${packaged_files}")
endif()
foreach(script preinst postinst prerm postrm)
  if(EXISTS "${SMOKE_ROOT}/control/${script}")
    fail("unexpected maintainer script ${script}")
  endif()
endforeach()
execute_process(COMMAND desktop-file-validate "${payload}/usr/share/applications/facepalm.desktop" RESULT_VARIABLE result)
check("${result}" "validate desktop entry")

if(INSTALL_PACKAGE)
  execute_process(COMMAND dpkg-query -W facepalm OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE result)
  if(result STREQUAL "0")
    fail("the install phase requires a profile without Facepalm")
  endif()
  # The Debian slim image excludes most /usr/share/doc files by default.
  execute_process(COMMAND apt-get -o "Dpkg::Options::=--path-include=/usr/share/doc/facepalm/README.md"
    install -y --no-install-recommends "${package}" RESULT_VARIABLE result)
  set(installed TRUE)
  check("${result}" "install package and runtime dependencies")
  foreach(item IN LISTS files)
    if(NOT EXISTS "/${item}")
      fail("installed file is missing: /${item}")
    endif()
  endforeach()
  set(payload "")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
  "${payload}/usr/bin/facepalm-menu" --version
  OUTPUT_VARIABLE version RESULT_VARIABLE result)
check("${result}" "launch tray with its installed Qt runtime")
if(NOT version MATCHES "facepalm-menu [0-9]+\\.[0-9]+\\.[0-9]+")
  fail("unexpected tray version output: ${version}")
endif()

if(UBUNTU_PACKAGE)
  execute_process(COMMAND "${payload}/usr/bin/facepalm-daemon"
    RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
  if(NOT result STREQUAL "2")
    fail("daemon failed to load or reject missing consent: ${result}")
  endif()
  execute_process(COMMAND /usr/bin/node --input-type=module -e
    "import('${payload}/usr/share/facepalm/mcp/server.js')"
    RESULT_VARIABLE result)
  check("${result}" "load packaged MCP adapter")
endif()

set(log "")
foreach(i RANGE 0 49)
  math(EXPR timestamp "${i} * 66")
  string(APPEND log "gesture_raw ${timestamp} 92 102 40 132 102 40 112 117 50 72 112 25 152 112 25 0 0 0\n")
endforeach()
file(WRITE "${SMOKE_ROOT}/replay.log" "${log}")
execute_process(COMMAND "${payload}/usr/bin/gesture_replay" INPUT_FILE "${SMOKE_ROOT}/replay.log"
  OUTPUT_VARIABLE output RESULT_VARIABLE result)
check("${result}" "replay from the package")
if(NOT output MATCHES "gesture_probs [0-9]+ .* class [0-9]+ report [0-9]+")
  fail("packaged replay did not classify the fixture: ${output}")
endif()

if(INSTALL_PACKAGE)
  execute_process(COMMAND apt-get purge -y facepalm RESULT_VARIABLE result)
  set(installed FALSE)
  check("${result}" "purge package")
  foreach(item IN LISTS files)
    if(EXISTS "/${item}")
      fail("purge left an installed file: /${item}")
    endif()
  endforeach()
endif()
file(REMOVE_RECURSE "${SMOKE_ROOT}")
