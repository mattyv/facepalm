# Built inside MediaPipe's workspace, where tools/fixtures/build.sh links this
# directory as the package //facepalm_fixtures.

cc_binary(
    name = "face_landmarker_fixtures",
    srcs = ["face_landmarker_fixtures.cc"],
    deps = [
        "//mediapipe/tasks/cc/vision/core:running_mode",
        "//mediapipe/tasks/cc/vision/face_landmarker",
        "//mediapipe/tasks/cc/vision/utils:image_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)
