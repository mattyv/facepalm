# Decisions

Each decision made about facepalm and why, newest first. An entry is short: the
evidence and the discussion are in the issue it names. An entry is never
edited once made; a decision that is reversed gets a new entry, and the old one
a line saying which entry replaced it.

## 2026-11-23: Widened eyes are gated on a measured threshold and a side-look guard, replacing #145's reasoned ceiling

-   **Decision (the threshold comes from a recorded true positive, not a
    reasoned ceiling):** `test/fixtures/eyes/widened_eyes.probe` (#145's
    own follow-up, #165) measures eyeWide (the mean of eyeWideLeft/Right)
    across its four six-second cue windows: ahead (resting) mean 0.007,
    max 0.013; widen mean 0.015, max 0.031; relax mean 0.007; widen2 mean
    0.022, max 0.030. `kDefaultEyeSignsWidenedEyesThreshold`
    (`src/config.h`) moves from the old reasoned ceiling (0.15) to 0.025,
    above the ahead/relax baseline and below both widen cues' own means.
-   **Decision (a side-look guard, since eyeWide alone cannot tell a widen
    from a sideways glance):** every other fixture's own eyeWide also
    rises when the eyes turn aside -- up to 0.051
    (`reading_other_screen.probe`) and 0.036 (`gaze_other_screen.probe`),
    both past the new threshold, and held for long runs (91 and 99 frames
    at or past 0.02, against `widened_eyes.probe`'s own 39). What tells
    them apart is how far the eyes are turned horizontally: at eyeWide
    >= 0.02, the larger of mean eyeLookIn and mean eyeLookOut reads 0.08
    for `widened_eyes.probe` and 0.09 for `thinking_aversion.probe`,
    against 0.35 to 0.36 for the two other-screen fixtures.
    `EyeSignSample` (`src/eye_signs.h`) gains a `side_look` field
    (`SampleFrom` fills it from `src/eyes_probe.h`'s own eyeLookIn/
    eyeLookOut indices, 13/14 and 15/16); `EyeSignSettings` gains
    `widened_eyes_max_side_look` (default 0.2, `src/config.h`'s new
    `eye_signs.widened_eyes_max_side_look`); `EyeSignsOf`'s widened run
    now requires `eye_wide >= widened_eyes_threshold` **and**
    `side_look < widened_eyes_max_side_look`, held together for
    `widened_eyes_hold_ms`.
-   **Decision (the hold rises to 1,000 ms, since the measured signal is
    held, not instantaneous):** `widened_eyes.probe`'s own longest run at
    or past the threshold is 39 frames, about 2.6 s at 15 fps;
    `kDefaultEyeSignsWidenedEyesHoldMs` moves from 200 to 1,000 ms,
    comfortably inside that run and clear of `thinking_aversion.probe`'s
    own longest run at the same bar (4 frames).
-   **Why weak, and kept anyway:** this user's own widen cue peaks at
    eyeWide 0.031, a small lift over their own 0.007 baseline, so the
    signal is weak for them specifically. It still only ever feeds the
    personal read's own tuning (`src/tuning.h`), never
    `specs/FeelingBeat.tla`'s `Rule`, so a weak or occasionally silent
    sign costs nothing but a feature that rarely fires.
-   **Rejected:** keeping the reasoned 0.15 ceiling now that a true
    positive exists -- it would never fire for this user, whose own widen
    cue peaks at 0.031, more than 4x under it.
-   **Evidence:** [#145](https://github.com/mattyv/facepalm/issues/145),
    measured through `src/eyes_gaze_replay.h`'s own `eye_sign` line
    against the recorded `widened_eyes.probe`.

## 2026-11-23: Publish each calibration as a fresh shared profile

A finished calibration is published as a new
`std::shared_ptr<const CalibrationProfile>`; the screen context reporter
and the look-away reader each keep a snapshot and swap the whole pointer,
so the camera thread never reads a profile another thread is assigning.
The look-away reader composes gaze with the profile's own gains once one
is set, as the reporter already did, so the other-screen box test and the
boxes agree. A resting step where every frame blinked is refused like a
weak gains step, and `eye_signs.min_window_ms` must be at least 1.
Why: a second calibration in one run copy-assigned the published profile
under a live reader (use-after-free with a new screen count). Rejected:
a lock around every read of a long-lived profile (the readers already
hold their own locks; a snapshot needs none on the camera thread).
Evidence: [#167](https://github.com/mattyv/facepalm/issues/167),
[#170](https://github.com/mattyv/facepalm/issues/170),
[#171](https://github.com/mattyv/facepalm/issues/171).

## 2026-11-23: macOS screens and mouse are normalised to top-left-origin, y-down at the source, and the calibration wizard orders and names screens off a primary captured once per session, closing #168 and #169

-   **Decision (flip at the source, in `MacosScreenSource`, rather than
    teach `ContentHitFrom` two coordinate spaces):** `MacosScreenSource::
    Screens()` and `::Mouse()` (`src/macos/screen_context_macos.mm`) used
    to hand over raw Cocoa frames and `NSEvent` points -- y-up, origin at
    the primary screen's bottom-left -- while `ScreenRect` and
    `ContentRect`'s own contracts, and `ContentHitFrom`'s math
    (`src/screen_context.cc`), already assumed top-left-origin, y-down,
    the space Linux's XRandr geometry already is. On any multi-monitor
    arrangement this judged a beat's content hit, and `SideOf`/`ZoneOf`'s
    screen-relative reads, against the wrong vertical axis (#169). The
    fix is a new pure function, `YDownFromYUp` (`src/screen_context.h`,
    `src/screen_context.cc`), that flips a y-up rectangle's top (or a
    point, with `height` 0) against the primary screen's own height; only
    `MacosScreenSource` calls it. The rejected alternative was passing
    the platform's own space through and having `ContentHitFrom` branch
    on it: that would spread the y-up/y-down distinction into every
    caller of `ScreenRect`, rather than fixing it once where the y-up
    reading enters the system.
-   **Decision (`main` is the true primary screen, not the key window's
    own):** `MacosScreenSource::Screens()` used to mark `ScreenRect::main`
    on `[NSScreen mainScreen]`, the screen holding the key window, a
    pre-existing mismatch with every other reader's assumption that
    `main` is the screen the camera faces (load-bearing since #92, #169).
    It now marks `[NSScreen screens].firstObject`, AppKit's own true
    primary (the screen with the menu bar), stable regardless of which
    window has focus.
-   **Decision (the calibration wizard captures its own primary once, at
    `Start`, instead of reading `[NSScreen mainScreen]` on every step):**
    `tools/menu/calibration_window_macos.mm`'s `OrderedScreens`,
    `ScreenAt`, `ScreenPosition` and `StepPrompt` used to read
    `[NSScreen mainScreen]` fresh each time they ran. Since the wizard's
    own window becomes the key window as `moveToScreenForStep:` moves it
    onto each step's screen, that screen became "primary" on the very
    next render, reordering and renaming every other screen out from
    under the person mid-session (#168). `CalibrationWindowController`
    now captures `[NSScreen screens].firstObject` once, in
    `primaryClicked:` when the session starts, into a new `_primaryScreen`
    ivar, and threads it through every one of those functions instead.
    `specs/CalibrationMenu.tla`'s own `windowOnScreen` abstracts only
    whether the window moved onto the step's intended screen, not which
    physical `NSScreen` that is, so this bug sat below the model's own
    abstraction; no spec change was needed, only `specs/README.md`'s
    prose description of the macOS/Linux implementation, which already
    described "primary first" as the intent.
-   **Verified:** #168 and #169 were read from the code at `065bd29`
    (both issues cite file:line there) rather than reproduced on a real
    multi-monitor Mac, since no second monitor was available in this
    session; the fix is covered by `YDownFromYUpTest`
    (`test/screen_context_test.cc`), a pure-function test against
    monitor arrangements including a screen above and a shorter screen
    left of the primary, run on real hardware (macOS, Apple clang). The
    calibration window's own screen-capture change has no test of its
    own, following `src/macos/screen_context_macos.h`'s own precedent for
    AppKit code that needs a real desktop session with more than one
    screen to exercise meaningfully; `menu_store_checks` still builds and
    passes with the changed file linked in.

## 2026-11-23: Blink rate, a long blink and widened eyes are wired live into the beat's own evidence, the same buffer and callback shape gaze already uses, closing #165

-   **Decision (one buffer, not two; eye signs ride alongside gaze's own
    `Sample`/`Between`):** rather than a separate reader and reporter for
    eye signs, `ScreenContextReader::OnSample` grew a fifth argument
    (`const std::optional<EyeSignSample>&`, nullopt on a frame with no
    face) and `ScreenContextReporter::Sample` a sixth
    (`const std::optional<EyeSignSample>&`, defaulted so every existing
    call keeps compiling), feeding a second `std::deque` alongside
    `samples_`, evicted by the same `max_window_ms` on every call --
    even one that gave it nothing to add, since a long stretch of
    missing faces should still age the buffer out. The alternative, a
    standalone `EyeSignsReader`/`EyeSignsReporter` pair mirroring
    `ScreenContextReader`/`ScreenContextReporter` exactly, was rejected:
    it would duplicate the mutex, the eviction loop and the daemon's own
    per-frame hook for no benefit, when the two signals are already
    sampled on the same frame, at the same cadence, by the same
    `OnFace`.
-   **Decision (the eye sign's own timestamp is replaced with `Sample`'s
    `at_ms`, not kept):** `EyeSignSample::timestamp_ms` is also used
    standalone, by fixture replay and `SampleFrom`'s own frame-capture
    clock (#145); `ScreenContextReporter::Sample` overwrites it with
    `at_ms`, the wall clock `GazeSample::at_ms` already uses, before
    buffering, so `EyeSignsBetween`'s bounds -- asked in the same wall-
    clock basis `ReactionFeed` marks a beat's start and end on -- agree
    with what is stored, rather than drifting against the camera's own
    capture-time clock.
-   **Decision (one more `ReactionFeed` constructor parameter,
    `EyeSignsWindowFn`, mirroring `GazeWindowFn` exactly):**
    `ReactionFeed::Beat` asks it the same `[started_ms, ended_ms)` it
    asks `gaze_window_`, and folds the three fields into the evidence
    right where gaze already is -- never onto `Interpretation`, since
    blink rate, a long blink and widened eyes are the personal read's
    own feature (#145), not a client-facing reply field the way gaze is;
    `specs/FeelingBeat.tla`'s `ContextNeverChangesFeeling` and
    `ThinkingGuardFires` are unaffected, since neither guard ever reads
    them.
-   **Decision (`scripts/record_eye_fixtures.sh` grows a tenth fixture,
    `widened_eyes`, instead of leaving #145's reasoned-not-measured
    threshold as is):** #145's own `widened_eyes_threshold` (0.15) and
    `widened_eyes_hold_ms` (200) were reasoned from the absence of a
    genuine "widen the eyes" cue across #139's original nine fixtures,
    not measured against one. Recording a tenth with exactly that cue
    (`FIXTURES`'s own new entry, its bats test's counts bumped from nine
    to ten) lets a later change tighten both against a real positive,
    the way #145's own decisions entry asked for; the fixture itself is
    not recorded in this change (an agent working this issue does not
    open the camera, #139's own rule), only the script's own capability
    to record it.

## 2026-11-23: Blink rate and a long blink join the personal read as coarse signs; widened eyes too, on a reasoned ceiling rather than a measured positive, closing #145

Its widened-eyes ceiling is replaced by "Widened eyes are gated on a
measured threshold and a side-look guard, replacing #145's reasoned
ceiling" (2026-11-23); blink rate and a long blink are unaffected.

-   **Decision (three more `Evidence` fields, following #143's own
    shape):** `src/eye_signs.h` is a new file: `BlinkRate`, `LongBlink`
    and `WidenedEyes`, each a coarse bucket like `src/screen_context.h`'s
    `Gaze`, plus the pure `EyeSignsOf` that reads a window of
    `EyeSignSample`s into all three. `src/feelings.h`'s `Evidence` gains
    `blink_rate`, `long_blink` and `widened_eyes` alongside `sign`,
    `beat` and `gaze`; `EvidenceOf` never sets any of the four, the same
    as it already left gaze for #142 to fill in. Unlike #143, no prior
    issue already built a live windowed reader for these three (gaze had
    #142's `GazeWindowFn` and `ScreenContextReporter::Between` ready to
    wire in); building one is real work of its own -- a stateful sample
    buffer and a new `ReactionFeed::Beat` parameter -- so this issue
    stops at the pure computation and the counts file, and files a
    follow-up issue for the live wiring rather than leave it unsaid.
    `specs/FeelingBeat.tla`'s `Rule`, `Interpret` and
    `ContextNeverChangesFeeling` are untouched, so, as with #143,
    `src/tuning.cc` needed no spec change: it is pure computation outside
    every spec's model.
-   **Decision (the fallback still only checks sign and beat, now pooling
    four context fields instead of one):** `Tuning::Read`'s own fallback
    loop already read `other.sign == evidence.sign && other.beat ==
    evidence.beat` with no mention of gaze; adding three more context
    fields to `Evidence` changes nothing about that loop, since it never
    named gaze as what it was ignoring -- it pools whatever the other
    fields happen to be. The one new code is `ContextOf` in
    `src/tuning.cc`, which reads the three (or one, or none) context
    words a line's own format version carries, kept apart from
    `Evidence` itself so a malformed or short line never leaves
    `Evidence::beat` -- which has no "unknown" enumerator of its own --
    default-constructed and unset.
-   **Decision (format bumps straight to v3; a v1 or v2 file is migrated,
    never refused):** the counts file's header bumps from `facepalm
    feelings 2` to `facepalm feelings 3` for the three new words, one
    bump for all three at once rather than one per sign, since they were
    measured and added together. Following #143's own precedent (and its
    own rejected alternative, recorded in that entry): a v1 file's picks
    read with all four context fields `unknown`, a v2 file's with the
    three new ones `unknown` and gaze kept, and `Tuning::Text` always
    writes v3. Refusing either outright, mirroring
    `src/calibration_store.cc`'s version bump for #137, would throw away
    the person's own picks for a file they never asked to change.
-   **Decision (widened eyes ships on a reasoned ceiling, not a measured
    positive, rather than being dropped like eye-rolls):** #139's nine
    fixtures were recorded for #140 through #145 together, before #145's
    own cue list was finalized, and none of the nine asks the person to
    widen their eyes -- every fixture's own eyeWide blendshape peaks at
    0.051 at most (`reading_other_screen.probe`), read through
    `src/eyes_gaze_replay.h`'s new `eye_sign` line. Blink rate and a long
    blink both have a true positive to measure against
    (`test/fixtures/eyes/blinks.probe` and `long_blinks.probe`); widened
    eyes does not. The rejected alternative was dropping widened eyes
    from this change the way eye-rolls were already dropped from #145's
    own scope, on the reasoning that "#139's recordings show they are
    detectable" (the issue's own condition) fails for a sign no fixture
    tries to elicit at all. It is kept instead because every fixture's
    own eyeWide ceiling is still informative -- it bounds the false-
    positive rate a threshold well above it would have on ordinary faces
    doing ordinary things -- and because the personal read only ever
    weighs this sign for a correction the person themselves made; a
    threshold that never fires on a real "widened eyes" moment costs
    nothing but a sign that stays silent, while dropping it loses the
    chance to tune it later against a real recording.
    `kDefaultEyeSignsWidenedEyesThreshold` (0.15, three times that
    ceiling) and `kDefaultEyeSignsWidenedEyesHoldMs` (200,
    `kDefaultExpressionsHoldMs`'s own flicker guard) are both reasoned
    this way, not measured against a positive; `src/eye_signs.h` and
    `src/config.h` say so again where each lives, and the follow-up issue
    for live wiring also asks for a dedicated "widen the eyes" fixture to
    tighten them against a real one.

## 2026-11-23: BeginStop's already-left branch is held deterministically, not diagnosed by luck, closing #162

-   **Decision (the flaky branch is `connection.name && !connection.left` in
    `Service::BeginStop`, not `Watched`'s `AwaitHangUp`):** that `&&`
    skips a connection Watched's watcher already left without touching
    the feed again, but only when `BeginStop`'s own read runs before
    Serve's cleanup resets `name`; a concurrent `Stop` races that reset,
    and both outcomes look identical to every existing assertion (no
    second `Disconnect` either way), which is why #143's heavy stress
    testing of `AwaitHangUp` (1012 runs, 30x repeats, 8-way parallel) never
    caught it: it was never the branch moving. `test/service_test.cc`'s
    new `AStopFindingAnAlreadyLeftWaiterSkipsItsOwnDisconnect` holds
    `Wait` from returning until `Stop`'s `BeginStop` has already run,
    confirmed through the listener's `Close` (the next thing `Stop` calls
    after `BeginStop` returns), so `name` is still set and `left` already
    true every run, not only when the scheduler happened to land that way.
-   **Decision (tighten, not fully pin, the `erase_if` "done" branch in
    `AcceptAll`):** `FinishedConnectionsAreCleanedUp` connected the next
    client right after each hang-up with no synchronisation at all, racing
    the whole next handshake against the hung-up connection finishing
    `Serve`. It now waits for that connection's own `Disconnect` call
    before the next `Named`, which removes that whole handshake from the
    window and leaves only the uncontended lock around `connection.done`
    itself. Closing that last sliver outright would need a test hook on
    `done` with no other purpose, for a gap that is a few instructions
    under an uncontended mutex (no syscall, so no scheduler point) rather
    than a wait with scheduling discretion; rejected as disproportionate
    to the risk. The CI artifact `scripts/coverage.sh` and
    `.github/workflows/ci.yml` now write and upload on a macOS coverage
    failure -- the raw profiles plus `llvm-cov show -show-branches=count`
    for any file below its floor -- is the backstop if this sliver, or
    anything else timing-dependent, ever does flip: the next failure names
    the exact branch instead of only the missing percentage.

## 2026-11-23: The personal read weighs a beat's own gaze, never the rule, closing #143

-   **Decision (gaze is `Evidence`'s third field, not a fourth map):**
    `src/feelings.h`'s `Evidence` gains a `Gaze gaze = Gaze::kUnknown`
    field alongside `sign` and `beat`; `EvidenceOf` never sets it, leaving
    it to whichever caller knows the beat's own gaze.
    `ReactionFeed::Beat` fills it in from the same `GazeWindow` its reply
    already reports (#142, #144), before the evidence is given to
    `tuner_.Read` or stored for a later `Feel`, so the personal read and
    the correction it later learns from both tune on the one gaze the
    client was told. `FeelingBeat.tla`'s own `Rule` and `Applied` are
    untouched, so `ContextNeverChangesFeeling` still holds exactly as
    #142 left it -- `src/tuning.cc` is pure computation outside every
    spec's model (`specs/FeelingFeedback.tla`'s own header comment
    already says so), so this needed no spec change, following
    `CLAUDE.md`'s "spec first" rule only where a spec models the thing
    changing.
-   **Decision (below the minimum for one gaze, pool every gaze of the
    same evidence, not read gaze-blind):** `Tuning::Read` first tries the
    picks for the evidence's own gaze; only once their own total is
    under `kLeastPicks` does it fall back to summing every pick of the
    same sign and beat across every gaze, and reads that pooled count the
    same held-out way. The alternative -- ignoring gaze whenever the
    specific bucket is thin, rather than pooling it in -- would silently
    throw away picks a person already made under a different gaze every
    time the gaze changes, so a feeling with no generic rule (engagement,
    annoyance) would need to wait for `kLeastPicks` fresh picks under
    *each* gaze it is ever read with, rather than the pool as a whole.
    Once a gaze's own bucket reaches the minimum, it is read alone, never
    pooled, even if pooling would also succeed
    (`TuningGazeTest.AtOrAboveTheMinimumForOneGazeReadsItAlone`): two
    gazes of the same evidence can then disagree, which is the point of
    adding gaze as a feature at all.
-   **Decision (the counts file's version bumps from 1 to 2, migrated on
    read, not refused):** `Tuning::Text`/`Parse`'s header and each line's
    word count change for the added `gaze` word (`src/screen_context.h`'s
    `BeatGazeName`/`BeatGazeNamed`, the same wording a beat's own reply
    already uses, not `Name(Gaze)`'s `this_screen`/`other_screen`). A
    file a pre-#143 daemon saved has no such word; `Parse` reads it as
    v1 (four words: sign, beat, feeling, count), giving every one of its
    picks gaze `Gaze::kUnknown` -- the gaze it would have carried had
    screen context always been on -- and `Text` always writes v2, so the
    next save upgrades the file in place.
-   **Decision rejected (refusing a v1 file, the way
    `src/calibration_store.cc` refuses a pre-#137 calibration profile):**
    the first cut of this change followed that precedent outright --
    `Parse` rejected any header but the current one, so
    `FeelingStore::Open` refused a pre-#143 file entirely. Rejected once
    reviewed, because the two files are not alike: a calibration profile
    missing is a normal, expected state (`uncalibrated`) the daemon
    already runs in, so the person simply redoes the short calibration
    session: no data of theirs is lost. A feelings file is the person's
    own accumulated picks (#35) -- refusing to open it would silently
    reset every one of them the first time they upgrade past #143, for a
    file they never touched and a format change they have no reason to
    know about. Migrating in place, above, keeps them instead.

## 2026-11-23: A look survives a relearning gap, and the squint gate settles past a blink burst, closing #157 and #158

-   **Decision (Gap and Learn stop clearing a held look, and Return waits
    for a resting pose, closing #157):** `LookAwayDetector::Update`'s own
    `!resting` branch no longer resets `away_`, `saw_other_screen_` and
    `saw_gone_`; it only declines to judge this frame, since there is no
    resting pose yet to judge it against. The branch also now runs after
    the `!gaze` check, not before, so a face lost while relearning still
    extends the open look (`saw_gone_`) the way a face lost at any other
    time does. `specs/LookAway.tla`'s `Gap` and `Learn` match: neither
    clears `held`, `heldFromBetween`, `heldLong`, `otherScreen` or
    `sawGone` any more, and `ReturnByMove`/`ReturnByAbsorb` both gained a
    `resting = "learned"` guard, so a look cannot report while relearning
    is still under way (it previously could not either, but only because
    `held` was always false there; once `held` survives a gap, that
    silent precondition needed to become an explicit one).
    `GapNeverDropsAHeldLook`, a new invariant over a new ghost
    (`heldAcrossGap`, snapshotting `held` into a gap), is what proves a
    held look is never cleared except by actually reporting it; `LookAway.cfg`
    holds at 922 distinct states (was 681), with `Learn` added to the
    model's weak fairness so a look stalled on relearning is not stuck
    forever. Verified by reproducing the bug on `test/fixtures/eyes/
    fully_away.probe` (#139): replayed through `src/eyes_gaze_replay.h`,
    the fixture's first turn away now reports
    `looked_away 282924515 gone 6798`, where it reported nothing before.
    `test/look_away_replay_test.cc`'s own `turns.head_pose` fixture
    gained a third, previously-lost report for the same reason (its wide
    turn, across a face lost for five seconds) -- a known, accepted
    limitation named in #141's own decision entry and in that test's
    comment, closed as a side effect of this fix rather than its target.
-   **Decision (`GuardLearned` is kept as unbitten defence in depth):**
    once `ReturnByMove`/`ReturnByAbsorb`'s own `resting = "learned"` guard
    exists, `GuardLearned = FALSE` no longer breaks any checked property
    (TLC: 1,112 states, no error) -- the new guard alone already keeps
    `NoReportWhileLearning` even if `Hold` itself ran too early. Rejected:
    removing `GuardLearned`/`Hold`'s own `resting = "learned" \/
    ~GuardLearned` term, since the code's matching guard
    (`ExpressionTracker::Away`'s own `!learning`) still has a real,
    separate job (freezing expression levels while away) unrelated to
    `LookAwayDetector`; `specs/README.md`'s own mutation table says so,
    naming the mutation rather than dropping it silently.
-   **Decision (a no-face line before any real frame is not yet a look,
    a caller question, not a resting-pose one):** fixing #157 exposed a
    second, latent bug the first one had been masking: `LookAwayReader::
    OnNoFace`, `ReplayLookingAway` (`src/look_away_replay.cc`) and
    `ReplayEyesAndLookAway` (`src/eyes_gaze_replay.cc`) each call
    `LookAwayDetector::Update` for a leading no-face line using a
    placeholder "no real frame yet" timestamp (`last_timestamp_ms_`'s own
    default, or `last = -1`) when that line precedes any real frame --
    every one of #139's own `.probe` fixtures starts with a few such
    lines, recorded while the camera was still warming up. Once a held
    look could survive past that call, this placeholder was read as the
    look's own start, giving an absurd duration once a real face finally
    settled (`eyes_gaze_replay_test.cc`'s `GlanceAtTheKeyboardIsNot
    ReportedAsALookAway` and `ThinkingAversionReportsAverted` both failed
    this way). `LookAwayDetector` itself is unchanged and still opens a
    look from its own very first call, gaze lost or not
    (`GoneKindTest.NoFaceFromTheVeryFirstFrameStartsTheLookToo`, #141's
    own design, with a deliberate, meaningful timestamp); each of the
    three callers now tracks whether it has ever read a real frame
    (`LookAwayReader::had_face_`, or `last != -1`) and does nothing for a
    no-face line before that, rather than opening a look from a timestamp
    that was never a real reading.
-   **Decision (the squint gate's own settle time, not `hold_ms`, closing
    #158):** `ExpressionTracker::EyesSettling` keeps eyes_narrowed's own
    gate (#140) shut for `kEyeGateSettleMs` (1,200 ms) after eyeLookDown
    or eyeBlink themselves last crossed back under their threshold, since
    eyeSquint's own decay lags theirs by close to that long between two
    blinks in a rapid burst (`test/fixtures/eyes/blinks.probe`, #139:
    eyeSquint stays elevated for about 999 ms after eyeLookDown/eyeBlink
    clear, before the next blink in the burst raises them again).
    Rejected: raising `Settings::hold_ms` instead, which every expression
    shares and which a value this large would delay by a full second
    (#158's own finding); reading eyeSquint's own decay rate directly,
    which would need tracking its slope and risks misreading a real,
    sustained squint as still-decaying noise, where a fixed settle keyed
    to the two stand-in scores does not. `eyes_gaze_replay_test.cc`'s own
    `BlinksAreNotReadAsEyesNarrowedAndDontMoveGaze` tightened from "at
    most one" `eyes_narrowed` start to none, now that it holds.

## 2026-11-22: The eye/gaze/squint/calibration/look-away defaults are tuned on #139's recordings, closing #137, #139 and #150

-   **Decision (what changed, and what did not):** of every provisional
    default #137, #140 and #141 left to be tuned on a recording,
    `eye_gaze.horizontal_gain` and `eye_gaze.vertical_gain` move from 30 to
    45, and `expressions.eye_look_down_threshold` from 0.3 to 0.2
    (`src/config.h`, `src/calibration.h`'s matching `kDefaultGain`); every
    other one -- `eye_gaze.blink_threshold`, `median_ms`, `resting_ms`,
    `gap_ms`, `head_only_yaw_degrees`, `calibration.min_head_travel_degrees`,
    `min_axis_degrees`, `min_spread_degrees`, `looking_away.averted_ms` and
    `other_screen_margin_degrees` -- is left as it was, either confirmed by
    the recordings or, for the three calibration minimums and the
    other-screen margin, because #139's nine fixtures (`test/fixtures/eyes`)
    include no calibration gains step or calibrated screen to measure one
    against. The measured distributions behind each choice are in #139's
    closing PR.
-   **Decision (measured through a new replay, not the existing ones):**
    `src/eyes_gaze_replay.h`/`.cc`, a new unit, replays a recording's
    `head_pose` and `eyes` lines together through `ExpressionTracker`,
    `EyeGazeTracker` and `LookAwayDetector` in `LookAwayReader`'s own order
    (#139's own `eyes_probe_test.cc`-style formatting, #137's `ComposeGaze`),
    since `src/look_away_replay.h`'s existing `ReplayLookingAway` reads head
    pose alone by design and `src/eye_gaze.h`'s own tests build frames by
    hand. `test/eyes_gaze_replay_test.cc`'s fixture tests are what hold the
    tuned defaults to #139's recordings, and what found the two findings
    below.
-   **Decision (two findings filed, not fixed here):** facepalm#157 (a look
    away spanning a no-face gap longer than `ExpressionTracker`'s own
    relearning window can end with no report at all, reproduced on
    `fully_away.probe`) and facepalm#158 (the squint-guard can still miss
    eyeSquint's own slower decay between two blinks close together,
    reproduced on `blinks.probe`) are both design gaps in code this PR does
    not otherwise change, not constants; each is filed rather than fixed,
    and `test/eyes_gaze_replay_test.cc` asserts the behaviour each fixture
    actually shows today, naming the issue.
-   **Decision (#150, the squint-guard's own two thresholds join the
    Settings windows):** `MenuSettings` (`src/settings_document.h`) gains
    `expression_eye_look_down_threshold` and `expression_eye_blink_threshold`,
    wired into the macOS menu's "Advanced tuning" rows
    (`tools/menu/main.mm`) and the Linux tray's own grid
    (`tools/menu/main_linux.cc`) the same way every other expression
    setting already is. The Linux change mirrors the macOS one; this
    environment cannot build or run the Linux tray to check it.

## 2026-11-22: A beat's reply carries gaze as context, never a rule's input, closing #142 and #144

-   **Decision (gaze is context, not a rule, with one named exception):**
    `src/feelings.h`'s `Interpret`/`Rule` take only cues, unchanged;
    `ReactionFeed::Beat` adds the majority gaze over the beat's own window
    (`ScreenContextReporter::Between`) to the interpretation *after* the
    cue-only feeling is computed, and only ever downgrades a question
    beat's own feeling to unknown when that majority reads averted
    (`Gaze::kOtherScreen`) -- "eyes never decide a feeling by rule"
    (#137's own comment on #142), everywhere else gaze is reported but
    changes nothing. `specs/FeelingBeat.tla` gains this as
    `GuardAvertedUnknown`, the thinking guard, checked by two new
    invariants against a ground-truth operator (`Averted(c)`) kept apart
    from the formula that computes the real `feeling`, the same
    "ghost checked against the computation" shape `during`/`lost` already
    use for `Rule`.
-   **Decision (the model's own stand-in for "majority"):** rather than
    model a true tally, `gazeDuring[c]` is overwritten by every `Look`
    while a beat stays open, so it holds whichever zone the beat *ends*
    with. The real code (`ScreenContextReporter::Between`) tallies a true
    majority; the model's simplification is enough to prove the two
    properties that matter (gaze never changes the feeling outside the
    guard, and the guard fires exactly when it should), and a short
    window's gaze rarely flips in practice (#137's own median filter and
    dwell already damp that before it ever reaches a beat).
-   **Decision (state space, not the model's own correctness):** `Kinds`
    (question/proposal) became a `CONSTANT`, like `Cues`, once `Mark`
    needed one: a first attempt that quantified over both kinds in every
    config nearly doubled `FeelingBeat.cfg`'s own 3.8 million states.
    `FeelingBeat.cfg` holds `Kinds` to one element and `MaxLooks = 0`
    (inert gaze); a new `FeelingBeat_Gaze.cfg`, with one client and small
    bounds, is the one config that spends the branching both kinds and a
    real `Look` cost, and where the thinking guard's own mutations run.
-   **Decision (#144's content rectangle skips the calibration profile):**
    `ContentHitFrom` maps gaze into a content rectangle by linear
    interpolation across `CoarseGazeFrom`'s own uncalibrated cone
    (`settings.other_screen_degrees`, centred on the resting pose),
    never a calibration profile's own saved corners. Rejected: using
    `#137`'s profile (`NearestCalibratedScreen` plus its four corners)
    for a finer mapping -- it would need an unverified assumption that a
    profile's `screens[i]` lines up index-for-index with
    `ScreenSource::Screens()[i]`, and #144 asks for "coarse only" (#83)
    regardless, so the simpler, calibration-free cone already meets the
    bar without that risk. `ContentHitFromTest` in
    `test/screen_context_test.cc` is the oracle; like `CoarseGazeFrom`,
    it carries no `specs/` entry of its own (a stateless computation, no
    lifecycle).
-   **Decision (no default argument on `Feed::Beat`):** Google's style
    forbids a default argument on a virtual function
    (`google-default-arguments`), so the content rectangle `Beat` gained
    has none, on `Feed`, `ReactionFeed` or `ApprovedFeed`; every call site
    (`src/service.cc`, every test, `test/fixtures/mcp_service_fixture.cc`)
    gives it `std::nullopt` explicitly instead.

## 2026-11-22: Looking away reads composed gaze, reports once at return with its kind and duration, closing #141

-   **Decision (reporting moves from mid-look to the look's own end):**
    `LookAwayDetector` (`src/look_away.cc`) no longer reports
    `{"gesture":"looking_away"}` the moment `hold_ms` elapses while still
    away (#35's own design); it reports
    `{"gesture":"looked_away","kind":"...","duration_ms":n}` once the
    gaze returns within half of `Settings::degrees`, so thinking (a short
    `averted` look) can be told from leaving (`away`, `other_screen` or
    `gone`) only once the whole look is known. `specs/LookAway.tla`'s own
    `Report` and `Rearm` actions, separate in #35's model, merge into one
    `Return`, since there is no longer an earlier moment to report at or
    a later one to rearm on its own.
-   **Decision (the kinds, and their priority):** `LookAwayKind` is
    `kAverted` (back within `Settings::averted_ms` of the look's own
    start), `kOtherScreen` (the gaze was ever inside another calibrated
    screen's own box while away), `kAway` (longer than `averted_ms`,
    never in another screen's box) or `kGone` (no face to read at some
    point during the look). A look under `Settings::hold_ms` is a glance
    and is never reported, under any kind. Priority at the moment the
    kind is read, highest first: `kGone`, then `kOtherScreen`, then
    `heldLong` (`averted_ms` elapsed) deciding `kAway` from `kAverted`.
    Leaving the camera's view is read as more informative than where the
    gaze last sat before it did, and actually landing on another screen
    is more informative than how long the look merely ran.
-   **Decision (another screen's box, not a side guess):** #141's
    `kOtherScreen` reads `src/calibration.h`'s new
    `CalibratedScreenBoxContaining`: each calibrated screen's box is the
    smallest yaw/pitch rectangle spanning its four corners (every
    `CalibrationPoint` but the centre), widened by
    `Settings::other_screen_margin_degrees`, following #137's own design
    ("a screen is the box they span, away is outside every box plus a
    margin"). This is a different, more precise test than
    `src/screen_context.h`'s own `CoarseGazeFrom`, which keeps its
    existing uncalibrated side guess for the `context` JSON every
    reading carries (#61 item 11): that reader is unchanged by #141,
    since its own gaze snapshot has no notion of a look's duration or
    kind to report in the first place.
-   **Decision (provisional thresholds, like #137's own):**
    `Config::kDefaultLookingAwayAvertedMs` (6500) is the issue's own
    "back within about 5 to 8 seconds", its midpoint rounded to a whole
    second; `kDefaultLookingAwayOtherScreenMarginDegrees` (3.0) matches
    `CalibrationSettings::kDefaultMinSpreadDegrees`, the same margin
    #137's own spread check already uses for "far enough from this
    screen's own centre to be a real corner". Neither is tuned on a
    recording: #139's fixtures are not yet recorded, and no camera was
    opened to record them for this change. Like #137's own `eye_gaze.*`
    and `calibration.*` settings, neither is added to the macOS or Linux
    Settings window (`src/settings_document.h` is unchanged): both are
    config-file-only until a recording tunes them, the same provisional
    treatment #137 gave its own new settings.
-   **Decision (no face is read as fully away, from the very first
    frame):** `LookAwayDetector::Update` takes an `std::optional`
    composed gaze, not a bare `HeadRotation`; `nullopt` (no face) opens
    or continues a look exactly as being genuinely away does, so a face
    lost from the very first frame of a look still counts, and
    `LookAwayReader::OnNoFace` (still given no timestamp of its own,
    `FaceReader`'s own interface) feeds it the last timestamp `OnFace`
    read. `src/look_away_replay.h`'s own replay tool reads
    `gesture_no_face` lines the same way, for #139's eventual recordings.
-   **Decision (the frame that closes a look must not see the gap's own
    relearning):** `LookAwayReader::OnFace` now reads
    `tracker_.RestingPose()` before calling `tracker_.Update()`, not
    after (`src/look_away_replay.cc`'s own replay matches it). Found
    while fixing #141: `ExpressionTracker::Update`'s own gap (over two
    seconds since the last frame, #46) relearns the resting pose afresh,
    and `RestingPose()` then reads `nullopt` for a further three
    seconds (`kLearningMs`) -- which, read on the very same frame a long
    `kGone` look is trying to close, would discard it entirely
    (`LookAwayDetector::Update`'s own `!resting` branch), rather than
    reporting it. Reading the resting pose before this frame's own
    update lets that one frame still close the look against the pose it
    opened against; a `kGone` look whose return pose also differs from
    the old resting pose by more than `Settings::degrees`, straddling
    the whole three-second relearning window before it settles, can
    still lose its report -- a known, narrower residual limitation,
    not one this change tries to close.
-   **Decision (`Absorb` must report too, now that reporting waits for
    `Return`):** `specs/LookAway.tla`'s `Absorb` (the resting pose
    catching up to a gaze that never itself returned) used to transition
    quietly to `rest`, safe because `Report` had already fired earlier
    in #35's own model if it was going to. With reporting moved to
    `Return`, a quiet `Absorb` would have nothing left to fire it,
    silently dropping a look -- this issue's own instruction to check
    that it still can't. `ReturnByAbsorb` now carries the exact same
    reporting effect `ReturnByMove` does; `RestHasNothingPending` is the
    invariant that would catch a quiet `Absorb` reintroduced, and the
    mutation that reintroduces it is in `specs/README.md`'s own table.
-   **Decision (a `LookAwayReader` given to `MakeFaceReaders` is reused,
    like `ScreenContextReader`'s):** the daemon's own composition root
    (`tools/daemon/main.cc`) now keeps one `LookAwayReader` for the whole
    process, not one per camera session, mirroring `screen_context_reader_`
    (#61 item 11): its own `kGone` tracking, and the calibration profile
    `OnCalibrationFinished` swaps into it (`LookAwayReader::SetProfile`,
    mirroring `ScreenContextReporter::SetProfile`, #136, #137), would
    otherwise be lost every time the camera reopens. `MakeFaceReaders`
    still builds one fresh from `Config` when none is given, for
    `tools/camera_probe` and any test that does not care.

## 2026-11-22: Coarse gaze and calibration read eyes with head pose, over every screen, and apply at once, closing #135 and most of #137

-   **Decision (composition):** `src/eye_gaze.h`'s `ComposeGaze` reads
    `gaze = head + gain * (eye - eye_resting)` per axis, relative to the
    eyes' own resting level, the way `ExpressionTracker` already reads a
    blendshape relative to its resting level; beyond
    `eye_gaze.head_only_yaw_degrees` of head yaw, or with no eye signal
    yet, gaze is head pose alone. `src/screen_context.h`'s `CoarseGazeFrom`
    now takes this composed gaze, not head pose, and `NearestCalibratedScreen`
    compares a calibration profile's own saved points against it the same
    way.
-   **Decision (eye signal):** `EyeSignalFrom` reads `src/eyes_probe.h`'s own
    blendshape indices (#139): horizontal is the mean of
    `(eyeLookOutLeft - eyeLookInLeft)` and `(eyeLookInRight - eyeLookOutRight)`;
    vertical is the mean "down" score minus the mean "up" score; a blink
    (`eyeBlink` past `eye_gaze.blink_threshold`) is skipped outright, never
    folded into the signal or its resting level. `EyeGazeTracker`, owned
    by `ScreenContextReader` alongside its existing `ExpressionTracker`,
    folds kept samples into a median over the last `eye_gaze.median_ms`
    (damping frame noise, which doubles as a dwell on whatever class is
    read off the result) and learns the resting level the first
    `eye_gaze.resting_ms` of samples (and again after a gap of
    `eye_gaze.gap_ms`), the same relative-to-resting approach
    `ExpressionTracker` already takes, reused rather than reinvented.
-   **Decision (the gains step and weak-step refusal, closing most of
    #137 and all of #136):** a new step between the resting pose and the
    first screen's points, "fixate a dot, turn the head slowly, keeping
    your eyes on it": `src/eye_gaze.h`'s `EstimateGains` reads the
    least-squares slope of head over eye per axis (negated, since
    `gaze = head + gain * eye` must stay level as head varies while
    fixating), refusing the whole step when neither axis's own head range
    reaches `calibration.min_head_travel_degrees`, and leaving an axis
    under `calibration.min_axis_degrees` of its own range at the
    configured default gain rather than dividing by a range that barely
    moved. A screen's corner is refused the same way when its own
    composed gaze reads under `calibration.min_spread_degrees` from that
    screen's already-captured centre. Either refusal resets the step's own
    capture for a retry, with a plain reason, rather than #136's silent
    accept of a profile whose corners could not be told apart.
-   **Decision (every screen, #135):** both menus' "Calibrate…" window now
    passes `[NSScreen screens].count` /
    `QGuiApplication::screens().size()` to `CalibrationFlow::Start`,
    rather than #136's hardcoded one screen, moves itself onto the screen
    a step names (`moveToScreenForStep:`/`MoveToScreenForStep`, primary
    screen first then the rest, the same order
    `MacosScreenSource::Screens()` already enumerates in on a Mac), and
    names that screen -- its position relative to the main one, and its
    display name -- in the step's own prompt. The wizard ends with
    "Calibrated N screens.", read from the daemon's own `done` reply,
    which now carries `"screens":n` (`CalibrationController::Status::screens`,
    `CalibrationStatusJson`).
-   **Decision (apply at once, the rest of #136):** `CalibrationSession`
    takes an `on_finished` callback, called with the freshly saved profile
    from inside the same `Next` call that saves it; the daemon's
    composition root (`tools/daemon/main.cc`'s `Engine`) uses it to call
    `ScreenContextReporter::SetProfile`, guarded by a mutex of its own
    (`ScreenContextReporter` is otherwise a stateless, lock-free snapshot
    on every other call), so the very next reading already uses the new
    profile rather than only the daemon's next run.
-   **Decision (old profiles, format version 2):** `CalibrationProfile`
    gains `resting_eye` (`EyeSignal`) and `gains` (`EyeGains`) fields,
    `CalibrationFileStore`'s own format version bumping from 1 to 2 to
    match; a profile a version-1 daemon saved has neither, so loading it
    now fails as "unsupported calibration file version" -- already treated
    as no profile at all by every caller -- rather than needing a
    migration path for two fields no v1 profile ever measured.
-   **Rejected:** a second, independent hysteresis layer on top of
    `CoarseGazeFrom`'s own class (this_screen/other_screen/away/unknown)
    to give it a dwell of its own -- the eye signal's median filter
    already provides that inertia for the angle a class is read off, and
    `CoarseGazeFrom` is deliberately stateless and spec-free (`specs/
    README.md`'s own note on it), so a second stateful layer on top would
    duplicate rather than compose with the first; storing a calibration
    profile's per-point eye and head values separately rather than their
    already-composed gaze angle (the profile's own consumers,
    `NearestCalibratedScreen` and the spread check, only ever need the
    composed angle, and the gains used to compose it are already saved
    alongside).
-   **Evidence:** [#137](https://github.com/mattyv/facepalm/issues/137),
    [#135](https://github.com/mattyv/facepalm/issues/135); #137 stays open
    until its constants are checked against #139's recordings, named in a
    tuning note in `docs/local-app.md`.

## 2026-11-22: Publish the eye recordings trimmed to the lines the tests read

The eye fixtures for #139 are the user's own recordings, published with only
the eye lines, head pose, reactions and summary lines; the full expression
scores, the landmark coordinates and the gesture probabilities are dropped,
and the recording script drops them when it saves. Numbers only, never
images. Why: the repository is public, and the tests and tuning need no more.
Rejected: publishing the logs as recorded (more facial data than any test
reads), and keeping them local only (CI could not replay them). Decided by
the user on 2026-11-22. Evidence:
[#139](https://github.com/mattyv/facepalm/issues/139).

## 2026-11-22: eyes_narrowed is gated on the raw eyeLookDown and eyeBlink scores, not #137's composed gaze

-   **Decision (what gates it):** `ExpressionTracker`'s `eyes_narrowed`
    (`src/expressions.cc`, #140) is also gated on MediaPipe's own
    `eyeLookDown` (indices 11, 12) and `eyeBlink` (9, 10) blendshapes,
    straight from the `Scores` `Update` already receives: the higher of
    each pair, each against its own threshold. A downward glance, as at a
    keyboard, or a blink lifts `eyeSquint` the same way a real squint
    does, so without this gate a glance at the keyboard reads as
    confused. Scoped to `eyes_narrowed` alone: the other four expressions
    are read as usual while it is gated.
-   **Decision (read from the raw scores, not #137's gaze):** #137 is
    building a composed gaze reading in parallel on its own branch; this
    gate does not depend on it, reads the two blendshapes directly, and
    holds whether or not that composed reading ever lands.
-   **Decision (skip the state entirely, like the pose gate):** a gated
    frame skips `eyes_narrowed`'s own level, spread and hold update
    entirely, mirroring how the existing pose gate (#46) skips every
    state while the head is away: the glance or blink neither reports nor
    learns into the resting squint, and a hold that straddles it restarts
    once it ends, rather than carrying a partial hold across the gap.
-   **Decision (provisional thresholds):** `Config::kDefaultExpressionsEyeLookDownThreshold`
    (0.3) and `kDefaultExpressionsEyeBlinkThreshold` (0.5) are reasoned
    from MediaPipe's 0-to-1 blendshape scale, not measured: #139's
    fixtures (`test/fixtures/eyes/*.probe`) are not yet recorded, and no
    camera was opened to record them for this change. They are to be
    tuned once those fixtures exist, or, lacking those, against synthetic
    data replayed through `src/eyes_replay.h` (#147).
-   **Decision (no spec):** no TLA+ spec models `ExpressionTracker`; its
    gates are simple per-frame arithmetic with no lifecycle or
    concurrency of their own, so `CLAUDE.md`'s spec-first rule does not
    apply here, as it did not for the pose gate (#46) either.

## 2026-11-22: A window-count tracker decides when Command-Tab can reach the menu app (#146)

-   **Decision (where the counting lives):** `WindowActivationTracker`
    (`src/window_activation.h`), not booleans or an open-window count kept
    on `FacepalmApp` itself, counts how many of Settings, the camera
    preview and the calibration wizard are open and decides when
    `tools/menu/main.mm` must flip `NSApp`'s activation policy between
    `NSApplicationActivationPolicyAccessory` and `...Regular`: the one
    piece of logic in the fix, kept apart and unit-tested the way #64
    asked, with `main.mm` only wiring `WindowOpened()`/`WindowClosed()`
    calls at each of the three windows' open and close points and
    applying the `ActivationPolicyChange` it returns.
-   **Decision (no TLA+ spec):** the tracker is a plain counter with a
    floor at zero, run on Cocoa's single main thread with no concurrency
    or cross-process race for a spec to model, unlike `SingleInstance.tla`
    or `CalibrationMenu.tla`; it is tested the same way `src/look_away.h`'s
    pure computation is, with ordinary unit tests enumerating its cases.
-   **Decision (defensive, not failing, on an unmatched close):** a
    `WindowClosed()` with nothing open returns `kNone` rather than letting
    the count go negative, so a close notification this tracker was never
    told to expect an open for can never flip the policy back a second
    time; no such call is currently possible from `main.mm`'s own wiring,
    but the tracker does not rely on that staying true.
-   **Rejected:** giving each of the three windows its own boolean on
    `FacepalmApp` and summing them inline at each open and close point --
    the same decision, repeated three times in untested Objective-C++,
    which is exactly what #64 found wrong with `main.mm` already.

## 2026-11-22: The camera preview now shows the readings and the latest beat's feeling, closing #134

-   **Decision:** beside the frame, in the same private folder, under the same
    rules (0600 file, 0700 folder, written only while the preview streams,
    cleared when it stops): `UserPaths::preview_readings`, a plain text file
    holding the readings as they happen (the same summary
    `FeedClients::Deliver` already builds for the reaction feed, such as
    "Gesture: nod"), each held for two seconds so it can be read
    (`src/preview_readings.h`'s `PreviewReadings::kHoldMs`), and the latest
    beat's feeling (such as "Last read: confused (question)"), which never
    expires on its own. Both menus (`tools/menu/main.mm`, which adds a text
    field below the preview image; `tools/menu/main_linux.cc`'s `CameraPreview`,
    which adds a `QLabel` and its own `RefreshReadings` method so `Refresh`'s
    own cognitive complexity stays down) poll it the same way they already
    poll the frame.
-   **Why:** the preview, a diagnostic for watching Facepalm read you live
    (#61 item 15), showed only the frame; a person watching it had no way to
    see what Facepalm made of what it saw, moment to moment, or what the
    last beat was read as.
-   **Mechanism:** `PreviewReadings` holds only the text -- the reading (with
    its own hold, reset by a new one, cleared by an empty summary's own
    "nothing recognized" case, which changes nothing) and the feeling line --
    never deciding on its own whether the preview is streaming. Engine
    publishes it through `PreviewLease::Publish`, the very same gate
    `PreviewPublisher::OnFrame` already takes for the frame -- one mutex, one
    method, called twice over -- once a frame, alongside the frame's own
    publish, so a reading's hold expiring shows up even when nothing new has
    happened. The latest beat's feeling reaches it through a new optional
    `on_beat` callback on `ApprovedFeed`, fired with the `Feed::Interpretation`
    a `Beat` resolves with, so Engine never has to reach into `ReactionFeed`
    for it. `specs/PreviewLease.tla` covers the new file the way #131 covers
    the frame: `readings_exists`, published by the new `PublishReadings`
    action, shares `GuardPublishChecksStreaming` and `GuardStartClearsFrame`
    with the frame's own `frame_exists` -- the same method, the same mutex --
    and needs no `pending_publish` of its own, since nothing in
    `src/preview_readings.h` ever releases the lock between building the line
    and writing it. `NeverReadingsWithoutStreaming` holds; both existing
    mutations of the two shared guards now break it too.
-   **Rejected:** a second lease or mutex of the readings' own (the frame's
    already serializes every writer that needs to agree with it on whether
    the preview is streaming, so a second one would only risk disagreeing
    with the first); modeling the readings' own write with a `pending_publish`
    of its own (there is no encode-like delay between deciding what to write
    and writing it, unlike the frame, so there is no gap for another
    transition to land inside); expiring the latest beat's feeling the way a
    reading expires (the issue asks for the *latest* beat's feeling, not a
    momentary one).
-   **Evidence:** [#134](https://github.com/mattyv/facepalm/issues/134);
    Codex's draft on `fix/delivery-audit` (`5757ad1`, `97af5ac`) was read
    first, against a `PreviewLease` design (`Ticket`/`PreviewOwner`,
    `Publish(ticket, frame)`) that predates #114/#129's simpler
    `CameraOpened`/`CameraClosed`/`Start`/`Stop`/`Publish(write)` one now on
    `main`, so its own `PreviewLease` and `PreviewPublisher` changes do not
    apply; its ideas worth keeping -- a `PreviewInfo`-like summary already
    built once and reused, a short-lived "recent cue" hold, and a feeling
    line beside it -- are what this decision ports onto the design actually
    on `main`.

## 2026-11-22: camera_probe prints an eyes line, and a replay reader reads it back, for #139's fixtures

-   **Decision (the eyes line's shape):** `camera_probe`'s new `eyes` line
    (`FormatEyesLine`, `src/eyes_probe.h`) prints the eye-look, blink,
    squint and wide blendshapes (MediaPipe's indices 9 through 22, left eye
    then right, confirmed against `src/expressions.cc`'s own
    `kEyesNarrowed = {19, 20}`), then the iris centres (landmarks 468 and
    473) and the eye corners (33, 133, 362, 263), each as x, y and z,
    numbers only. It prints alongside `head_pose`, under the same
    `IsFiniteRotation` skip, since `GestureProbeReader::OnFace` already
    holds the rotation when it decides whether to print that line.
-   **Decision (fixture file shape):** `scripts/record_eye_fixtures.sh`
    saves each fixture as `camera_probe`'s whole log
    (`test/fixtures/eyes/<name>.probe`), not a line filtered down to
    `eyes` alone the way the three `.head_pose` fixtures were. A later
    issue's reader may want `head_pose` or `gesture_raw` from the same
    recording, and `ReplayEyes` (`src/eyes_replay.h`) already skips every
    line it does not read, so filtering at record time would only lose
    information for no test that needs it.
-   **Decision (eye corners, unnamed by side):** `EyesFrame`'s four corner
    fields are named by landmark number (`corner_33`, `corner_133`,
    `corner_362`, `corner_263`), not inner or outer, left or right: #139
    groups them as `(33/133, 362/263)` without saying which is which, and
    nothing else in this repository's code or comments settles it either.
    The iris centres keep `iris_left`/`iris_right` instead, since
    MediaPipe's own landmark numbering for the iris (468 left, 473 right)
    is widely and consistently documented.
-   **Decision (no eyes replay CLI tool, rejected):** unlike
    `gesture_replay`'s CLI, the eyes replay reader gets no
    `tools/eyes_replay` of its own in this change. Nothing yet prints from
    an `EyesFrame` worth running by hand: #140 through #145 have not yet
    written the detector whose output such a tool would show. Adding one
    now would be code no real use can reach, against this repository's
    rule on overengineering; one can follow once a detector exists to run.

## 2026-11-22: `ask_feeling`, `tell_feeling` and the feeling card now tell the agent the person's pick, not only Facepalm, closing #133

-   **Decision:** found in the live check on 2026-11-22 (#61 item 15),
    `ask_feeling` told the agent only "Feeling taken," so a pick the person
    made never reached the agent's own context: the agent kept treating
    Facepalm's tentative read as what stood for that beat. `ask_feeling`,
    `tell_feeling` and the new `get_feeling` now all return
    `{id, read, picked, feeling}` through one result builder,
    `mcp/feeling-result.js`: `read` is Facepalm's own interpretation,
    `picked` is the person's pick once there is one, and `feeling` is
    `picked ?? read` -- the pick replaces the read for that beat; with no
    pick, the read stands, and nothing waits on one. Every result stays
    advisory, never an answer or an approval, the card's thank-you line
    unchanged. `mcp/session.js`'s `knownInterpretations` now keeps a
    `{read, picked}` record per id, through two new methods,
    `interpretation()` and `recordPick()`, rather than a bare `Set` of
    known ids; `specs/McpSocket.tla`'s `Learn`, `Tell` and
    `GuardForgetsIdsOnLinkDown` model only whether an id is known at all,
    never what is stored alongside it, so they needed no change, read and
    confirmed in the same branch (`specs/README.md`'s `McpSocket` section
    now says so). A refusal Facepalm itself gives still carries the pick,
    since the person did pick regardless of whether it tunes; a call that
    never reaches a definite yes or no from Facepalm (offline, unapproved,
    an unrecognized id, or any other native failure) leaves the read
    standing instead, its own text unchanged but for "Facepalm's read
    stands" appended. `mcp/widget/feeling-view.js`'s card now passes a
    successful `tell_feeling`'s own result to the host through
    `app.updateModelContext()`, gated on `getHostCapabilities()?.
    updateModelContext`, with a rejection swallowed, for a host that does
    not otherwise surface a tool call's own result back to the agent. The
    one open risk, carried forward rather than resolved here: whether
    Claude Desktop actually surfaces `updateModelContext` needs a live
    check, not exercised by any test here.

## 2026-11-22: A publish in flight can no longer outlive the preview, and the MCP adapter now agrees with its own spec, closing #129

-   **Decision (preview frame, #123's own review):** `specs/PreviewLease.tla`
    now models the frame file itself, not only the lease that gates it:
    `frame_exists` and `pending_publish` split
    `PreviewPublisher::OnFrame`'s own check-encode-publish into the
    decision (`Frame`, which now commits to a pending publish rather than
    assuming one) and the publish itself (`Publish`, its own action, so
    `CameraOpens`, `CameraCloses`, `StopPreview` and a later `Frame`'s own
    revoke can land in between the way the real control queue can during a
    real encode). The finding was real: a camera close landing mid-encode
    already cleared the frame through `PreviewLease`'s own
    `streaming_changed` hook, but the same call's publish, once the encode
    finished, still wrote it back regardless. The fix is
    `PreviewLease::Publish`, a new method that takes `mutex_` itself and
    re-checks `streaming_` at the moment of the write, the same lock every
    transition to not-streaming already takes before its own clear:
    `src/preview_publisher.cc`'s `OnFrame` now calls it instead of
    `publish_` directly. `GuardPublishChecksStreaming` is that re-check;
    `GuardStartClearsFrame` is `Engine::Start` now also removing any frame
    a previous crash left behind, before the daemon can stream again, the
    same way `CameraStatus!Restart` already republishes the camera
    indicator off. Both prove `NeverFrameWithoutStreaming` bites.
-   **Also decided:** three nits from the same review, fixed in the same
    branch. `tools/daemon/main.cc`'s `WriteFileAtomically` now `fchmod()`s
    the temporary file explicitly, every call: `open()`'s own mode
    argument only applies the first time `O_CREAT` actually creates it, so
    an existing `.tmp` left by an earlier failed write kept whatever mode
    it already had through `O_TRUNC`. `tools/menu/main.mm` and
    `tools/menu/main_linux.cc`'s own comments on `stopPreview`/`Stop`, which
    still called a frame left on disk "harmless," are corrected to say what
    `PreviewLease`'s own hook has done since #114: remove it, synchronously,
    the next time the daemon notices the request is gone.
-   **Decision (MCP reconnect, also #61 item 8's review, same cycle):**
    `specs/McpSocket.tla`'s `LinkDown` is corrected on two points found by
    matching it against `mcp/session.js`'s own `disconnect()`: `backoff' =
    0` let a reconnect redial immediately, which `disconnect()` never
    allows (it always bumps backoff, by the time `LinkDown` can even fire,
    always to its own minimum, so the fix is a plain `backoff' = 1`, not a
    new guard -- there was only one correct value); and `remoteWaiting' =
    FALSE` asserted the remote side's own wait ends the instant the local
    link drops, which nothing in `session.js` has any way to tell it, so
    `LinkDown` now leaves `remoteWaiting` unchanged, the same as `Cancel`
    already does for the same reason.
-   **Also decided:** `mcp/session.js`'s two 'error'/'close' handlers,
    registered once per `dial()`, now check the socket they were bound to
    is still `this.socket` before calling `disconnect()`: a socket already
    replaced by a later reconnect does not retire its own events
    synchronously, so a stale one landing after the replacement could tear
    the replacement down too. An unapproved hello (`src/service.cc`'s own
    `"client not approved"`) now rejects with a new `UnapprovedError`,
    exported from `session.js` the same way `OfflineError` is, so
    `mcp/server.js` and `mcp/feeling-elicitation.js` can say "Facepalm
    hasn't approved this client" rather than "Facepalm is off," which told
    the person to do the wrong thing when Facepalm was already on.
-   **Decision (interpretation ids, the same review):** a late
    `tell_feeling`/`ask_feeling` for an interpretation given before a lost
    link could train a different beat after a reconnect, since
    `src/reaction_feed.cc`'s own `next_interpretation_` restarts at 1 on
    every daemon run and the daemon has no way to tell the two apart by the
    id alone. Of the two fixes the review offered, the adapter forgetting
    ids on its own link loss is the one taken, over giving ids a daemon run
    id: it needs no protocol or daemon change, stays entirely in
    `mcp/session.js`, and the two sides of getting it wrong are not even:
    refusing a feeling that would have been fine is a retry away; taking
    one for the wrong beat is not undoable at all. `SocketSession` gets
    `noteInterpretation()` (called by `wait_for_reaction` for each
    interpretation a delivery gives the caller) and `knowsInterpretation()`
    (checked by `tell_feeling` and `ask_feeling` before ever sending `feel
    <id> ...`), backed by a set `disconnect()` now clears on every link
    lost. `specs/McpSocket.tla` models this with `knownId`,
    `generation`, `learnedGeneration` and a ghost `stale`: `generation` is
    bumped by every real `LinkDown` regardless of the guard, so a mutation
    that stops `LinkDown` from forgetting (`GuardForgetsIdsOnLinkDown =
    FALSE`) is caught by the ghost's own bookkeeping, not by the thing it
    is meant to guard, the same lesson `specs/PreviewLease.tla`'s own
    `menu_alive` already taught for #114.
-   **Rejected:** ids carrying a daemon run id, the review's other option --
    would touch `src/protocol.{h,cc}`, `src/reaction_feed.{h,cc}` and every
    caller of `feel <id> <feeling>`, for a problem the adapter's own,
    purely local, forgetting already closes without any of that.
-   **TLC:** `PreviewLease.cfg`, now 37 distinct states, holds (adds
    `NeverFrameWithoutStreaming` to the four properties already there).
    `McpSocket.cfg`, now 65 distinct states, holds (adds `NoStaleTell`).
    Full suite, `specs/check.sh`: all 20 configs pass.
-   **Evidence:** [#129](https://github.com/mattyv/facepalm/issues/129),
    [#61 item 8](https://github.com/mattyv/facepalm/issues/61),
    [#114](https://github.com/mattyv/facepalm/issues/114),
    [#117](https://github.com/mattyv/facepalm/issues/117).

## 2026-11-22: The wizard waits for the camera, not the daemon, closing #128

-   **Decision:** of the two ways #128's first finding could be fixed --
    the wizard waiting for the camera to come up, or the daemon starting
    calibration itself once it does -- `CalibrationFlow::Start` is the one
    that waits: a `calibrate start` refused only for
    `kCameraUnavailableError` is retried, `kCameraWaitInterval` apart, up
    to `kMaxCameraWaits` times, before failing. The daemon's own
    synchronous, one-request-one-reply protocol (`src/service.cc`) has no
    way to hold a request open across an asynchronous camera open and
    answer it later; the wizard already blocks its caller's thread for
    the length of one `Start` call (`tools/menu/main.mm`,
    `main_linux.cc`'s button handlers), so stretching that one call across
    a few bounded retries costs nothing structural, where teaching the
    daemon to queue a request would.
-   **Spec:** `specs/CalibrationMenu.tla` gains the retry as a new
    `"opening"` state (`OpenWaitsForCamera`, `RetrySucceeds`, `RetryWaits`,
    `RetryGivesUp`, against an environment `CameraComesUp` on its own
    schedule) and a liveness property, `CameraWaitResolves`, whose
    mutation (no fairness on the three retry actions) shows a flow stuck
    in `"opening"` forever. `"opening"` is never observed through
    `CalibrationFlow::CurrentView()`: the whole retry loop runs inside one
    call to `Start`, the same reasoning that already makes `ClosingCompletes`
    hold without needing its own fairness assumption to be a real one.
-   **The second finding (the orphaned session) has two separate causes,
    fixed separately, each with its own guard:** `ApplyReply` treating any
    refused `calibrate next` as simply failed, with no cancel sent, is
    `specs/CalibrationMenu.tla`'s new `NextRefused` action and
    `GuardCancelOnRefusal`, proved against a new invariant,
    `SessionEndsWithFlow` (`state \in {failed, done} => ~sessionLive`);
    `CalibrationFlow::Next` now sends `calibrate cancel` and reads its
    reply before reporting the original failure, regardless of what the
    refusal said, since the alternative -- parsing the daemon's error text
    to tell a session still running apart from one already gone -- is
    fragile where "always try" costs nothing. The daemon never tying the
    session to its connection, so a menu crash orphans it the same way, is
    instead `specs/Service.tla`'s new `calibOwner`, `CalibrateStart`,
    `CalibrateEnds` and `Closed`'s own `GuardCalibCancelOnClose`, against a
    new invariant, `NoDanglingCalibOwner`: `Service::CalibrateReply` ties a
    successful `calibrate start` to its connection, and `LeaveCalibration`,
    called from `Serve` as that connection's thread ends, cancels the
    session it still owns, if any, before its slot can be reused.
-   **The third finding (the data race) is fixed with one mutex,** not a
    new spec: `specs/Calibration.tla`'s actions were already modeled as
    atomic steps: `CalibrationSession` now guards `Start`, `Next`,
    `Cancel`, `Update`, `OnCameraLost` and `OnCameraUp` alike with one
    `mutex_`, taken for each one's whole body, which is what makes the
    model's atomicity true of the code rather than merely assumed; a
    two-thread stress test exercises it under ThreadSanitizer.
-   **The fourth finding (macOS SIGPIPE and the unbounded `recv`)** is
    fixed in `tools/posix_calibrate_client_io.h` the way `src/unix_socket.cc`
    already fixes it on the daemon's own side: `SO_NOSIGPIPE` on the
    socket (macOS) or `MSG_NOSIGNAL` on each `send` (Linux), and
    `SO_RCVTIMEO` so a daemon that stops answering, rather than closing
    the socket, ends `ReadLine` the same way a hang-up does, instead of
    blocking the menu's UI thread forever. Left untested by this repo's
    own convention for this file (untested POSIX glue, the same as
    `tools/calibrate_cli/main.cc`'s own use of it).

## 2026-11-22: A menu entry for calibration, with its own small spec, closing #118

-   **Decision:** `tools/menu/main.mm` and `tools/menu/main_linux.cc` each
    gain a **Calibrate…** entry that opens a small window: a plain
    explanation that calibration is optional, then the session's steps
    (the resting pose, each screen's centre and four corners) with Next and
    Cancel, following `src/calibrate_cli.h`'s protocol and item 12 of #61's
    `specs/Calibration.tla` session. The decision logic that drives it --
    what to send, how to read the daemon's reply, what the window shows --
    lives in `src/calibration_flow.h`'s `CalibrationFlow`, a new unit
    (`test/calibration_flow_test.cc`, 100 percent, mocking
    `CalibrateClientIo`) kept out of `main.mm`/`main_linux.cc` (#64): both
    menus' own additions are thin AppKit/Qt classes
    (`CalibrationWindowController`, `CalibrationWindow`), each its own file,
    the way `CameraPreview` is kept apart from `main_linux.cc`'s `main()`.
-   **Spec:** `CalibrationFlow` has a lifecycle `specs/Calibration.tla` does
    not model -- it is the *client's* own view, which only learns the
    daemon's session has ended when its own next request answers that way,
    and the window closing has to do something about a session still
    running before the socket goes away. A small new spec,
    `specs/CalibrationMenu.tla`, models it: `CalibrationMenu.cfg` holds (7
    states), and each of its two guards was proved to bite --
    `GuardFailOnDisconnect = FALSE` breaks `NoStalePrompt` (the window kept
    showing a step once it knew, from a failed or hung-up `calibrate next`,
    that the session was gone), and `GuardCancelOnClose = FALSE` breaks
    `NoOrphanOnClose` (the window's own close button tore it down without
    ever sending `calibrate cancel`, leaving the daemon's session running
    with nobody left to answer for it) -- plus a liveness property,
    `ClosingCompletes`, whose mutation (no fairness on completing a cancel)
    shows a flow stuck forever in `"closing"`. `CalibrationFlow::Cancel`
    and `WindowClosed` are the same call (`WindowClosed` is `Cancel` under
    its own name, so main.mm/main_linux.cc's call sites show which
    happened), which is what makes `NoOrphanOnClose` hold in the code with
    one guard rather than two call sites to keep in sync.
-   **Socket client:** the menu had none before this (`calibrate_cli`'s own
    `PosixCalibrateClientIo` was the only one, specific to that one tool).
    It is pulled out to `tools/posix_calibrate_client_io.h`, shared by
    `calibrate_cli` and the macOS menu; the Linux tray gets its own
    `QtCalibrateClientIo` (`tools/menu/calibration_window_linux.cc`), a
    `QLocalSocket` behind the same `CalibrateClientIo` interface. Both are
    untested POSIX/Qt glue by this repo's own convention (the same as
    `tools/gesture_replay/main.cc`'s and `QtSingleInstanceIo`'s).
-   **Approval:** the menu approves the already-existing `calibrate` client
    name (`ClientApprovals::Approve`, the same mechanism `SwitchService`
    uses to approve its own client for Turn On, #93) rather than a `menu`
    client of its own, since `calibrate_cli` already established that name
    and nothing distinguishes one caller of this protocol from another.
    Approval happens the first time **Calibrate…** is chosen, not when the
    menu starts, so running the tray or the menu bar app never approves a
    client the person has not asked to use yet.
-   **Rejected:** a screen-count picker in the wizard. `calibrate start`
    takes an optional screen count, but the window always asks for one
    (`CalibrationFlow::Start(1)`), calibrate_cli's own default when none is
    given; nothing in #118 asked for a picker, and multi-monitor selection
    is its own small design, better left for whoever actually needs it to
    file.
-   **Evidence:** #118's own comments and #96's and #119's PR bodies, read
    before designing this; `specs/CalibrationMenu.tla`'s mutations, run
    locally with TLC 1.7.4 (`CalibrationMenu.cfg`: 7 distinct states, holds,
    each guard's mutation produces the counterexample named above);
    `calibration_flow_test` (25 cases) and the full `ctest` suite (782
    tests) passing with every unit, including `src/calibration_flow.cc`,
    at its coverage floor; `clang-tidy --warnings-as-errors=*` clean on
    every touched file.

## 2026-11-22: FuzzTest alone, fuzzing the parsers and property-testing the rules, closes #120

-   **Decision:** Google FuzzTest, and no other property-testing library
    (RapidCheck was considered and rejected): FuzzTest's domains cover
    property-based testing with shrinking on their own, and it keeps one
    dependency, in GoogleTest's own style, rather than two different ones
    doing overlapping jobs. `CMakeLists.txt` fetches it with `FetchContent`
    behind a new `FACEPALM_FUZZTEST` option (default off), pinned past the
    `2026-06-29` release to commit `39e5d587af63` (2026-10-21; no tagged
    release carries it yet — see the real bug below) by a SHA-256 checksum
    on its source archive, not a bare `GIT_TAG` (which carries no checksum
    of its own). Every fuzz or property test is a GoogleTest executable
    FuzzTest also drives, `test/fuzz/<name>_fuzz_test.cc` built as
    `<name>_fuzz`, registered through `facepalm_fuzz_test` in
    `CMakeLists.txt`.
-   **The fuzz targets, in #120's order:** `src/protocol.cc`'s `Parse`
    (the hello line, reaching unauthenticated local processes before any
    approval); `src/config.cc`'s `ParseConfig` and
    `src/settings_document.cc`'s `UpdateSettingsDocument`; the feelings
    counts file, through `src/tuning.cc`'s `Tuning::Parse` (the pure entry
    point behind `src/feeling_store.cc`'s own file I/O); `src/frame_times.cc`'s
    `FrameTimes`; the calibration profile, through
    `src/calibration_store.cc`'s `ParseCalibrationProfile`; and the
    `cameras` listing the Settings windows parse, through a new pure
    function extracted for it, `ParseCamerasListing` in new files
    `src/cameras_listing.{h,cc}`, with its own 100 percent unit test
    (`test/cameras_listing_test.cc`): `tools/menu/main.mm` and
    `tools/menu/main_linux.cc` each parsed facepalm-daemon's tab-separated
    `cameras` stdout by hand, with no pure entry point of their own; both
    now call the extracted function instead of parsing it twice.
-   **The property tests, each following #120's own wording:** `Interpret`
    (`src/feelings.cc`) is independent of cue order, cues of different
    feelings give unknown, and an incomplete window always gives unknown;
    `Tuning::Read` (`src/tuning.cc`) never replaces the generic read on
    fewer than three agreeing picks (`Tuning::kLeastPicks` is 2, but the
    held-out check `Read` runs needs one of those picks spare, so a
    feeling needs 3 before it can out-vote the generic read — found while
    writing the test, confirmed by reading `Tuning::Read`, not a bug),
    and a reset restores it; the frame-rate choice (#43) always lies
    within one of the camera's own supported ranges, through
    `NearestSupportedRate`, exposed from `src/session_camera.cc`'s
    anonymous namespace into `src/session_camera.h` so the test can call
    it directly; the settings document keeps every unrelated line and
    comment; a finite rotation gives finite angles
    (`src/head_pose.cc`'s `HeadRotationFrom`); and protocol's hello line
    round-trips through `Parse`.
-   **Not fuzzed as asked, with a reason:** the frame-rate and head-pose
    properties and `src/feelings.cc`'s `Interpret` are pure, already-public
    functions a property test calls directly, not new fuzz targets of
    their own; #120 lists them under property tests, not the six fuzz
    targets, and they need no file format fuzzed, only arguments.
-   **How fuzz targets run:** GoogleTest's own brief, uninstrumented
    replay (a handful of random inputs, around a second) in every job that
    builds with `FACEPALM_FUZZTEST=ON`, including the existing `macos`
    job's coverage step; real, continuous, coverage-guided fuzzing only
    under `-DFUZZTEST_FUZZING_MODE=on`, which needs Clang's sanitizer
    coverage instrumentation, in the new Linux-only `fuzzing` CI job,
    under AddressSanitizer and UndefinedBehaviorSanitizer, each target run
    with `--fuzz_for=45s`. Verified locally in GoogleTest's brief mode only
    (macOS, AppleClang): the task notes FuzzTest's fuzzing mode may not
    work on macOS, so the continuous mode is verified by the Linux job
    alone.
-   **Cost on the `macos` job:** measured locally (macOS, AppleClang,
    `-j4`): a cold `bash scripts/coverage.sh` is about 59 seconds; with
    `-DFACEPALM_FUZZTEST=ON` added, about 3 minutes 4 seconds, a FuzzTest,
    Abseil, RE2 and ANTLR build away from the floor. That crossed the "a
    few minutes" line, so both this job and the new `fuzzing` job cache
    `build-coverage/_deps` and `build-fuzzing/_deps`, keyed on
    `CMakeLists.txt` (which holds the pin), the way
    `scripts/fetch_mediapipe_c.sh`'s own fetch already is: a warm run only
    compiles facepalm's own sources and the fuzz targets, not FuzzTest's
    dependency tree over again.
-   **A real bug the new `fuzzing` CI job found, on its first real run,
    in FuzzTest itself, not facepalm's code:** every target crashed
    `--fuzz_for`'s first input with an AddressSanitizer stack-overflow, a
    few thousand frames deep, entirely inside
    `fuzztest::internal::ExecutionCoverage::IsTracing` and
    `std::atomic<bool>::load`, never reaching any facepalm frame. Read:
    `IsTracing`'s `std::atomic<bool>::load(std::memory_order_relaxed)`
    (added 2026-01-29 to fix a ThreadSanitizer warning) compiles, under
    `-fsanitize-coverage=trace-cmp`, to a libstdc++ branch on the memory
    order that is itself instrumented, calling back into `IsTracing`
    through `TraceCmp` — the exact bootstrapping problem sanitizer
    coverage runtimes exist to avoid in their own code. GoogleTest's own
    brief replay mode (every other job) never reaches
    `FUZZTEST_FUZZING_MODE`'s instrumented build, so this was invisible
    until the `fuzzing` job's own first real run; verified by reading
    `fuzztest/internal/coverage.h` at the pinned commit against the
    crash's stack trace. Fixed by moving the pin to `39e5d587af63`
    (2026-10-21), where FuzzTest replaces that `std::atomic<bool>` with
    the raw `__atomic_load_n`/`__atomic_store_n` builtins, which compile
    to no instrumentable branch; confirmed against that commit's own diff.
    No facepalm regression test applies — the bug is in FuzzTest's own
    source, fixed by moving to the commit that already fixes it there.
-   **A real bug in facepalm's own code, found once the fuzzing job got
    past the problem above:** `SessionCameraFuzz.ChosenRateWithinSupportedRange`
    found a genuine UndefinedBehaviorSanitizer signed-integer-overflow in
    `src/session_camera.cc`'s `NearestSupportedRate`, on its very next
    fuzzed input after the fix above: `Nearest`'s candidate-to-`wanted`
    distance, `std::abs(candidate - wanted)`, overflows `int` once
    `wanted` is far enough from a candidate (its counterexample: `wanted
    = -2147483647`, a candidate of `60`). GoogleTest's own brief replay
    mode never generates so extreme a value, so this was invisible until
    the fuzzing job's own run. Fixed by widening the subtraction to
    `int64_t` before taking the absolute value (a new `Distance`
    function), which cannot overflow for any `int` pair. Verified the bug
    and the fix directly: a standalone `-fsanitize=undefined
    -fno-sanitize-recover=all` build of the old `std::abs(candidate -
    wanted)` aborts on the counterexample's inputs; the widened version
    does not, and computes the same (correct) distance. Regression test:
    `SessionCameraFrameRateTest.DoesNotOverflowOnARateNearIntMin`
    (`test/session_camera_test.cc`), the exact counterexample.
-   **Two build-integration problems found and fixed, not FuzzTest's own
    bugs:** Abseil, FuzzTest's own dependency, defines a raw CMake target
    literally named `config`, colliding with facepalm's own `config` unit;
    `facepalm_unit` gained a `TARGET` option, used once, to build that
    unit's library as `facepalm_config` while its test executable stays
    `config_test` (`CMakeLists.txt`, every `target_link_libraries` that
    named `config` updated to `facepalm_config`). FuzzTest's own
    `CMakeLists.txt` also fetches GoogleTest itself unless `GTest::gtest`
    already names a target when it configures; moving this project's own
    `find_package(GTest REQUIRED)` ahead of FuzzTest's `FetchContent`
    (previously below it) makes every test in the project, not only the
    fuzz tests, link the same GoogleTest FuzzTest's own fetch would
    otherwise silently replace it with — the fetched copy's Abseil
    integration made an existing GoogleTest printer emit a
    deprecated-declaration warning `-Werror` turned fatal, across every
    test executable in the build, not only the new ones.
-   **Coverage:** fuzz and property test executables are exempt from the
    per-file coverage gate the way the fixture tool is: `CLAUDE.md`'s Tests
    section and this entry record it. Named `<name>_fuzz`, not
    `<name>_test`, so `scripts/coverage.sh`'s own `*_test` glob, which
    measures each unit's floor from its own tests alone, already skips
    them without a change to that script, the same way it already skips
    `menu_store_checks`, `preview_jpeg_checks` and
    `linux_tray_integration`. No existing floor in `coverage_thresholds.json`
    was lowered or removed; `src/cameras_listing.cc`'s new floor is 100
    percent, lines, branches and functions, met by its own unit test.
-   **Evidence:** [#120](https://github.com/mattyv/facepalm/issues/120),
    [#61](https://github.com/mattyv/facepalm/issues/61),
    [#43](https://github.com/mattyv/facepalm/issues/43).

## 2026-11-22: AvFoundationSession's device-change observers wait for StartWatching too (#121)

-   **Decision:** `AvFoundationSession`'s constructor no longer registers
    the `AVCaptureDeviceWasConnectedNotification`/
    `WasDisconnectedNotification` observers itself; a new `StartWatching`
    does, called from `Engine::Start()` in `tools/daemon/main.cc`
    alongside the Linux `StartWatching()` call #115 already added, once
    every member those callbacks reach, `camera_` among them, is
    constructed. Before, those two observers, posted with `object:nil`,
    could fire for any device as soon as the constructor ran, and the
    blocks they ran, `on_connected`/`on_disconnected`, close over
    `camera_` in `Engine`, which `capture_` (the `AvFoundationSession`) is
    declared, and so constructed, before. The runtime-error observer is
    unaffected and stays in the constructor: it is scoped to this
    session's own `AVCaptureSession` (`object:`), which cannot run before
    `Start`, well after construction.
-   **Also decided:** `avfoundation_session_checks`
    (`test/avfoundation_session_check.mm`) is a new real-framework check,
    following `preview_jpeg_checks`' and `menu_store_checks`' own pattern,
    since `AvFoundationSession` has no seam to mock `NSNotificationCenter`
    behind, and posting a notification needs no camera, open or
    otherwise. It has no coverage floor, the same as those two. The
    `format-check` target's file glob in `CMakeLists.txt` was missing
    `test/*.mm`, so it never checked those two existing files either;
    fixed in the same change, found while adding a third.
-   **Spec:** no change to `specs/CameraLifecycle.tla`: the construction
    order is not part of the device lifecycle it models, the same call
    #115 made for the identical hazard on Linux.
-   **Evidence:** [#121](https://github.com/mattyv/facepalm/issues/121),
    filed from reading #122's Linux fix for #115, the same hazard's
    macOS analog.

## 2026-11-22: A preview request must be fresh, and must be a menu's own, closing #114

-   **Decision:** `specs/PreviewLease.tla` now also models the request
    the menu makes (`requested`) as needing to be fresh
    (`age <= StaleAge`), not merely present, and a menu that is gone
    (`menu_alive = FALSE`, a crash, a quit that skipped cleanup, or a
    session logging out) as never able to refresh one again. The code
    follows: `src/preview_request.cc`'s `IsFreshPreviewRequest` is the
    `requested` callback `tools/daemon/main.cc` now gives
    `PreviewPublisher` in place of the plain file-existence check #114
    found insufficient, so a request a dead menu left behind is treated
    as gone once its last touch is more than `kPreviewRequestStaleMs`
    (2 s) old; the menu's own 0.5 s refresh timer
    (`tools/menu/main.mm`'s `refreshPreviewFrame`,
    `tools/menu/main_linux.cc`'s `CameraPreview::Refresh`) now also
    re-touches the request file on every tick, not only the first write,
    which is what keeps a live menu's own request from ever reaching
    that age; and both menus remove the request file outright at launch
    and on a clean quit (`applicationWillTerminate:`,
    `QCoreApplication::aboutToQuit`), rather than leaving even a clean
    exit to the freshness check's own grace period.
-   **Also decided:** three findings from the same review
    (#61 item 8's Fable review, #114) are fixed in the same branch,
    small enough not to need issues of their own:
    -   The frame is written 0600
        (`tools/daemon/main.cc`'s `WriteFileAtomically`, now POSIX
        `open()` with an explicit mode rather than an `ofstream`, whose
        own default leaks the process umask) and removed once streaming
        stops, through `PreviewLease`'s own `streaming_changed` hook
        (`src/preview_lease.h`), a no-op until now.
    -   The state folder is put back to 0700 even when it was already
        there: `src/client_approvals.cc`'s `MakePrivateDirectory` used to
        accept an existing directory of any mode, and
        `tools/menu/main_linux.cc`'s own `QDir().mkpath()` calls relied
        on the process umask the same way; both now chmod the directory
        they are asked to secure, whether they made it or found it.
    -   Neither menu opens a preview window, or starts its timer, with
        the camera off: both now check the camera-in-use file first
        (the same one their own indicator reads) and show a message
        instead, closing the gap where the window, and
        `tools/menu/main.mm`'s own timer, would otherwise flash into
        existence only to close themselves again on their first poll.
-   **Also decided:** `src/preview_lease.cc`'s `camera_session_` and
    `started_session_`, the same review's fourth finding, are removed
    rather than given a real read somewhere to justify keeping them:
    `CameraOpened` and `CameraClosed` already force `streaming` off
    directly, on every transition, so no grant from before either one
    ever has a session number left to be stale against by the time a
    later `Start` could compare one. `specs/PreviewLease.tla` drops the
    counters the same way, and `NeverStreamsWithoutAFreshStart` -- the
    review's fifth finding, a property with no mutation row -- goes with
    them, its concern folded into `NeverStreamsOnAStaleRequest`, which
    does have one.
-   **Why:** the review (#61 item 8, #114) found the daemon kept
    encoding and writing frames from a request file nothing had removed,
    across the menu quitting, crashing, or its session logging out, with
    no window left to show them; and, separately, that `PreviewLease`'s
    session bookkeeping was dead weight the fix should not carry forward
    unexamined.
-   **Rejected:** adding a real grant-level session check to make
    `camera_session_`/`started_session_` "real", the review's other
    offered option: the only readings or assignments of
    `started_session_` were in service of a comparison that `CameraOpened`
    and `CameraClosed`'s own direct reset already made redundant, so
    inventing one would have protected against nothing a design that
    never had it was not already safe from.
-   **Evidence:** [#114](https://github.com/mattyv/facepalm/issues/114),
    [#61 item 8](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-22: The MCP session reconnects, with a bounded backoff, instead of dying (#117)

-   **Decision:** `mcp/session.js`'s `SocketSession` no longer treats a lost
    link as terminal: a new `disconnect()` tears down the socket without
    ending the session, and a later request reconnects once this
    session's own backoff (1 s, doubling to a 30 s cap, reset on a
    successful connection) has run out. Closing the session for good
    (`close()`), and cancelling a request already in flight, are still
    terminal, exactly as before: a new `OfflineError`, exported from
    `session.js`, is what `request()` rejects with for every other
    reason a request did not reach the wire, so `mcp/server.js` and
    `mcp/feeling-elicitation.js` can tell "Facepalm is off; turn it on
    from its menu" apart from every other failure without matching error
    text across files. That phrase, and the session's own retrying,
    replace the old generic failure message that told the person to
    restart the MCP server, which no longer applies.
-   **Also decided:** `mcp/main.js` no longer exits when its token file is
    simply not there yet (`ENOENT`): it starts anyway, and
    `SocketSession`'s own token, given as a function, reads that path
    fresh on every connection attempt, so approving this adapter later,
    from the daemon's menu, needs no restart. Every other way reading the
    token file can fail (bad permissions, a symlink, the wrong length or
    format) still exits immediately, unchanged: those are
    misconfigurations retrying cannot fix.
-   **Also decided:** `mcp/server.js`'s zod `delivery` schema gets a
    `context` key, with the same shape `src/screen_context_reporter.cc`'s
    `Json` sends (active_app, window_title only when the daemon has one to
    report, mouse, gaze, calibrated_screen), so it is no longer silently
    dropped before an MCP client ever sees it. #36's design already has
    both channels (the native service and MCP) report screen context;
    this was the one case where zod, parsing what crosses that boundary,
    dropped a field the daemon already sent.
-   **Spec:** `specs/McpSocket.tla` is changed, not written fresh, to add
    `link`, `backoff` and the reconnect actions, keeping `state`'s own
    terminal "closed" exactly as it was. `NoSendAfterClose` and
    `OneRequest` still hold; a new `BackoffBounded` and a new liveness
    property, `LinkRecovers` (something must happen: a lost link always
    comes back, or the session closes first), both hold too, each proved
    to bite by a guard mutation or by removing fairness.
-   **Evidence:** [#117](https://github.com/mattyv/facepalm/issues/117),
    [#93](https://github.com/mattyv/facepalm/issues/93),
    [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-22: Calibration is wired to the daemon; the menu entry is its own issue

-   **Decision:** `tools/daemon/main.cc`'s `Engine` now gives `Service` a
    real `CalibrationController`, adds a `CalibrationReader` to every
    camera session's readers, and forwards the camera's own lifecycle to
    it, through a new tested class, `src/daemon_calibration.h`'s
    `DaemonCalibration` (#116, #64: the nullptr-check kind of decision
    stays out of `main.cc`, in a source with its own test file).
    `Engine::Open` calls `OnCameraUp` exactly when the frame pipeline's
    model is created (the camera is really open by then), `Engine::Close`
    calls `OnCameraLost` before it is destroyed, and `Engine::Start` calls
    `OnCameraLost` once more at startup, before either has run: without
    it, `CalibrationSession`'s own default of an up camera (right for
    #96's tests, which gave it no camera signal at all) would let a
    `calibrate start` through before any client has ever subscribed.
-   **Also decided:** the menu gets no calibration entry in this fix. The
    daemon side (`Service`, `MakeFaceReaders`, the camera's lifecycle) is
    what #116 names; a stepped wizard in both platforms' Settings windows,
    with a socket client the menu does not have today, is real UI work
    with a lifecycle of its own to track, not a composition-root wiring
    bug. Filed as [#118](https://github.com/mattyv/facepalm/issues/118).
-   **No new spec:** `specs/Calibration.tla` already models `CameraLost`
    and `CameraBack`; this change implements the actions it already
    specifies, in the composition root that was the one piece missing, not
    a new piece of lifecycle.
-   **Evidence:** [#116](https://github.com/mattyv/facepalm/issues/116),
    [#64](https://github.com/mattyv/facepalm/issues/64),
    [#96](https://github.com/mattyv/facepalm/issues/96),
    [#92](https://github.com/mattyv/facepalm/issues/92).

## 2026-11-22: A service hang-up leaves the feed once, from whoever gets there first (#115)

-   **Decision:** `Service::Serve`'s own end-of-connection cleanup, the
    watcher's `Service!Noticed` on a hang-up mid-wait, and `Stop`'s
    `BeginStop` can each reach the point of disconnecting a connection's
    name from the feed; only the first of them now does, through a new
    `Service::Leave(Connection&, name)` guarded by a `Connection::left`
    flag under `mutex_`. Before, `Watched`'s watcher called
    `feed_.Disconnect(name)` itself on a hang-up, and `Serve`'s own
    cleanup called it again unconditionally afterward; a new connection
    that took the freed name in the gap between those two calls was then
    disconnected by the second, stale one. `specs/Service.tla` was not
    wrong (`Noticed` already left `feed` unchanged, only `Closed` removes
    the name); the code did not match it.
-   **Also decided:** a Linux hotplug node `IN_CREATE` could not yet open,
    because udev had not yet applied its permissions, gets one retry: the
    watch on `/dev` now also takes `IN_ATTRIB`, and a probed node's
    `IN_ATTRIB` is retried through the same `IdOf` probe when the node is
    not already known, so the permissions change udev makes a moment
    after creating the node is itself the retry trigger, not a timer.
-   **Also decided:** `V4l2Session`'s constructor no longer starts its
    `HotplugMonitor`'s watch itself; a new `StartWatching()` does, called
    from `Engine::Start()` in `tools/daemon/main.cc`, once every member
    its callbacks reach, `camera_` among them, is constructed. Before,
    the watcher thread `hotplug.Start()` creates in the constructor could
    call back into `camera_` before `Engine`'s own constructor had built
    it, since a `std::jthread` begins running concurrently with whatever
    the constructor does next, unlike `AvFoundationSession`'s own
    notification observers.
-   **Spec:** no change to `specs/Service.tla` (the code was wrong, not
    the spec) or to `specs/CameraLifecycle.tla` (neither the retry nor
    the construction order is part of the device lifecycle it models).
    A mutated copy of `Service.tla`, with `Noticed` also removing the
    name from `feed`, violates `FeedMatchesConns` (6 states) and
    `UniqueNames` (8 states) under `Service.cfg`'s own bounds; the
    mutation was discarded once both counterexamples were seen.
-   **Evidence:** [#115](https://github.com/mattyv/facepalm/issues/115),
    found in the Fable review of #86-#113 named on
    [#61](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-22: The preview window and the Linux encoder close #108, no new spec

-   **Decision:** the menu's own window for the camera preview (#108, the
    gap the 2026-11-21 preview decision below filed): a "Show Camera
    Preview"/"Hide Camera Preview" menu item on both platforms that
    creates `UserPaths::preview_request` on showing and removes it on
    hiding (`PreviewLease.tla`'s `StartPreview`/`StopPreview`, the
    "menu's own ask" that spec already treats as given), then polls
    `UserPaths::preview_frame` on its own short timer (0.5 s) to show the
    latest frame: an `NSImageView` in a plain `NSWindow`
    (`tools/menu/main.mm`), a `QLabel` in a plain `QDialog`
    (`tools/menu/main_linux.cc`, needing no subclass of its own, since a
    `QDialog` shown without `exec()` already emits `rejected()` on its
    own close button). Either platform's poll also stops the preview
    once the camera itself goes off, reading the same status file
    `CameraStatus.tla` already models, so the window does not go on
    showing a stale last frame once `PreviewLease` itself has stopped
    publishing: this is the acceptance bar's own "stops with the
    camera", not a new guarantee the daemon side did not already make.
    Closing the window, by its own close button or by toggling the menu
    item, always removes the request file the same way, so an interrupted
    preview never leaves the daemon publishing to nobody.
-   **Also decided:** the Linux JPEG encoder the same gap named,
    `src/linux/preview_jpeg.{h,cc}`, through libjpeg (`find_package(JPEG
    REQUIRED)`; `libjpeg-dev` added to the three CI jobs that build the
    full Linux daemon), not Qt's `QImageWriter`: `facepalm-daemon`, unlike
    `facepalm-menu`, does not link Qt, and the encoder runs in the
    daemon, not the menu. `test/linux/preview_jpeg_check.cc` follows
    `src/macos/preview_jpeg.mm`'s own `preview_jpeg_checks` convention: a
    real encode-decode round trip through libjpeg itself, exempt from the
    coverage gate the way `menu_store_checks` already is, not from being
    tested; verified standalone against Homebrew's jpeg-turbo on this Mac
    before ever touching CI, since no Linux build is possible here.
-   **No new spec:** the window and the encoder both implement an action
    or a value `PreviewLease.tla` and `CameraStatus.tla` already model
    (the human's ask, the camera's own status), not a new piece of
    lifecycle; the same reasoning #112's `V4l2InotifyMonitor` decision
    below gives for needing no change to `CameraLifecycle.tla`.
-   **Evidence:** [#108](https://github.com/mattyv/facepalm/issues/108),
    [#61 item 8](https://github.com/mattyv/facepalm/issues/61),
    [#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-21: The camera preview stays in, to the menu alone, started by hand

-   **Decision:** the camera preview ships (#61's 2026-11-21 decision log
    reverses the rule that no image ever leaves the service), but only
    this far: a JPEG of the camera, encoded in the daemon
    (`src/macos/preview_jpeg.mm`, through ImageIO) and written to a
    private per-user file, `UserPaths::preview_frame`, that only the
    menu is expected to read. No frame is ever sent over the
    approved-token service (`src/service.cc`, `src/approved_feed.h`)
    that MCP, AI Pair or any other client reaches: that service has no
    verb for a preview, and never will, so an image can leave the daemon
    only by a process on the same machine, as the same user, directly
    reading the file (the same boundary #89's client tokens and #61 item
    8's camera-in-use indicator already accept: privacy to the service's
    clients, not to every other program the person's own account could
    run). Nothing is encoded or written unless both the camera is open
    and the menu has asked for it by creating `UserPaths::preview_request`
    (removed to stop): `src/preview_lease.cc`, following
    `specs/PreviewLease.tla`, is the gate, and a grant never carries over
    a camera close, so turning the preview off and on, or the camera off
    and on, always needs a fresh ask from the menu. The request file is
    polled on the frame-capture path already running at the camera's own
    rate, not a new timer; an unopened menu, or a closed one, asks for
    nothing and nothing is ever encoded.
-   **Why:** #61's 2026-11-21 decision log: "Preview and settings is a
    good thing" (the user, on being shown the salvaged design) keeps the
    preview, but #37's Fable plan narrows what the earlier draft did:
    "the preview sent JPEG frames out of the daemon -- keep it only to
    the local menu app over the user-only socket, never to MCP/agents,
    explicitly started by the user, with an indicator while it runs." A
    file in the already-private per-user directory needs no new socket
    or token to reach only the menu, the same reasoning #61 item 8's
    status indicator used.
-   **Rejected:** Codex's `fix/delivery-audit` design
    (`8ea3d8b`/`ada5311`/`5757ad1`/`97af5ac`), which served live frames
    over a second socket (`PreviewTransport.tla`) with its own token and
    instance secret, and which the daemon could serve to any approved
    client, MCP included, once one asked; streaming automatically
    whenever the camera opened, rather than only once the menu asks, which
    would encode and write frames nobody is watching; and reusing the
    main approved-token socket for the request/frame exchange, which
    would put a preview verb within reach of every other approved
    client, not only the menu.
-   **Cost:** the preview is macOS-only for now, and nothing in the menu
    yet lets a person actually turn it on: `src/macos/preview_jpeg.mm` is
    the only encoder (the daemon's Linux build links `preview_lease` and
    `preview_publisher`, so the lease and the request/frame files behave
    identically, but the encode step always returns nothing there), and
    neither the macOS menu nor the Linux tray has a window that creates
    `UserPaths::preview_request` and shows `UserPaths::preview_frame`.
    Filed as [#108](https://github.com/mattyv/facepalm/issues/108) rather
    than fixed here.
-   **Evidence:** [#61](https://github.com/mattyv/facepalm/issues/61),
    [#37](https://github.com/mattyv/facepalm/issues/37),
    [#36](https://github.com/mattyv/facepalm/issues/36),
    [#108](https://github.com/mattyv/facepalm/issues/108).

## 2026-11-21: Watching /dev with inotify for a camera connecting or disconnecting on Linux, not a udev monitor

-   **Decision:** `V4l2Session` takes the same `on_disconnected`/
    `on_connected` callbacks `AvFoundationSession` already does (#111,
    #13), and a `HotplugMonitor&` it asks, once, to call them: a seam, so
    `V4l2SessionTest` fakes the interface rather than linking a real watch,
    the way `CaptureSession` itself already seams the device calls out of
    `SessionCamera`'s own tests. `V4l2InotifyMonitor`
    (`src/linux/v4l2_hotplug.{h,cc}`) is the real one: once asked to start,
    it watches `/dev` with `inotify_init1`/`inotify_add_watch` for
    `IN_CREATE`/`IN_DELETE`, filtered to the same four
    `/dev/video0`-`/dev/video3` nodes item 10 of #61 already chose to
    probe for `Configure` and `List`, rather than opening a udev monitor's
    netlink socket to learn of every video node udev knows about. Each
    node that appears is opened and queried the same way `Configure`/`List`
    already do, to find its id (`v4l2_capability.bus_info`) and keep it
    whether it is attached now; one that disappears is reported by that
    same cached id, since a gone node cannot be queried for it anymore.
    Chosen over udev because it adds no library (`libv4l-dev`, already a
    dependency, is all `V4l2InotifyMonitor` needs) and because its own
    OS calls (`inotify_init1`, `inotify_add_watch`, `poll`, `read`) fake
    at link time the same way `v4l2_open`/`v4l2_ioctl`/`v4l2_close` already
    do for `V4l2Session`, so `test/linux/v4l2_hotplug_test.cc` covers the
    real watch loop with no real directory and no real camera, which a
    udev monitor's richer event properties would not obviously improve on
    for what this needs: an id already known the way `Configure`/`List`
    find one. `specs/CameraLifecycle.tla` already models a camera
    connecting or disconnecting platform-agnostically
    (`DeviceLost`/`DeviceBack`/`OtherDeviceLost`/`OtherDeviceBack`,
    `CapturesOnlyChosenDevice`), proved against both the default and the
    other device chosen (item 10 of #61, #13), so this is Linux's missing
    implementation of an action the spec already covers, not a model
    change: `CameraLifecycle.tla`/`.cfg` are untouched, and
    `specs/check.sh` still passes every config unchanged (specs/README.md's
    own table is updated to name the new Linux test, not its results).
-   **Also decided:** `V4l2Session`'s constructor asks `HotplugMonitor::Start`
    once, so the watch begins immediately and runs for the daemon's own
    lifetime, independent of `Configure`/`Start`/`Stop`'s own per-attempt
    lifecycle, the same way `AvFoundationSession`'s `NSNotificationCenter`
    observers are set up once in its constructor and torn down in its
    destructor. A node already attached when the watch starts fires
    nothing, matching AVFoundation's own notifications, which do not fire
    for what is already connected either: `Configure`'s own probe is what
    tells `SessionCamera` about a camera already there.

## 2026-11-21: Choosing the camera by a stable ID, never silently switching, and retrying a denied permission or a failed start

-   **Decision:** A camera is a `CameraDevice{id, name, built_in}`
    (`src/camera_directory.h`): `id` is stable across a reboot (macOS:
    `AVCaptureDevice.uniqueID`, through an `AVCaptureDeviceDiscoverySession`
    over the built-in, external and Continuity Camera device types; Linux:
    `v4l2_capability.bus_info`, found by probing `/dev/video0` through
    `/dev/video3`, since `opendir` has no seam a test can replace the way
    `v4l2_open`/`v4l2_ioctl`/`v4l2_close` already do). `Config` gets
    `camera.device_id` (empty for the default); `CaptureSession` (now also
    a `CameraDirectory`) resolves it in `Configure()`: empty means the
    first built-in camera found, by its own device type
    (`AVCaptureDeviceTypeBuiltInWideAngleCamera` on macOS, a `bus_info`
    starting `platform:` on Linux), never whatever the OS hands back as
    its "default" device, which is how a Continuity Camera got used
    silently (#39's live-check comment on #13). A non-empty choice is
    matched by `id` alone, across every discoverable type, and
    `specs/CameraLifecycle.tla`'s new `GuardChoosesByIdentity` proves why
    identity has to be the guard: turned off, `Start` (and `Configure`'s
    real counterpart) falls back to "any device will do" exactly as the
    code did before, and TLC finds a 5-state trace where the chosen
    device is absent but a second, unchosen one present lets the camera
    open and capture anyway (`CapturesOnlyChosenDevice`). A device
    connecting or disconnecting that is not the active target changes
    nothing: `DeviceLost`/`DeviceBack` and the new `OtherDeviceLost`/
    `OtherDeviceBack` are proven symmetric for both the default
    (`CameraLifecycle.cfg`) and the other device chosen
    (`CameraLifecycle_ChosenOther.cfg`). The person's choice is a daemon
    option read once at startup (`tools/daemon/main.cc` already only
    reads `--config` once), not a live setting, so it is a TLC `CONSTANT`
    fixed for a run, not a `VARIABLE`. The devices available are exposed
    two ways: an approved client already connected asks the protocol's
    `cameras` line (`protocol::CamerasRequest`, answered from a
    `CameraDirectory*` `Service` is optionally given, independent of the
    camera queue), and `facepalm-daemon cameras`, a new subcommand
    alongside `approve`/`revoke` that needs no running daemon, socket or
    approval (a throwaway `CaptureSession` just for `List`), one camera a
    line as id, name and whether built in, tab-separated rather than JSON,
    since the only callers are the two Settings windows and the codebase
    otherwise has no reason to carry a JSON reader. Both Settings windows
    (#97, #106, merged by the time this item landed) run that subcommand
    to fill a camera picker: an `NSPopUpButton` added to the existing
    fixed-offset layout by extending its `kExtraHeight` the same way #106
    did for its own rows, and a `QComboBox` in the existing auto-laid-out
    form, both keeping the configured choice selected, marked "not
    connected", when it is not among the cameras just listed, so saving
    without touching the picker does not silently revert to the default.
-   **Also decided (#43):** `CaptureSession::SupportedFrameRates` returns
    `std::vector<FrameRateRange>`, one entry a range, instead of one
    collapsed minimum and maximum: two disjoint ranges such as 15 and 30
    fps are not the same as "anything from 15 to 30", and collapsing a
    fixed rate such as 29.97 fps the old way (`ceil` the minimum, `floor`
    the maximum) could put the minimum above the maximum, undefined for
    `std::clamp`. `SessionCamera::Start` now picks the nearest rate across
    every range (`SessionCameraFrameRateTest`), and
    `AvFoundationSession::UseFormat` picks the 640x480 format with the
    widest frame-rate coverage, not the device's first, which a Fable
    review of #18's fix found could exclude the wanted rate (comment on
    #13); `UseFormat` has no unit test, as it never has, so this is held
    to the spec by reading, like the rest of `avfoundation_session.mm`.
-   **Also decided (#52):** `CameraReports::OnOpenFailed` takes an
    `OpenFailure` (`kNoDevice`, `kPermissionDenied`, `kStartFailed`)
    instead of nothing, so `CameraController` can tell a camera that will
    not come back without a device-connected report from one that might
    come back on its own. The spec's `Start` splits the same way, adding
    `permitted` and `startWorks`; `blocked`, cleared only by `Retry`
    (`GuardRetries`), stands in for the device cause's `usable`, and
    `Wanted` grows `permitted /\ startWorks` so `WantedOpens` proves the
    retry, not only a device coming back: turning `GuardRetries` off
    makes TLC find a denied permission that never clears, a loop with no
    exit, violating `WantedOpens`. `CameraController` gets an injected
    `RetrySchedule` (`std::function<void(std::function<void()>)>`,
    default a no-op, so every existing test and caller is unaffected) it
    calls once `blocked_` is set; the daemon's own
    (`tools/daemon/main.cc`'s `RetryAfter`) waits a fixed 5 seconds on a
    detached thread, then posts back to the control queue, chosen over a
    growing backoff because the usual cause (the person has not yet
    answered or opened a System Settings prompt) does not get more or
    less likely to have cleared the more times it is checked. Since
    `DaemonControl::OnOpenFailed` stopped the whole daemon on *every*
    failed open (`specs/DaemonAdmission.tla`'s `GuardOpenFailStops`, "a
    choice, not a protection"), it now only does that for `kNoDevice`;
    the other two causes are left to run their retry inside the same
    process instead. `specs/DaemonAdmission.tla` is re-specified for this
    too: `reports` splits `openFailed` from a new `openBlocked`, `ctl`
    gets `blocked`, and a new `Retry` (`GuardRetries`) mirrors
    `CameraLifecycle.tla`'s; `JoinOpens` moves from a plain `~>` to that
    spec's `[](<>[]P => <>Q)` shape, over a new `canStart` standing for
    `permitted /\ startWorks`, since the old shape's escape for a cause
    that never clears on its own (`~ctl.device`) cannot stand in for one
    the controller clears itself. Proving `Retry` correct this way found
    a real gap the addition itself opened: unlike `OnDeviceBack`, which
    is `DaemonControl`'s own method and checks `~dc.stopping` directly,
    `Retry` is `CameraController`'s own (scheduled by
    `schedule_retry_`, never routed through `DaemonControl`), so it has
    no such flag and depends entirely on `OpenIf`'s consent check;
    `GuardStopRevokes = FALSE` now also breaks `ReopenAfterRetire`,
    `Retry` reopening a camera a stop left standing, which the spec's own
    table records.
-   **Rejected:** keeping `GuardDiscoverAnyCamera`'s old reach (any camera
    counts once none is picked) as the default's fallback when no
    built-in camera exists at all, so facepalm would still open on a Mac
    with only an external camera and no explicit choice; rejected because
    the acceptance bar for this item is exactly the opposite ("never
    silently switching to a Continuity Camera iPhone"), and a Mac with no
    built-in camera is rare enough that asking the person to choose one
    in Settings is the honest answer. A real `opendir`-based scan of
    `/dev/video*` on Linux, rejected because it has no seam a unit test
    can stand in for, unlike the libv4l2 calls V4l2SessionTest already
    replaces at link time; a bounded probe of a fixed, small range of
    node numbers is the same tradeoff the project already made for
    `camera_probe`'s own diagnostics. Evidence:
    [#13](https://github.com/mattyv/facepalm/issues/13),
    [#43](https://github.com/mattyv/facepalm/issues/43),
    [#52](https://github.com/mattyv/facepalm/issues/52),
    [#61 item 10](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-21: Publish the camera-in-use indicator to a plain status file

-   **Decision:** the daemon writes whether the camera is in use to
    `status`, a new file beside `service.sock` in `UserPaths::directory`
    (#89's existing per-user folder), replaced atomically on every
    subscription change and once more at startup. The menu and the tray
    reread it on their own one-second timer; no socket, token, or
    environment variable of its own. `specs/CameraStatus.tla` models it.
-   **Why:** #61 item 8 asks for the menu to show "camera on" within two
    seconds of a client subscribing, and off after the last one leaves,
    on both macOS and Linux. A plain file in the directory every local
    process already trusts (the socket and the client tokens live there
    too) needs nothing a reader must authenticate or a writer must
    configure; a rename is atomic, so a reader never sees a half-written
    value. TLC's model of the earlier design salvaged from Codex's
    `fix/delivery-audit` branch (`UiStatus`, a second socket with its own
    generated token and instance secret) showed none of that machinery is
    needed once the value is this simple and this private.
-   **Rejected:** a second socket serving a live request/response
    protocol (the salvaged design), which needs its own secret and adds a
    protocol to maintain for one boolean; and restarting without
    republishing, which TLC's `NeverStaleWhileRunning` showed leaves the
    file stuck on a value from before a crash with no later subscription
    change to correct it.
-   **Cost:** a crash timed to land between a subscription change and the
    next poll can make the display miss that one change for good; this is
    physically correct, since the camera the display never caught really
    did go off in the crash, but it is worth naming as a limit TLC found
    rather than a bug it ruled out (`specs/README.md`'s `CameraStatus`
    section).
-   **Evidence:** [#61](https://github.com/mattyv/facepalm/issues/61),
    [#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-21: A QLockFile claim plus a QLocalServer handoff for the Linux tray's single instance

-   **Decision:** `tools/menu/main_linux.cc` claims a `QLockFile` at
    `~/.local/state/facepalm/menu.lock` (next to the daemon's own socket,
    `src/user_paths.h`) right after constructing `QApplication` and before
    building any window or tray icon. A launch that wins the claim becomes
    the tray and listens on a `QLocalServer` at
    `~/.local/state/facepalm/menu.sock` for later launches; one that loses
    the claim asks that server to raise the Settings window only when it
    was itself run with `--settings`, then exits either way, without ever
    creating a `QSystemTrayIcon`. The decision of which of those two things
    to do, and the retry when a handoff's owner quits or crashes first
    (`ForwardLost`, bounded by `kMaxSingleInstanceAttempts`), is the pure
    `ClaimSingleInstance` in `src/single_instance.{h,cc}`, tested with a
    mock `SingleInstanceIo`; the real `QLockFile`/`QLocalServer` calls are
    `QtSingleInstanceIo`, wiring only, in `main_linux.cc` itself. The
    lifecycle is specified first in `specs/SingleInstance.tla`, following
    `specs/UserService.tla`'s shape (Guard constants mutated to prove each
    invariant bites): `QLockFile::tryLock()` is modeled as one atomic step
    (`GuardAtomicClaim`) that also reclaims a lock left by a dead owner
    (`GuardStaleReclaimed`), since Qt documents it doing both.
-   **Why:** #98: launching `facepalm-menu --settings` while the tray
    already runs opened a duplicate tray icon and a duplicate Settings
    window, because `main_linux.cc` had no single-instance guard at all.
    `QLockFile` was chosen over a bare `QLocalServer`-only scheme (try to
    connect, and if that fails, listen) because the latter is a
    check-then-act race: two launches can both fail to connect to a dead
    or not-yet-started server and then both successfully `listen()`,
    which TLC's `GuardAtomicClaim = FALSE` mutation reproduces as two
    simultaneous primaries. `QLockFile`'s `tryLock()` is one OS-level
    atomic call with its own stale-owner detection (by checking whether
    the PID that holds it is still running), which is exactly
    `GuardStaleReclaimed` and answers the issue's worry that a crashed
    tray would wedge every later launch. The `QLocalServer` is kept only
    for the one thing `QLockFile` cannot do: carry the `--settings`
    request across to the instance that already holds the claim.
-   **Rejected:** a `QLocalServer`-only scheme, for the TOCTOU race above;
    reusing the daemon's own `service.sock` or `src/user_paths.h`'s
    `UserPaths` for the menu's lock and forwarding socket, which would
    tie an unrelated client-process concern to the daemon's own paths
    (the existing `SocketPath()` in `main_linux.cc` already computes the
    daemon's socket path locally rather than through `UserPaths`, so the
    menu's own single-instance paths follow that same local convention);
    a plain PID file, which does not give a crash-safe claim without
    re-implementing what `QLockFile` already does, and gives no path for
    forwarding a Settings request at all. Evidence:
    [#98](https://github.com/mattyv/facepalm/issues/98).

## 2026-11-21: "Pick the feeling" asks through MCP elicitation where there is no card

-   **Decision:** `ask_feeling` (`mcp/feeling-elicitation.js`) asks the same
    "How were you feeling just then?" question as `show_feeling_card` (#61
    item 5), through MCP elicitation instead of an inline widget, for a
    host that supports elicitation but not MCP Apps, such as the Claude
    Code terminal. It takes `id`, `feeling` and `feelings` exactly as
    `wait_for_reaction` returns them, requests one form field (a `string`
    enum of `feelings`, `enumNames` the same plain words the card's buttons
    use, and `default` set to Facepalm's own read), and on an accepted
    pick sends it on itself as `feel <id> <feeling>`, the same request
    `tell_feeling` sends. A decline, a cancel, or a host that refuses to
    elicit at all (including a pick outside the requested schema, which
    the SDK's own validation of an "accept" response catches before this
    tool sees it) is reported without an error and without sending
    anything. The `/facepalm:feel` skill calls it when invoked with no
    arguments, using the most recent beat's interpretation from the
    conversation, rather than requiring the person to type an
    interpretation ID.
-   **Why:** #61 item 6 asks for the same question where a host cannot
    render an inline card; MCP elicitation is the Claude Code terminal's
    way of showing a person a form (supported since v2.1.76).
-   **Rejected:** a tool that only returns the person's pick and leaves the
    caller to call `tell_feeling` separately: `show_feeling_card`'s own
    card sends `tell_feeling` itself, through its script, and this keeps
    the two paths to the same place symmetric. Re-validating the picked
    feeling against `feelings` after elicitInput returns, which this work
    found unreachable: an accepted response outside the requested schema
    throws from elicitInput itself, so a second check never saw a bad
    value in any test that could reach it.
-   **Evidence:** [#61](https://github.com/mattyv/facepalm/issues/61) item
    6, [#36](https://github.com/mattyv/ai-pair/issues/36).

## 2026-11-21: "Pick the feeling" renders as an MCP Apps card, with a text fallback

-   **Decision:** `show_feeling_card` (`mcp/feeling-widget.js`) shows the
    person a card asking "How were you feeling just then?", for a beat's
    interpretation, when the host renders [MCP Apps](https://github.com/modelcontextprotocol/ext-apps)
    (`@modelcontextprotocol/ext-apps` 1.7.5, pinned to the version a prior
    draft already proved rendering in Claude Desktop): one button per
    feeling, in the order the service's own `feelings` list gives them,
    Facepalm's read pre-highlighted, and one click either way, which the
    card sends itself as `tell_feeling`; the buttons disable on that click,
    refusing a second one. The card names no interpretation ID, basis,
    eligibility or retention, and never says a gesture happened. The tool
    takes `id`, `feeling` and `feelings` directly, the same shape
    `wait_for_reaction` already returned to the LLM, rather than an ID
    alone kept server-side: the adapter keeps no record of interpretations
    between calls, so there is nothing to look an ID up in. It is a tool of
    its own, not attached to `wait_for_reaction`'s result, so a plain wait
    or a client with no beat never carries widget framing, and a host
    without MCP Apps still gets a usable fallback: the tool's own text,
    carrying the same `id`, `feeling` and `feelings`.
-   **Why:** the user chose "pick the feeling" over the "Right/Wrong" card
    an earlier draft tried, which was "super duper confusing" (several
    sessions back); MCP Apps is the way a host renders it inline, and
    `tell_feeling` (#86) already exists to take the pick.
-   **Rejected:** attaching the card's resource URI to `wait_for_reaction`
    itself, which would put widget metadata on every wait, beat or not,
    and on every client, MCP-Apps-capable or not. Looking the ID up against
    a server-side store of issued interpretations, which the draft's
    `InterpretationFeedback` class did for the abandoned design: the
    native service already times an interpretation out after five minutes
    on its own, and the adapter has no reason to duplicate that state.
-   **Cost:** `@modelcontextprotocol/ext-apps/app-with-deps`, bundled with
    esbuild for the widget's `<script>`, is about 370 KB minified, mostly
    its own copy of zod; this is accepted as the price of the proven,
    working combination rather than risking an untested newer major version
    (2.x) or a slimmer but unverified bundling approach.
-   **Evidence:** [#61](https://github.com/mattyv/facepalm/issues/61) item
    5, [#36](https://github.com/mattyv/ai-pair/issues/36).

## 2026-11-21: Node's own JavaScript coverage report cannot be trusted as the CI gate

-   **Decision:** `scripts/coverage_js.sh` (#67, closing the gate #74
    deferred to this work) runs each JavaScript source file in `mcp/` and
    `integrations/claude-code/` against its own test file alone, under
    `node --test --experimental-test-coverage --test-coverage-include`, and
    reads the numbers from the LCOV report (`--test-reporter=lcov`) rather
    than from Node's own printed summary table or its
    `--test-coverage-lines/branches/functions` threshold flags: both pass
    even when the file's test run loaded nothing at all for it (a vacuous
    pass), and the summary table's own merge across several child
    processes, which a test for a CLI entry point (`main.js`) or a fixture
    process (the hook broker's chmod-denied case) spawns more than one of,
    is unreliable in a way the one LCOV record per file is not. The script
    also merges every LCOV record a single source file gets, since a test
    that re-imports a file under a cache-busting query string, to force a
    second evaluation of its top-level code, produces one record per
    import, each showing only what that one evaluation reached.
-   **Why:** building this gate found the summary table's numbers
    swinging between a vacuous 100 percent and a reproducibly stable,
    genuinely too-low percentage depending only on how test files were
    organized, never on the actual code; the LCOV records, read and merged
    directly, matched manual verification of the code's behavior in every
    case this surfaced.
-   **Rejected:** trusting the summary table or its threshold flags, which
    this work found unreliable for exactly the two kinds of file the rule
    most needs to hold (a CLI entry point, and anything that spawns a
    child process to isolate a side effect fixtures elsewhere cannot force,
    such as a security-sensitive syscall failure). A per-file floor in
    `coverage_thresholds.json`, as C++ and shell have, for a line Node's
    own precise coverage reports as uncovered no matter how the
    surrounding code is restructured: in every case found, folding the
    guard clause into an already correctly-tracked multi-clause condition,
    rather than giving it its own separate `if (...) return;` statement,
    made Node see it too, so no such floor was needed after all.
-   **Evidence:** [#67](https://github.com/mattyv/facepalm/issues/67),
    [#61](https://github.com/mattyv/facepalm/issues/61) item 5.

## 2026-11-21: Add an optional calibration session, in plain text

-   **Decision:** a short guided session, started with `calibrate start
    [screens]` and stepped with `calibrate next` or ended early with
    `calibrate cancel`, records the resting pose and, for each screen, the
    head pose at its centre and four corners. It saves to a private file
    (mode 0600) the daemon owns, at `UserPaths::calibration` (#37's fixed
    per-user paths, alongside `clients` and `socket`), written to a
    temporary file beside it and renamed over it, in a hand-rolled
    `name = value` text format rather than JSON, since nothing else here
    needs to parse that format back. `src/calibration.h`'s
    `CalibrationController` is the session; `src/calibration_store.h`
    saves and loads the file; `src/calibration_reader.h` feeds it each
    frame's pose, as `LookAwayReader` does its detector.
    `specs/Calibration.tla` models the lifecycle, including the camera
    going away mid-session, which cancels it. Like every client (#89), the
    CLI built for testing here, `calibrate_cli`, needs its own token,
    approved with `facepalm-daemon approve calibrate`.
-   **Why:** #36 and #35 want calibration kept optional, against the
    low-calibration goal on #35: without it, gaze stays coarse (#36, item
    11). The session's own protocol line, not a new top-level verb pair,
    keeps every client (`calibrate_cli`, the eventual menu entry of item 8,
    and MCP, which can already send any line through
    `SocketSession.request`) talking to one place.
-   **Rejected:** JSON for the saved profile, which would need a JSON
    parser this codebase does not otherwise have, just to read back a file
    only this code writes; and wiring the session into
    `tools/daemon/main.cc` and the camera's lifecycle callbacks in the same
    change, which belongs with item 7's daemon work landing alongside it
    (`CalibrationController::OnCameraLost` and `OnCameraUp` are ready for
    that wiring, covered by `NoRunningWithoutCameraTest`, but nothing calls
    them yet).
-   **Cost:** until item 7 wires `OnCameraLost` and `OnCameraUp` into the
    daemon's camera lifecycle, a session only ever sees the camera as up;
    the daemon's own `calibrate` support is likewise unwired until then,
    so `calibrate_cli` and the protocol line have nothing to talk to on a
    running daemon until that follow-up lands. Item 11's coarse gaze (#92,
    landed after this entry was first written) still reads `CoarseGazeFrom`
    exactly as it did: `src/screen_context.cc`'s rule and the `gaze` field
    it writes are untouched. `main.cc` loads a saved profile, if any, at
    `UserPaths::calibration`, and `ScreenContextReporter` now takes it as
    an optional fourth constructor argument; when given one with at least
    one screen, and a pose to judge, `Json()` adds an additive
    `calibrated_screen` field alongside `gaze` (never replacing it) from
    `NearestCalibratedScreen`. Whether `calibrated_screen` should instead
    replace or refine `gaze` itself (for instance, mapping a calibrated
    screen other than the nearest-to-resting one onto `other_screen`) is
    #92's design to make, not this item's.
-   **Evidence:** [#61 item 12](https://github.com/mattyv/facepalm/issues/61),
    [#36](https://github.com/mattyv/facepalm/issues/36),
    [#35](https://github.com/mattyv/facepalm/issues/35).

## 2026-11-21: Run clang-tidy for the Linux-only sources in CI's linux job

-   **Decision:** the `linux` job gets its own `clang-tidy` step, configuring
    with the project's `-DFACEPALM_TIDY=ON` (the same flag and `.clang-tidy`
    the `macos` job uses) once against `src/linux/*`, `test/linux/*` and the
    Linux branches of `tools/daemon/main.cc` (with the MediaPipe C paths
    already fetched for that job), and once with `-DFACEPALM_LINUX_TRAY=ON
    -DBUILD_TESTING=OFF` to also reach `tools/menu/main_linux.cc`, the Linux
    tray menu that otherwise only builds under the `debian-package` job's
    Qt and GUI test stack. `clang-tidy` itself is installed from
    apt.llvm.org at the major version Homebrew's `llvm` formula currently
    resolves to (23), so a shared source that `FACEPALM_TIDY` re-checks on
    both jobs gets the same checker on each; the comment above the `linux`
    job says to bump that number with Homebrew's.
-   **Why:** clang-tidy only ran on macOS, so it never saw the sources that
    build only on Linux, against the rule that clang-tidy checks all C++ and
    fails CI on any finding (#88).
-   **Rejected:** scoping `CXX_CLANG_TIDY` to the Linux-only targets instead
    of reusing `FACEPALM_TIDY`, which would have skipped re-checking the
    sources the `macos` job already covers, at the cost of a second
    clang-tidy wiring to keep in step with `.clang-tidy`'s checks; enabling
    `FACEPALM_LINUX_TRAY` in the main Linux build so one configure covers
    everything, which would have pulled dbus/xvfb/xdotool into the `linux`
    job for a tidy pass that never runs its GUI test. Evidence:
    [#88](https://github.com/mattyv/facepalm/issues/88).

## 2026-11-21: Report the screen context a reaction was read in

-   **Decision:** every reading, when `screen_context.enabled` (default on),
    carries a `context` object: `active_app` (the frontmost app's name),
    `window_title` (only with `screen_context.window_titles_enabled`, default
    off), `mouse` (`x`/`y`), and `gaze` (`this_screen`, `other_screen`,
    `away` or `unknown`). Gaze comes from the same head pose and resting pose
    LookAwayReader already reads, with no new calibration (#61 item 12 is
    separate): within `screen_context.other_screen_degrees` (default 7.5,
    half of `looking_away.degrees`) of the resting pose is `this_screen`;
    past `looking_away.degrees` is `away`; between the two, a turn toward a
    screen the platform's arrangement places on that side is
    `other_screen`, and one with no screen known that way, or a pose not
    yet learned, is `unknown`. Monitors are placed only left or right of the
    one the camera is assumed to face; pitch alone, and monitors stacked
    above or below, are not modeled. `src/screen_context.{h,cc}` is the pure
    rule (`CoarseGazeFrom`, `SideOf`), tested directly with no mock, as it
    has no lifecycle to spec. `src/screen_context_reader.{h,cc}` is a
    `FaceReader` that, like `LookAwayReader`, keeps its own
    `ExpressionTracker` for the resting pose, behind a mutex: it is read
    from the daemon's control thread while frames update it on the
    inference thread, which is also why it is a long-lived member of
    `Engine` rather than one of `MakeAnalyzers`' own per-session readers
    (`ScreenContextReaderForwarder` bridges the two). `src/screen_source.h`
    is the mockable OS seam `src/screen_context_reporter.{h,cc}` builds the
    `context` JSON from: `MacosScreenSource` (AppKit: `NSWorkspace`,
    `NSScreen`, `NSEvent`, and `CGWindowListCopyWindowInfo` for a title,
    which needs the Screen Recording permission) and `LinuxScreenSource`
    (X11, reachable through XWayland where a Wayland session runs one).
    Nothing from a window's pixels, and no screenshot, is read; only these
    fields leave the process, as JSON text, like every other reaction.
-   **Why:** #61's design (item 11) asks what screen and window was active,
    where the mouse was, and a coarse "this screen, another screen, or
    away" gaze, each so a client can line a reaction up with what the
    person was looking at. Window titles are opt-in because they can hold
    private text and need a permission; the rest needs none.
-   **Rejected:** eye-landmark gaze (needs landmarks no reader uses yet,
    and calibration against the camera's position, which #35's entry below
    already deferred to this item); merging `context` into a reading's
    `detail`, which would change the exact JSON `CueOf` (`src/feelings.h`)
    matches and silently break the feelings rule; a second reading just for
    context, which would double what a client sees per gesture and spend
    the feed's capacity and "missed" accounting on it.
-   **Linux's verified limit:** under a GNOME Wayland session, `XWayland`
    does not give an X11 client the real global mouse position (Mutter
    keeps Wayland coordinates from X11 clients; see the 2014 GNOME report
    discussed on Wayland's own mailing list) and sets `_NET_ACTIVE_WINDOW`
    to `None` once a native Wayland window has focus (found fixing Wine's
    own XWayland integration, 2025). Rather than report a stale or partial
    answer, `LinuxScreenSource` reads the mouse and the active app as
    `nullopt` whenever `WAYLAND_DISPLAY` is set, whatever an XWayland
    socket says; RandR's monitor geometry is a separate subsystem XWayland
    still forwards, and is read either way. This needs verifying by hand
    against a real GNOME Wayland session (not done in this change) and
    against a plain X11 session on Linux, neither of which this agent's
    environment can run.
-   **Cost:** the `other_screen` reading never tells which of several
    screens on the same side the person looks at, and a monitor stacked
    above or below the camera's screen is never `other_screen`, only
    `this_screen`, `away` or `unknown`. The platform sources
    (`src/macos/screen_context_macos.mm`, `src/linux/screen_context_linux.cc`)
    need a real desktop session and so, like
    `src/macos/avfoundation_session`, have no unit tests or coverage floor;
    `ScreenContextReporter`, which is tested, takes each behind
    `ScreenSource`. The Linux build now also needs `libx11-dev` and
    `libxrandr-dev`; CI's Linux job (#83) needs them added alongside its
    existing packages.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36),
    [#61](https://github.com/mattyv/facepalm/issues/61) item 11.

## 2026-11-21: Turn facepalm on and off from its menu, as a user service

-   **Decision:** the menu's **Turn On Facepalm…** asks for the person's
    consent, approves the bundled MCP adapter, and installs and loads a
    user service that runs the daemon with `--consent` and the saved
    settings: a launchd LaunchAgent, `com.mattyv.facepalm.daemon`, on macOS,
    and a systemd `--user` unit, `facepalm.service`, on Linux. The service
    manager starts it again after a failure and at each login. **Turn Off
    Facepalm** unloads it, which stops the daemon and closes the camera,
    and removes its file. Facepalm is on exactly while that file is there.
    `specs/UserService.tla` models it.
-   **Why:** #37 asks for the menu to start the service and give consent,
    with launchd on macOS and systemd on Linux, and for a daemon that
    crashes to recover on restart. The service manager already restarts and
    logs a background process, and a file the menu writes and removes keeps
    the on and off state where the person can see it.
-   **Rejected:** the menu running the daemon as its own child, which stops
    with the menu and would not start at login; restarting after a clean
    stop too (`KeepAlive = true`, `Restart=always`), which TLC shows adds
    nothing, since a clean stop only follows unloading; and leaving the
    file on turning off, which starts the daemon again at the next login.
-   **Cost:** turning on or off waits for the service manager, so the menu
    is busy for as long as the daemon takes to stop. Whether macOS grants
    the camera to a LaunchAgent inside the app bundle without a prompt that
    names the app has not been checked on a real Mac yet.
-   **Evidence:** [#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-21: Approve clients by name, and let several in at once

-   **Decision:** an approved client is a token file named for it in a
    directory private to the user: `clients/` under
    `~/Library/Application Support/Facepalm` on macOS and
    `~/.local/state/facepalm` on Linux, beside the service's socket,
    `service.sock`. `facepalm-daemon approve NAME` makes the file with a new
    random token, keeping one already there, and `revoke NAME` removes it.
    A client's first line is `hello <name> <token>`: the service lets it in
    when any client's file holds the token, read at each hello, and several
    connections may show the same token, each under its own name. Every
    client shares one camera subscription, taken when the first joins and
    given up when the last leaves, and every client reads the same readings.
    The MCP adapter, given no arguments, uses the socket and the `mcp` token
    from those places. A client that hangs up during a wait or a beat is
    disconnected at once by a watcher on its socket, so it gives up its
    session, and the camera, without waiting out the window.
    `specs/DaemonAdmission.tla` and `specs/Service.tla` model it, with
    liveness.
-   **Why:** #37 asks for tokens provisioned on approval, with no
    hand-run `openssl`, found by the MCP adapter and AI Pair in a fixed
    per-user place, and for two clients, such as Claude Desktop and Claude
    Code, to get the same nod. One token per app with a name per connection
    lets two sessions of one app run at once. One subscription for every
    client keeps a reaction one reading, where a subscription per client
    would add it once for each.
-   **Rejected:** one connection per token, the design before, which turns
    away a second session of the same app (`ApprovedAdmitted` fails it); a
    camera subscription per client, which delivers each reaction to the
    shared feed once per client; and paths from `XDG_RUNTIME_DIR` or
    `XDG_STATE_HOME`, which the service manager and an MCP host may not
    share with the person's shell.
-   **Cost:** any program running as the user can read the token files, as
    it could the one token file before; the socket and the files are
    private to the user, and that is the boundary. A file that is not a
    valid token is skipped, not repaired.
-   **Evidence:** [#37](https://github.com/mattyv/facepalm/issues/37),
    [#63](https://github.com/mattyv/facepalm/issues/63).

## 2026-11-21: Build and test the Linux camera's sources in CI

-   **Decision:** CI's `linux` and both sanitizer jobs fetch MediaPipe's C
    headers and `libmediapipe.so` with `scripts/fetch_mediapipe_c.sh`, each
    file checked against `scripts/mediapipe_c.sha256` and the folder cached,
    and build the Linux daemon with them. `src/linux/mediapipe_c_shim.cc`
    and `src/linux/v4l2_session.cc` are units of their own, held to 100
    percent by `test/linux/*_test.cc`, whose fakes stand in for MediaPipe's
    C API, libv4l2 and `poll` at link time. Their floors count on Linux
    only.
-   **Why:** no job built either file or the daemon, so a mistake in them,
    such as in the shim's result-shape checks, would show only on a real
    Ubuntu run.
-   **Rejected:** fetching the whole MediaPipe checkout for nine headers;
    pinning only the wheel's hash, which leaves the headers to a git commit;
    and an interface injected into `V4l2Session` for its system calls, a
    larger seam than the link-time one `test/face_model_test.cc` already
    uses. Evidence: [#83](https://github.com/mattyv/facepalm/issues/83).

## 2026-11-21: The feeling the person picks tunes the next read, in the daemon

-   **Decision:** a client sends `feel <interpretation-id> <feeling>` when
    the person says how they really felt at a beat. The service takes it only
    for an interpretation it gave that connection, once, within five minutes,
    and only a feeling from `src/feelings.h`; it answers `{"ok":true}` or an
    error. `reset feelings` forgets every pick. Each beat's interpretation
    also lists every feeling, so clients take the names from the service. The
    daemon counts picks of each feeling for each reaction and kind of beat, in
    one file private to the person (`facepalm/feelings` in `$XDG_CONFIG_HOME`
    or `~/.config`, or `--feelings-file`), which it alone writes: it holds a
    lock file while open, saves one pick at a time by renaming a synced
    temporary file over it, and answers ok only once the folder is synced. A
    feeling picked at least twice and more than half the time is the
    person's read for that reaction and beat, once a held-out check passes:
    each pick in turn is held out and read from the others, and those reads
    must be right more often than the generic rule. Three matching picks are
    enough. The MCP adapter's `tell_feeling` replaces `rate_reaction`, and
    `/facepalm:feel` replaces `/facepalm:rate`. `specs/FeelingFeedback.tla`
    models it.
-   **Why:** the user chose "pick the feeling" over the 1 to 5 rating, and
    feelings and their learning in the service, so every client feeds the
    same corrections (#61). Generic reads first, then light personal tuning
    (#35): the check keeps a person's picks from changing a read until they
    would have predicted that person better than the rule.
-   **Rejected:** the 1 to 5 rating, `rate_reaction`, the ratings file and
    `specs/RatingPersistence.tla`, which a rating needed. Codex's draft
    `meaning-model.js`, whose held-out idea this keeps: it learned only
    agreement and disagreement from nods and shakes, held out the last closed
    session, which needs a session lifecycle no client has, and compared
    against the most common label rather than the generic rule; the check
    here holds out each pick, so plain counts suffice. A machine-learning
    framework, which a few counts do not need. Keeping summaries, reading IDs
    or times in the file.
-   **Cost:** picks never fade; a reset clears them all. A beat with reactions
    of more than one kind, or with a dropped reading, takes a feeling but
    teaches nothing, since it cannot say which reaction it was about. A frown
    already reads as confused, so picking confused for it changes nothing. A
    save whose folder sync fails is answered with an error, yet its pick is in
    the file and is read. One daemon at a time can open the file.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35),
    [#61](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-21: Remove the Linux ELF build of the MediaPipe shim

-   **Decision:** remove the Bazel `libfacepalm_mediapipe.so` target, its
    ELF export list and the bats test that cross-linked it;
    `tools/fixtures/build.sh` refuses to run on Linux, naming
    `docs/linux-camera.md`. This replaces the 2026-11-20 entry "Use an ELF
    export list for the Linux MediaPipe shim".
-   **Why:** nothing used it. Since #60, the Linux daemon compiles
    `src/linux/mediapipe_c_shim.cc` in and links MediaPipe's own
    `libmediapipe.so` from its 0.10.35 wheel; CMake imports the Bazel shim
    on macOS only, and no workflow built the `.so`, so its 100 percent rule
    never ran.
-   **Rejected:** building and testing the `.so` on CI's Linux, which would
    add a half-hour Bazel build for a library no Linux build links.
-   **Evidence:** [#65](https://github.com/mattyv/facepalm/issues/65).

## 2026-11-21: Run the native tests under ThreadSanitizer too

-   **Decision:** a CI job of its own, on Linux, builds the native CTest suite
    with `-fsanitize=thread` and fails on the first data race it reports.
    It sets `vm.mmap_rnd_bits=28` first, since ThreadSanitizer does not start
    under the 32 bits of address space randomization newer kernels use.
-   **Why:** the service, the feed and the client gate run on threads, and
    AddressSanitizer and UndefinedBehaviorSanitizer find no data races. With
    the lock around `connection.done = true` in `Service::Serve` removed,
    `service_test` failed under ThreadSanitizer with a data race against
    `AcceptAll`, three runs out of three; with it, the suite is clean.
-   **Rejected:** adding the flag to the existing sanitizers job, which
    Clang refuses: ThreadSanitizer cannot be combined with AddressSanitizer.
-   **Cost:** as with the other sanitizers, only threads the tests start are
    checked, and only on Linux; the MediaPipe shim is not in the build.
    Evidence: [#66](https://github.com/mattyv/facepalm/issues/66).

## 2026-11-21: Skip a degenerate pose's frame in the expression reader

-   **Decision:** the expression reader drops a face whose transformation
    matrix has no rotation to read, as it would a frame with no face: no
    scores are judged and nothing is learned from it. The looking-away
    reader already did the same.
-   **Why:** averaged into `ExpressionTracker`'s resting pose, one such
    frame left it NaN until the next gap of more than two seconds, and the
    pose gate never shut: with a degenerate frame before it, a smile held
    while the head turned 25 degrees away was reported, where without one
    it was not.
-   **Rejected:** judging the frame's scores with the pose left out, which
    would let scores the pose has moved through ungated; and having
    `ExpressionTracker` ignore a non-finite pose itself, where
    `ReactionFrom` and the looking-away reader check the matrix as they
    read the pose from it, and this reader now does the same.
-   **Narrows:** the 2026-11-21 entry for #44, which left the other readers
    seeing such a face. Evidence:
    [#79](https://github.com/mattyv/facepalm/issues/79).

## 2026-11-21: Read a reaction as a feeling when a client marks a beat

A client that has just asked a question or made a proposal sends
`beat question|proposal <milliseconds>`. The service waits out that window
(1 to 10000 ms) and replies with the readings since the client last looked
and one `interpretation`: an ID, the beat's kind, and one of six feelings.
`src/feelings.h` is the only list of them: agreement, disagreement,
engagement, annoyance, confused and unknown; the MCP adapter checks only that
a feeling is a name. The generic rule reads only the readings made during the
window: nods as agreement, shakes as disagreement, brows down or narrowed
eyes (which the expression reader reports only once held) as confused. No
cue, cues of different feelings, or a window with a reading dropped before it
ended read as unknown; so does a reaction that comes after the window, which
goes to the next read instead. No generic rule gives engagement or annoyance
yet: they exist so a correction (#35, item 4 of #61) can name them. The
window opens when the service gets the line, so a reaction made while the
person was still reading the question is missed. `specs/FeelingBeat.tla`
models it. `rate_reaction` and the ratings file stay until item 5 replaces
them.

Rejected: reading the beat in the MCP adapter, as Codex's draft did
(`groundedWait`), because feelings and their tuning belong to the service, so
every client feeds the same corrections (#61); and its
`tentative_agreement` names, for the six feelings the user chose. Evidence:
[#70](https://github.com/mattyv/facepalm/issues/70),
[#61](https://github.com/mattyv/facepalm/issues/61).

## 2026-11-21: Read looking away from the resting pose, held a second

-   **Decision:** report `{"gesture":"looking_away"}` once when the head's
    yaw or pitch stays more than 15 degrees from its resting pose for 1,000
    ms, and again only after it comes back within half that. The resting
    pose is `ExpressionTracker`'s, learned from the same faces with the
    expression settings; nothing is reported while it is being learned.
    `looking_away.enabled`, `.degrees` and `.hold_ms` are in the config file.
    `specs/LookAway.tla` models the rule.
-   **Why:** #35's design reads reactions from head pose and blendshapes
    judged against the person's own resting state, with no calibration up
    front. On one person's recordings, replayed through the detector, each
    prompted look down and each turn the face model kept tracking was
    reported once, and nods, which stay within about 12 degrees and leave
    for under half a second, were not.
-   **Rejected:** reading gaze, which needs eye landmarks no reader uses yet
    (#61 item 11); counting roll, which is a tilt, not a look away; and a new
    reaction key, such as `attention`, which the daemon's feed filter and
    every client would have to learn.
-   **Cost:** a pose held for long enough, under a minute, becomes the resting
    pose, so turning back from it is reported as a look away. A face the
    model loses, as on a wide turn, gives no frames and so no report. The
    thresholds come from one person's recordings.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35).

## 2026-11-21: Hold the shell scripts to 100 percent of lines with kcov on Linux

`scripts/coverage_shell.sh` runs every bats test under kcov, in a Debian
trixie container on CI, and holds each shell script to 100 percent of its
lines (its `shell` floor in `coverage_thresholds.json`), as the C++ and
JavaScript are held. The tests run copies of the scripts in temporary
folders, so a script's lines are counted over every copy. kcov counts lines
that bash never traces: each line of a multi-line quoted string or array, a
`$(...)` continued over lines, and `done <file`. The scripts are written so
none occurs: awk programs come from here-documents, which kcov skips; long
lists are built with `+=`; each `$(...)` sits on one line; and loops read
here-strings. Rejected: floors at the measured number for the lines kcov
miscounts (this PR's first version), since the user holds every file to 100;
kcov on macOS, where its bash tracing rides on stderr and bats captures it,
so every script reads 0 (ai-pair's finding; here, Homebrew's kcov 43 running
one bats file had not finished after two minutes); bashcov, a Ruby gem
neither repository uses; and Ubuntu's runner, which does not package kcov.
Evidence: [#2](https://github.com/mattyv/facepalm/issues/2).

## 2026-11-21: A turn's suppression ends once the head settles anywhere

-   **Decision:** after a turn, later turns stay suppressed until the model
    reads no gesture for 750 ms with yaw back within ten degrees of the
    estimated starting yaw, as before, or for 2,000 ms with yaw within ten
    degrees of wherever the head settled. The pose it settled in becomes the
    origin, and the next turn is reported. `TurnExcursion.tla` gains the
    settled hold and a liveness property, `TurnRearms`.
-   **Why:** the suppression had no bound: someone who turned to a second
    screen, or settled 15 degrees off, got no turns for as long as they
    stayed (#62), and looking away (#35) needs turns to keep reporting.
-   **Rejected:** releasing after the general gate's 750 ms of stillness
    anywhere, which reports a glance's return as a second turn, the problem
    the turn latch was made for; and a plain timeout from the turn, which
    releases mid-movement and can report the same excursion twice.
-   **Cost:** a head held away for two seconds and then turned back gives two
    turns, one each way. Two seconds is the limit NHTSA's 2013 visual-manual
    distraction guidelines set for a single glance, taken from memory and not
    measured on facepalm's users; a longer glance reports its return.
-   **Replaces:** the suppression rule in "A turn is one excursion from a
    starting pose" (2026-11-20).
-   **Evidence:** [#62](https://github.com/mattyv/facepalm/issues/62).

## 2026-11-21: Bound camera_probe's recording and fail an incomplete one

The probe's recorder holds at most 30 frame copies waiting to be written
(two seconds at 15 frames a second, about 35 MiB at 640x480), and drops a
frame past that. The probe exits only after every frame it accepted is
written or has failed, and exits with 1 when any frame was dropped or not
written; a frame's capture time goes in `timestamps.txt` only once its PNG
is written. Rejected: blocking the capture queue on a full backlog, which
would stall the camera and the face model with it, and exiting without
waiting, which reported success with frames still unwritten. A disk that
never answers now leaves the probe waiting rather than exiting. Evidence:
[#41](https://github.com/mattyv/facepalm/issues/41).

## 2026-11-21: Send no head-angle reaction for a degenerate pose

When the face's transformation matrix has no rotation to read (all zeros, or
holding NaN or infinity), the head-pose reader sends nothing for that frame,
rather than JSON holding `nan`, which no client can parse. The other readers
still see the face. Rejected: sending `null` angles, which every client would
have to handle for a case of no use to it, and sending zeros, which reads as
a head facing the camera. Evidence:
[#44](https://github.com/mattyv/facepalm/issues/44).

## 2026-11-21: Remove only facepalm's own socket at the socket path

At startup, facepalm still replaces a socket that nothing answers on, so it
starts again after a crash without help. It now refuses a path that holds
anything other than a socket, and leaves it alone. On close, it removes the
socket file only if it is still the one it bound, so it cannot delete a newer
facepalm's socket. Rejected: never removing an existing socket (a draft on
`fix/delivery-audit`), because a daemon started from the menu after a crash
could not recover without someone deleting the file by hand. Evidence:
[#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-20: Use the macOS settings layout on Linux

Use Qt Widgets to present the same detection controls and field order as the
macOS settings window. Both hosts use the shared config parser and document
editor, which preserve comments and unrelated values. The desktop launcher
opens Settings directly, and the tray keeps service readiness and replay
status. This replaces the status-only Linux window so users can edit the
daemon configuration on either desktop.

Settings apply at the next daemon start with `--config` and the displayed
path. Save checks for changes since opening the form and writes atomically.
A writer racing that final save can still overwrite or be overwritten.
[Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Run the Linux camera through the shared daemon

Use Video4Linux through libv4l2 and link the pinned MediaPipe Tasks C
library on Linux. The capture adapter joins its worker before reporting
retirement, and the existing consent, session, frame, and feed owners
keep their responsibilities. Serial queues run the same daemon composition
as macOS without requiring libdispatch on Linux.

The build keeps the existing FaceModel interface and uses the official C
library to avoid a separate application or an inference service. It needs
matching 0.10.35 headers and library and a Face Landmarker model. The first
Linux setup uses one selected single-plane camera. Restart after reconnecting
a disconnected device. The development daemon installs separately from the
tray `.deb`.

On Debian 13, the integrated camera bound at `/dev/video0`, MediaPipe
processed a live frame, and MCP returned a classified turn. A refused
client left the camera closed. Two consecutive approved MCP sessions each
opened, processed, and released the device. [Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Package the Linux tray and replay tools for Debian 13

Use Qt 6 Widgets for the Linux tray, menu and status window. Keep the
existing shared core and macOS AppKit host. Qt supplies the desktop tray
protocol and menu, so Facepalm only owns its status and controls.

Build and install-test the `.deb` in separate Debian 13 amd64 environments.
The package contains the tray, desktop launcher, licensed icon and replay
CLI. The MCP adapter keeps its separate Node.js 22 install because Debian
13 supplies Node.js 20. Users launch the tray from their applications menu.

Desktops with StatusNotifierItem support show the icon. The status window
covers desktops without a compatible tray. The Linux camera daemon is a
separate portability milestone. [Facepalm service issue #37](https://github.com/mattyv/facepalm/issues/37)

## 2026-11-20: Install the portable replay and MCP adapter separately

-   **Decision:** use CMake's `BUILD_TESTING` switch and install the existing
    replay CLI and MCP adapter with its lockfile under a relocatable prefix.
-   **Why:** the portable tools can be built and installed without GoogleTest
    or a camera backend, while normal test builds retain their existing suite.
-   **Rejected:** installing a synthetic fixture as a daemon or presenting the
    portable tools as a complete Linux camera application.
-   **Cost:** the adapter needs Node.js and separately installed npm
    dependencies and a separately provisioned compatible service. This does
    not deliver a Linux camera backend, daemon, model, or live camera test.
-   **Evidence:** [#37](https://github.com/mattyv/facepalm/issues/37).

## 2026-11-20: Use an ELF export list for the Linux MediaPipe shim

Replaced by "Remove the Linux ELF build of the MediaPipe shim" (2026-11-21).

-   **Decision:** build a `.so` on Linux with the same six-function C ABI as
    the macOS dylib, hiding all other symbols with an ELF version script.
-   **Why:** MediaPipe's internal symbols must not collide with another copy.
-   **Rejected:** exporting all symbols or passing Mach-O flags to Linux.
-   **Cost:** the ELF boundary was cross-linked with synthetic symbols; the
    full MediaPipe build and runtime have not yet been verified on Linux. This
    does not supply a Linux camera backend or a runnable Linux daemon.
-   **Evidence:** [#3](https://github.com/mattyv/facepalm/issues/3).

## 2026-11-20: Store feedback only when explicitly requested

Replaced by "The feeling the person picks tunes the next read, in the daemon"
(2026-11-21).

-   **Decision:** `--ratings-file` enables a private, single-writer versioned
    JSONL feedback file capped at 10 MiB. A rating succeeds only after its
    record is appended and synced. It records the delivered interpretation,
    reading ID, publication and rating times, and score.
-   **Why:** explicit local feedback can support future investigation without
    storing frames, model inputs, credentials or client identity.
-   **Rejected:** silently discarding ratings or automatically tuning from
    them. No rating is accepted without a configured feedback sink.
-   **Cost:** a partial write or sync failure disables the sink for that run;
    a failed sync may nevertheless leave an unacknowledged record. Incomplete
    files need manual recovery. Existing files are capped, never truncated.
-   **Replaces:** the earlier daemon decision to reject all ratings.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Use a native menu and next-start settings on macOS

-   **Decision:** use an AppKit menu-bar host and native settings window. It
    edits the daemon's existing config format, including gesture and expression
    tuning, preserving comments and other settings. It saves atomically after
    checking the file snapshot.
-   **Why:** the current daemon has no status or settings control API; the
    native host provides the requested controls without inventing live state.
-   **Rejected:** implying the camera is running, or starting the daemon from
    the menu before there is a reviewed consent and lifecycle interface.
-   **Cost:** settings apply only when a daemon is started with `--config` and
    the shown path. Camera status is unavailable. Snapshot checking detects
    prior external edits but is not a cross-process compare-and-swap; a writer
    racing the final save can still be overwritten. The host does not stop a
    separately running daemon when it quits.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Gate the foreground daemon by consent and a private token

The rating rejection in this entry is replaced by the feedback decision above.

-   **Decision:** the daemon starts only with explicit launch consent, a
    readable model, and a user-private token file. One connection presenting
    that token admits the camera; disconnect revokes its subscription. Each
    connection gets a distinct internal identity and no credentials enter the
    feed. Stop waits for camera closure and model retirement.
-   **Why:** a same-user socket connection or a self-asserted name alone is not
    proof that the user approved a client.
-   **Rejected:** opening the camera at login or treating installation as
    consent. The token must be provisioned by an explicit approval step.
-   **Cost:** one approved connection at a time; no approval UI or feedback
    storage exists in this slice, so daemon ratings are rejected. Consent is
    launch-scoped and stopping the process revokes it. Only a supplied
    `--config` file is loaded. This has not been exercised on a live camera.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: Expose the reaction feed through a stdio MCP adapter

-   **Decision:** each MCP process owns one uniquely named socket session and
    forwards bounded waits and ratings to the existing feed. The adapter uses
    the official Node MCP SDK as a maintained production adapter; it does not
    own or start the camera.
-   **Why:** an agent can use the same interpreted feed and rating rules as
    other clients without a second cursor or a new wire protocol.
-   **Rejected:** silently reconnecting after cancellation: a native wait may
    already have consumed a reading, and a new session has a different cursor.
-   **Cost:** cancelling an active request closes the session and can lose an
    unseen delivery; the native wait may run until its timeout. The feed records
    delivery to MCP, not confirmation that the host displayed the summary.
    Socket hello names are self-asserted, not client authentication.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-20: A turn is one excursion from a starting pose

Its suppression rule is replaced by "A turn's suppression ends once the head
settles anywhere" (2026-11-21).

-   **Decision:** after a turn, suppress further turns until yaw returns within
    ten degrees of the estimated starting yaw and the model reads no gesture
    for 750 ms. The estimate is the oldest yaw in the model window when the
    turn candidate starts. Other gesture classes retain their existing rule.
-   **Why:** a turn and its return were reported repeatedly as separate turns.
-   **Rejected:** treating any held pose as a completed excursion: it reports
    the return as another turn.
-   **Cost:** ten degrees is based on one person's capture; a different return
    pose can suppress a later turn. Tracking loss starts a new track, without
    evidence of return. A head turn does not establish gaze or attention.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Run native tests under Clang sanitizers

-   **Decision:** run the native CTest suite on Linux with AddressSanitizer and
    UndefinedBehaviorSanitizer together, failing on the first finding.
-   **Why:** coverage and clang-tidy do not detect all exercised memory errors
    and undefined behavior at runtime.
-   **Rejected:** replacing the existing coverage or clang-tidy checks; each
    finds different problems.
-   **Cost:** this checks exercised Linux CMake code only. It does not cover the
    macOS camera, the separately built MediaPipe shim, or unexecuted paths.
-   **Evidence:** the user's request for sanitizer checks in CI.

## 2026-11-20: Replay recorded head gestures without a camera

-   **Decision:** replay the probe's raw gesture frames through the live window,
    model and reporting gate; new recordings mark each loss of a face.
-   **Why:** recorded frames make gesture classification and tracking gaps
    inspectable without another camera session.
-   **Rejected:** treating a timestamp gap as lost tracking: dropped frames and
    no-face callbacks cannot be distinguished by elapsed time alone.
-   **Cost:** older logs lack no-face markers, so their replay cannot reliably
    reproduce tracking resets. Replay uses the default confidence threshold.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Expression reading pauses while the head moves

-   **Decision:** while the head is away from its resting pose, or has just
    moved, the expression reader neither reports nor learns.
-   **Why:** the face model's expression scores follow head pose. With no
    expression asked for, nods, shakes and tilts were reported as about 40
    expressions in 85 seconds.
-   **Rejected:** pausing on distance from the resting pose alone, which left
    24 false reports; pausing on head speed, which swallowed a real squint.
-   **Cost:** an expression made while the head moves is reported only if it
    is still held shortly after the head settles.
-   **Evidence:** [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: A head gesture is reported once per movement

-   **Decision:** a gesture is reported once it has held for a moment, and
    nothing more is reported until the head has been still.
-   **Why:** within one movement the model's answer alternates between related
    gestures, such as shake and turn, and each change was being reported. The
    model's authors do not report its raw answers either; this follows their
    smoothing, in a form that does not wait for the movement to end.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: A frown and a squint are read apart

-   **Decision:** lowered brows and narrowed eyes are separate expressions.
-   **Why:** a small frown moves the brow scores alone, and averaged with the
    squint scores its rise was lost. A squint therefore reports both.
-   **Replaces:** the plan in #46's description to report them as one.
-   **Evidence:** [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: Facial expressions come from the face model's own scores

-   **Decision:** expressions are read from the 52 blendshape scores
    MediaPipe's face landmarker already produces, each judged against the
    person's own resting face. facepalm trains no model of its own for this.
-   **Why:** measured on one person's small, natural expressions, a brow
    raise, a smile and pressed lips stand 20 to 90 times above the resting
    spread. The scores cost nothing extra and are free to ship.
-   **Known weakness:** the frown, at about 6 times the resting spread.
-   **Rejected:** OpenFace 2.2 and 3.0, whose licence allows noncommercial
    research use only and forbids passing the software on; ready-made emotion
    models, which label happy, sad and angry and not confused or
    concentrating, read a face picture and not the scores, and are trained on
    data with noncommercial terms.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35),
    [#46](https://github.com/mattyv/facepalm/issues/46).

## 2026-11-20: facepalm is licensed GPL-3.0-only

-   **Decision:** facepalm's licence changes from MIT to GPL-3.0-only.
-   **Why:** Idiap's head-gesture code and model are GPL-3.0-only, and facepalm
    contains a port of both.
-   **Rule that follows:** a model under the GPL is acceptable; one under
    noncommercial terms is not.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: Head gestures use Idiap's CCDbHG model, alone

-   **Decision:** nods, shakes, tilts, turns and up-down movements are read by
    a C++ port of Idiap's CCDbHG model. Nodding Pigeon is removed.
-   **Why:** Idiap's model was trained on natural conversation and reads the
    landmarks and head pose facepalm already has. Live, it caught one person's
    small nods, a single quick nod and small shakes, with nothing reported
    while still.
-   **Rejected:** Nodding Pigeon, which rounds face positions to 1 percent of
    the picture and so loses small movements, and which added nothing beside
    the other model; a detector of our own on head angles, kept only as a
    fallback had no existing model worked.
-   **Evidence:** [#34](https://github.com/mattyv/facepalm/issues/34).

## 2026-11-20: TLC is pinned to its stable release

-   **Decision:** CI downloads TLC 1.7.4.
-   **Why:** the 1.8.0 pre-release's file is rebuilt in place, which broke the
    pinned checksum and failed every pull request.
-   **Evidence:** [#48](https://github.com/mattyv/facepalm/issues/48).

## 2026-11-20: Decisions are logged here

-   **Decision:** this file holds each decision and why; the issue it names
    holds the evidence and the discussion.
-   **Why:** the reasons behind one day's model choices were spread over four
    issues and their comments.
-   **Replaces:** the rule that decisions go in issues only.
-   **Evidence:** [#54](https://github.com/mattyv/facepalm/issues/54).

## 2026-11-18: How reactions reach an agent

Its 1 to 5 rating is replaced by
"The feeling the person picks tunes the next read, in the daemon" (2026-11-21).

-   **Decision:** facepalm has its own MCP server. Where a client supports
    hooks, reactions are delivered automatically after each agent action; the
    MCP tools are the fallback.
-   **What the agent gets:** interpreted reactions (a frown, a smile, a nod,
    and what each is taken to mean), not raw data, and only what happened since
    it last looked.
-   **What the person sees:** a one-line summary written by facepalm, carrying
    a reading ID, which the agent shows as it is.
-   **Feedback:** a rating from 1 to 5, given through the MCP or in the AI Pair
    panel, and only within five minutes of the reading being shown.
-   **Also decided:** a settings page in the browser, served locally;
    calibration is optional; Ubuntu is supported; ChatGPT's desktop app is out
    of scope.
-   **Evidence:** [#36](https://github.com/mattyv/facepalm/issues/36).

## 2026-11-18: Generic readings first, light personal tuning after

Its tuning from ratings is replaced by
"The feeling the person picks tunes the next read, in the daemon" (2026-11-21):
the person's picked feelings tune the reads.

-   **Decision:** reactions are read with rules that are the same for
    everyone, and tuned for each person from their ratings.
-   **Why:** most expressions are small, the generic readings are expected to
    be right about nine times in ten, and calibration up front is a burden.
-   **Evidence:** [#35](https://github.com/mattyv/facepalm/issues/35).
