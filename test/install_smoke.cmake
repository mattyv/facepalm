# Exercise the production install after moving it, with optional MCP integration.
# Run with -DSOURCE_DIR=<source> -DSMOKE_ROOT=<new temporary directory>.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED SMOKE_ROOT)
  message(FATAL_ERROR "SOURCE_DIR and SMOKE_ROOT are required")
endif()
if(EXISTS "${SMOKE_ROOT}")
  message(FATAL_ERROR "SMOKE_ROOT must not already exist")
endif()
file(MAKE_DIRECTORY "${SMOKE_ROOT}")
set(build "${SMOKE_ROOT}/build")
set(prefix "${SMOKE_ROOT}/initial")
set(moved "${SMOKE_ROOT}/relocated")

function(fail reason)
  file(REMOVE_RECURSE "${SMOKE_ROOT}")
  message(FATAL_ERROR "${reason}")
endfunction()

function(check_result result step)
  if(NOT result EQUAL 0)
    fail("${step} failed: ${result}")
  endif()
endfunction()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${build}" -G Ninja
  -DBUILD_TESTING=OFF -DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE
  -DCMAKE_INSTALL_PREFIX=${prefix} RESULT_VARIABLE result)
check_result("${result}" "production configure without GTest")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}"
  --target gesture_replay_cli RESULT_VARIABLE result)
check_result("${result}" "replay build")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build}"
  RESULT_VARIABLE result)
check_result("${result}" "install")
file(RENAME "${prefix}" "${moved}")

set(log "")
foreach(i RANGE 0 49)
  math(EXPR timestamp "${i} * 66")
  string(APPEND log "gesture_raw ${timestamp} 92 102 40 132 102 40 112 117 50 72 112 25 152 112 25 0 0 0\n")
endforeach()
file(WRITE "${SMOKE_ROOT}/input.log" "${log}")
execute_process(COMMAND "${build}/gesture_replay"
  INPUT_FILE "${SMOKE_ROOT}/input.log" OUTPUT_VARIABLE reference
  RESULT_VARIABLE result)
check_result("${result}" "build-tree replay")
execute_process(COMMAND "${moved}/bin/gesture_replay"
  INPUT_FILE "${SMOKE_ROOT}/input.log" OUTPUT_VARIABLE output
  ERROR_VARIABLE error RESULT_VARIABLE result)
check_result("${result}" "relocated replay: ${error}")
if(NOT output MATCHES "gesture_probs [0-9]+ .* class [0-9]+ report [0-9]+")
  fail("relocated replay did not classify the fixture: ${output}")
endif()

if(NOT output STREQUAL reference)
  fail("relocated replay output differs from build-tree replay")
endif()

foreach(file LICENSE THIRDPARTY.md)
  if(NOT EXISTS "${moved}/share/facepalm/${file}")
    fail("installed legal notice is missing ${file}")
  endif()
  file(SHA256 "${SOURCE_DIR}/${file}" source_hash)
  file(SHA256 "${moved}/share/facepalm/${file}" installed_hash)
  if(NOT source_hash STREQUAL installed_hash)
    fail("installed legal notice differs: ${file}")
  endif()
endforeach()

foreach(file main.js server.js session.js hook-broker.js package.json package-lock.json)
  if(NOT EXISTS "${moved}/share/facepalm/mcp/${file}")
    fail("installed MCP adapter is missing ${file}")
  endif()
endforeach()
if(DEFINED MCP_NODE_MODULES AND DEFINED MCP_FIXTURE)
  file(COPY "${MCP_NODE_MODULES}" DESTINATION "${moved}/share/facepalm/mcp")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "FACEPALM_MCP_ADAPTER=${moved}/share/facepalm/mcp/main.js"
    "MCP_FIXTURE=${MCP_FIXTURE}" node --test
    "${SOURCE_DIR}/test/integration/mcp.test.mjs"
    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
  check_result("${result}" "relocated MCP integration")
elseif(DEFINED MCP_NODE_MODULES OR DEFINED MCP_FIXTURE)
  fail("MCP_NODE_MODULES and MCP_FIXTURE must be supplied together")
else()
  message(STATUS "MCP integration skipped: dependencies and fixture not supplied")
endif()

file(REMOVE_RECURSE "${SMOKE_ROOT}")
