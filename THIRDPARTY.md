# Third-party notices

facepalm uses a third-party model and dependencies listed below.

## Idiap's CCDbHG head-gesture model

`src/head_gesture_model.cc`, `src/head_gesture_model_weights.h` and
`test/head_gesture_reference.h` are ported from the `cnn_lmk_hp` checkpoint in
[idiap/ccdbhg-head-gesture-recognition](https://github.com/idiap/ccdbhg-head-gesture-recognition),
licensed GPL-3.0-only:

```text
Copyright (C) 2024 Idiap Research Institute <contact@idiap.ch>
```

Full text: [LICENSES/GPL-3.0-only.txt in that
repository](https://github.com/idiap/ccdbhg-head-gesture-recognition/blob/main/LICENSES/GPL-3.0-only.txt),
and in facepalm's own [LICENSE](LICENSE), which this port's GPL-3.0-only
licensing follows.

Citation:

> Vuillecard, Pierre, Arya Farkhondeh, Michael Villamizar, and Jean-Marc
> Odobez. "CCDb-HG: Novel Annotations and Gaze-Aware Representations for Head
> Gesture Recognition." 18th IEEE International Conference on Automatic Face
> and Gesture Recognition (FG), Istanbul, 2024.

This is the `cnn_lmk_hp` checkpoint, not `cnn_lmk_hp_gaze`, which that
repository licenses CC-BY-NC-SA for non-commercial use only and which
facepalm does not use.

## MCP adapter dependencies

The MCP adapter uses the [Model Context Protocol TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk)
(Copyright 2024 Anthropic, PBC) and [Zod](https://github.com/colinhacks/zod)
(Copyright 2025 Colin McDonnell), both MIT licensed. Their license texts and
those of their transitive dependencies are included in the installed npm
packages; `mcp/package-lock.json` records the exact dependency versions.
The adapter follows the structure of ai-pair's MIT-licensed relay without
copying its source.

## Linux tray icon

The 128×128 `tools/menu/linux/facepalm.png` is the unmodified Noto Emoji
facepalm image, Copyright 2013 Google, Inc., licensed under Apache-2.0. The
[upstream image at commit `9cde38e`](https://github.com/googlefonts/noto-emoji/blob/9cde38ef5ee6f090ce23f9035e494cb390a2b051/png/128/emoji_u1f926.png)
and [upstream licence](https://github.com/googlefonts/noto-emoji/blob/9cde38ef5ee6f090ce23f9035e494cb390a2b051/LICENSE)
are recorded with the full licence in `tools/menu/linux/NOTO-EMOJI-LICENSE`.
SHA-256: `1e93ef63a0f8c2b8461e315fb079eb25a436236cbac6b3e495840f7ffcd5e5dd`.

The Linux tray host follows the API structure of Qt's
[System Tray Icon example](https://doc.qt.io/qt-6/qtwidgets-desktop-systray-example.html).
It uses a simpler status dialog and does not copy the example source.

## Qt Widgets

The Linux tray host links to the system's Qt 6 Widgets libraries, available
under the GNU LGPL-3.0 or GPL-2.0 as described in the
[Qt Widgets documentation](https://doc.qt.io/qt-6/qtwidgets-index.html).
The system packages supply Qt and their accompanying licence notices.

## Linux camera and face model

The Linux camera daemon uses the system's libv4l2 for device access and RGB
conversion. It links the native C library from the official MediaPipe 0.10.35
Linux wheel with headers from the matching source revision. The wheel includes
the MediaPipe Apache-2.0 licence. The development setup uses the same pinned
Face Landmarker model as the project fixtures.
[Video4Linux library documentation][v4l-doc]
and [MediaPipe release][mp-release].

[v4l-doc]: https://www.kernel.org/doc/html/v4.16/media/uapi/v4l/libv4l-introduction.html
[mp-release]: https://pypi.org/project/mediapipe/0.10.35/
