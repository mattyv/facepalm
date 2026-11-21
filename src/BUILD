# Built inside MediaPipe's workspace, where tools/fixtures/build.sh links this
# directory as the package //src, so a facepalm source that the fixture tool
# shares keeps its own include path, "src/<name>.h". Not part of facepalm's
# CMake build, which builds and unit-tests these sources itself.

cc_library(
    name = "frame_times",
    srcs = ["frame_times.cc"],
    hdrs = ["frame_times.h"],
    visibility = ["//facepalm_fixtures:__pkg__"],
)
