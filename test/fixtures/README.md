# Test fixtures

Recorded inputs that the unit tests replay. The head-pose fixtures hold
numbers only, never images.

## Head poses for looking away

`look_away_replay_test` replays these through the looking-away detector
(#35). Each line is `head_pose <timestamp_ms> <yaw> <pitch> <roll>`, in
degrees, as `camera_probe` prints it. All three files come from one person's
`camera_probe` recordings of 2026-11-20, at 15 frames a second. The spoken
prompts, in seconds from the start, were:

| File | Prompts |
| --- | --- |
| `look_down.head_pose` | down 6, center 12, down 18, center 24 |
| `turns.head_pose` | down 6, center 13, slight turn 17, center 24, medium turn 29, center 35, wide turn 40, center 47 |
| `nods.head_pose` | frown 12, neutral 22, smile while nodding 28, finish 38 |

Those recordings came before `camera_probe` printed `head_pose` lines. Each
was converted by pairing a frame's `gesture_raw` timestamp with the
head-pose reaction printed after it:

```shell
awk '/^gesture_raw/ {t = $2}
  /^reaction \{"yaw"/ {s = $0; gsub(/[^-0-9.,]/, "", s); split(s, a, ",");
    print "head_pose", t, a[1], a[2], a[3]}' probe.log > out.head_pose
```
