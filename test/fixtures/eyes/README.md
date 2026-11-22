# Eye fixtures

Recordings for a later issue's eye reader (#140 through #145) to replay
through `src/eyes_replay.h` (#139). Each `<name>.probe` file is one
person's full `camera_probe` log from one `scripts/record_eye_fixtures.sh`
session: numbers only, never images, since that script never gives
`camera_probe` a recording directory. Record them with:

```shell
cmake --build build --target camera_probe
bash scripts/record_eye_fixtures.sh
```

It prompts for each fixture below in turn, recording for
`RECORD_EYE_FIXTURES_CUE_SECONDS` (6 by default) times four seconds while
printing its four cues, and saves the log once `camera_probe` exits.

| File | Cues, six seconds apart |
| --- | --- |
| `head_turn_eyes_centered.probe` | center; turn head left, eyes on the camera; center; turn head right, eyes on the camera |
| `eyes_to_corners.probe` | center; look at the top-left corner without moving your head; center; look at the bottom-right corner without moving your head |
| `gaze_other_screen.probe` | look at the near screen; move only your eyes to the other screen; back to the near screen; the other screen again, for longer |
| `blinks.probe` | look ahead normally; blink a few times, as while reading; rest; blink a few more times |
| `fully_away.probe` | look at the camera; turn away completely, as if called; come back; turn away completely again and stay away |
| `glance_keyboard.probe` | look at the screen; glance down at the keyboard; look at the screen again; glance down once more and hold it |
| `thinking_aversion.probe` | look at the camera, as if just asked a question; look away to think, as if answering a hard one; look back with your answer; look away again, for an even harder one |
| `reading_other_screen.probe` | look at the near screen; read the other screen for a while; keep reading it; look back at the near screen |
| `long_blinks.probe` | look ahead normally; close your eyes slowly, hold a couple seconds, then open; rest; close your eyes slowly again, hold longer, then open |

Each file is missing until the user records it; this is deliberate (#139):
an agent working this issue does not open the camera. `eyes_probe_test.cc`
and `eyes_replay_test.cc` cover the line's format and its parsing with
hand-written lines, so they do not need these recordings to pass.

## What these files keep

Each file is camera_probe's log trimmed to what the tests replay: the `eyes`
lines (eye-look, blink, squint and wide scores, and the iris and eye-corner
positions), `head_pose`, the `reaction` lines and the run's summary lines.
The full expression scores (`blendshapes`), the landmark coordinates
(`gesture_raw`) and the gesture model's probabilities (`gesture_probs`) are
dropped; `scripts/record_eye_fixtures.sh` does this when it saves. Recorded
on a Mac with one screen on 2026-11-22; the other-screen fixtures are a look
off to the side.
