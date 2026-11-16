# Built inside MediaPipe's workspace, where tools/fixtures/build.sh links this
# directory as the package //facepalm_shim. Not part of facepalm's CMake build.

# The face landmarker behind facepalm_mediapipe.h's C interface. It exports
# only that interface's three functions, so the abseil and protobuf inside it
# cannot clash with another copy in the same process. MediaPipe's own
# mediapipe/tasks/c/vision/face_landmarker:libface_landmarker.dylib is the
# reference.
cc_binary(
    name = "libfacepalm_mediapipe.dylib",
    srcs = [
        "facepalm_mediapipe.cc",
        "facepalm_mediapipe.h",
    ],
    additional_linker_inputs = ["exported_symbols.txt"],
    linkopts = [
        "-Wl,-install_name,@rpath/libfacepalm_mediapipe.dylib",
        "-Wl,-exported_symbols_list,$(location exported_symbols.txt)",
        "-Wl,-dead_strip",
    ],
    linkshared = True,
    deps = [
        "//mediapipe/framework/formats:image_format_cc_proto",
        "//mediapipe/tasks/cc/vision/core:running_mode",
        "//mediapipe/tasks/cc/vision/face_landmarker",
        "//mediapipe/tasks/cc/vision/utils:image_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)
