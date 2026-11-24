# The complete package must reject configurations without its camera inputs.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "SOURCE_DIR and TEST_ROOT are required")
endif()
file(REMOVE_RECURSE "${TEST_ROOT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${TEST_ROOT}"
  -G Ninja -DBUILD_TESTING=OFF -DFACEPALM_UBUNTU_PACKAGE=ON
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(REMOVE_RECURSE "${TEST_ROOT}")
if(result STREQUAL "0" OR NOT "${output}${error}" MATCHES "FACEPALM_UBUNTU_PACKAGE requires")
  message(FATAL_ERROR "complete package accepted missing camera inputs: ${output}${error}")
endif()
