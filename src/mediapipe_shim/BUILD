# Built inside MediaPipe's workspace, where tools/fixtures/build.sh links this
# directory as the package //facepalm_shim. Not part of facepalm's CMake build.

# The face landmarker and face detector behind facepalm_mediapipe.h's C interface. It exports
# only that interface's six functions, so the abseil and protobuf inside it
# cannot clash with another copy in the same process. MediaPipe's own
# mediapipe/tasks/c/vision/face_landmarker:libface_landmarker.dylib is the
# reference.
SHIM_SRCS = [
    "facepalm_mediapipe.cc",
    "facepalm_mediapipe.h",
    "facepalm_mediapipe_internal.h",
]

SHIM_DEPS = [
    "//mediapipe/framework:calculator_framework",
    "//mediapipe/framework/formats:detection_cc_proto",
    "//mediapipe/framework/formats:image_format_cc_proto",
    "//mediapipe/framework/formats:image_frame",
    "//mediapipe/framework/formats:location_data_cc_proto",
    "//mediapipe/modules/face_detection:face_detection_cc_proto",
    "//mediapipe/tasks/cc/vision/core:running_mode",
    "//mediapipe/modules/face_detection:face_detection_short_range_cpu",
    "//mediapipe/tasks/cc/vision/face_landmarker",
    "//mediapipe/tasks/cc/vision/utils:image_utils",
    "@com_google_absl//absl/status",
    "@com_google_absl//absl/status:statusor",
]

cc_binary(
    name = "libfacepalm_mediapipe.dylib",
    srcs = SHIM_SRCS,
    additional_linker_inputs = ["exported_symbols.txt"],
    linkopts = [
        "-Wl,-install_name,@rpath/libfacepalm_mediapipe.dylib",
        "-Wl,-exported_symbols_list,$(location exported_symbols.txt)",
        "-Wl,-dead_strip",
    ],
    linkshared = True,
    deps = SHIM_DEPS,
)

cc_binary(
    name = "libfacepalm_mediapipe.so",
    target_compatible_with = ["@platforms//os:linux"],
    srcs = SHIM_SRCS,
    additional_linker_inputs = ["exports_linux.map"],
    linkopts = [
        "-Wl,-soname,libfacepalm_mediapipe.so",
        "-Wl,--version-script=$(location exports_linux.map)",
    ],
    linkshared = True,
    deps = SHIM_DEPS,
)

# The shim against the real face landmarker. FACEPALM_DATA names the folder
# that tools/fixtures/build.sh downloads MediaPipe's test model and portrait
# into; tools/fixtures/check_shim.sh runs it.
cc_test(
    name = "facepalm_mediapipe_test",
    srcs = [
        "facepalm_mediapipe.cc",
        "facepalm_mediapipe.h",
        "facepalm_mediapipe_internal.h",
        "facepalm_mediapipe_test.cc",
    ],
    deps = [
        "//mediapipe/framework:calculator_framework",
        "//mediapipe/framework/formats:detection_cc_proto",
        "//mediapipe/framework/formats:image",
        "//mediapipe/framework/formats:image_format_cc_proto",
        "//mediapipe/framework/formats:image_frame",
        "//mediapipe/framework/formats:location_data_cc_proto",
        "//mediapipe/framework/formats:matrix",
        "//mediapipe/framework/port:gtest_main",
        "//mediapipe/tasks/cc/components/containers:category",
        "//mediapipe/tasks/cc/components/containers:classification_result",
        "//mediapipe/tasks/cc/components/containers:landmark",
        "//mediapipe/tasks/cc/vision/core:running_mode",
        "//mediapipe/modules/face_detection:face_detection_cc_proto",
        "//mediapipe/modules/face_detection:face_detection_short_range_cpu",
        "//mediapipe/tasks/cc/vision/face_landmarker",
        "//mediapipe/tasks/cc/vision/utils:image_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)
