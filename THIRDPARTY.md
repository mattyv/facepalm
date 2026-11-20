# Third-party notices

facepalm ships one third-party model, ported to C++ for #34.

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
