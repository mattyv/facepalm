# TLA+ specs

facepalm's TLA+ specs model the parts with a lifecycle or concurrency before
their code is written, as `CLAUDE.md` requires. Each spec's section lists its
invariants with the test that proves the code obeys each one, and the
mutations that prove each one bites. The layout follows ai-pair's
`specs/README.md`.

## Tools

TLC 1.7.4 runs on Java 11 or later; CI uses 21. `scripts/download_tlc.sh`
fetches its jar and accepts it only with the pinned SHA-256. It is the latest
stable release: 1.8.0 is a pre-release whose jar is rebuilt in place, which
broke the pin once (#48):

```shell
bash scripts/download_tlc.sh build-tla/tla2tools.jar \
  936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88
bash specs/check.sh
```

`specs/check.sh` checks every config; `Foo_Bar.cfg` checks `Foo.tla`. Set
`TLC` to run TLC some other way. On macOS, Homebrew's `openjdk` is keg-only:
put `/opt/homebrew/opt/openjdk/bin` on the `PATH`.

## Results

| Config | Bounds | Result | Distinct states |
|---|---|---|---|
| `Calibration.cfg` | six steps, one camera outage | holds | 30 |
| `CameraLifecycle.cfg` | two clients, one helper restart, a second device, and the OS permission and Start's own readiness, with the default device chosen | holds | 20,864 |
| `CameraLifecycle_ChosenOther.cfg` | the same bounds, with the other device chosen (#13) | holds | 20,864 |
| `DaemonAdmission.cfg` | three clients, two approved; each client and the OS queue at most two changes; a camera that can be blocked and retried (item 10 of #61, #52) | holds, with `StopEnds`, `LastLeaveCloses` and `JoinOpens` | 19,892 |
| `FeedWaitRetirement.cfg` | one canceled wait and its successor | holds | 8 |
| `FeelingBeat.cfg` | two clients, three readings, a timeline keeping two, one beat each, cues of a nod, a shake and none | holds | 3,815,485 |
| `FeelingFeedback.cfg` | two clients, two interpretations, two feelings, one reset, one restart | holds | 264,176 |
| `FramePipeline.cfg` | the camera opening twice | holds | 93 |
| `HookBroker.cfg` | one hook peer and one MCP session | holds | 20 |
| `LookAway.cfg` | three moves, one tracking gap | holds | 134 |
| `McpSocket.cfg` | one session, up to two requests, backoff bounded to two ticks | holds, with `LinkRecovers` | 7 |
| `PreviewLease.cfg` | three camera sessions | holds, with `EventuallyOpens` | 19 |
| `ProbeShutdown.cfg` | four frames, a backlog of two | holds | 185 |
| `ReactionFeed.cfg` | two clients, three readings, a timeline keeping one | holds | 12,171 |
| `Service.cfg` | three connections, two client names, an approved token and a stranger's | holds, with `StopEnds` and `HangUpEnds` | 1,040 |
| `SingleInstance.cfg` | two launches, each free to run with or without `--settings` | holds, with `LaunchSettles` and `RequestServed` | 77 |
| `UserService.cfg` | the menu, the service manager, one daemon and a session that may log out and in | holds, with `TurnOffStops` and `OnRuns` | 15 |
| `TurnExcursion.cfg` | yaw near or away, two moves a turn, two hold lengths | holds | 26 |

## `McpSocket`: an MCP session owns one socket

Models one MCP session's socket to the daemon (#117, mcp/session.js). Two
things can end a request before it reaches the wire, and they are not the
same: `state` is this session's own, terminal "closed" (`close()`, or an
exchange's own cancellation; `Cancel`), and `link` is whether the live
connection is up right now (`LinkDown`), which comes and goes on its own and
is retried, unlike `state`. A lost link's retry waits out `backoff`, bounded
by `MaxBackoff` and counted down a tick at a time by `BackoffTick`, before
`ReconnectSucceeds` or `ReconnectFails` may run. `McpSocket.cfg` checks
`MaxBackoff = 2`: 7 distinct states, every property holds.

| Property | What it says | Test (`mcp/test/session.test.js`) |
|---|---|---|
| `NoSendAfterClose` | A request never reaches the wire once this session is closed, or while its link is down. | `'active cancellation is terminal and never reconnects'`, `'refused hello and pre-cancelled calls do not send requests'` |
| `OneRequest` | At most one request is on the wire at a time. | `'bounds pending requests and recovers after they settle'` |
| `BackoffBounded` | The wait before a retry never grows past `MaxBackoff`. | `'offline never bounds beyond its own cap'` |
| `LinkRecovers` | Something must happen: a lost link, on a session never itself closed, always comes back up (or the session closes first, giving up the right to retry at all). | `'reconnects once its backoff has run out, without the caller doing anything (#117)'`, `'backoff grows on each failure in a row, and resets once a connection succeeds (#117)'` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardClosed = FALSE`: a request can reach the wire once closed, or while the link is down | `NoSendAfterClose`, 2 states |
| `GuardSingle = FALSE`: a second request can reach the wire while one is already there | `OneRequest`, 10 states |
| `GuardBackoffBounded = FALSE`: a failed reconnect can set a backoff past `MaxBackoff` | `BackoffBounded`, 9 states |
| No fairness on `BackoffTick` and `ReconnectSucceeds` | `LinkRecovers`: a link down forever, stuttering once its own backoff is set |

`src/daemon_calibration.h`'s own `OnCameraLost`/`OnCameraUp`
(specs/Calibration.tla's `CameraLost`/`CameraBack`) are a close cousin: a
different link, modeled separately since the camera's and the MCP
session's are not the same one going down. The model does not prove the
remote service has retired, that a host displayed a delivery, or anything
about the token a reconnect sends (`mcp/session.js`'s `currentToken`, read
fresh on every attempt so an approval that lands after this session
started reaches it with no restart, is outside the model, a single opaque
`hello`).

## `DaemonAdmission`: the daemon's clients, camera and stop

Models the daemon's control logic (#37, #63), as `src/daemon_control.cc`
and `src/approved_feed.cc` implement it and `tools/daemon/main.cc` wires it:
which approved clients are connected, when the camera opens and closes for
them, and how the daemon stops. Every approved client shares one camera
subscription: `ApprovedFeed` tells the control queue when the first one
joins and when the last one leaves, and every client reads the same
readings from `ReactionFeed`, so two clients get the same nod.

Four serial queues run the work, as in the daemon. The control queue owns
`DaemonControl` and `CameraController`, so each of its tasks is one atomic
action; it takes subscription changes from `ApprovedFeed`, reports from the
camera session (opened, open failed, open blocked, closed, device lost,
device back), and the frame pipeline's word that a face model was
destroyed. The session queue opens and closes the camera, and the inference
queue creates and destroys the face model. Each queue is FIFO, and the order
between the control queue's three sources is left free, which allows more
interleavings than the code has. The model covers stopping, a retirement
under way, a device coming back during one, an open that found no device,
one that was blocked some other way and retried, a lost device, and a
`Stop` re-entered from a failed open. The queues are weakly fair, and so is
a pending retry; the person, the clients and the OS are not. `MaxQueue = 2`
bounds what the clients and the OS may queue, so the state space is finite.

Item 10 of #61 (#52) splits "the open failed" into two reports: no device
to use (`openFailed`, unchanged: `GuardOpenFailStops` still decides whether
that stops the daemon) and everything else Start can fail at, such as a
denied permission or a configuration race (`openBlocked`), which never
stops the daemon and is retried instead, fairly, through the new `Retry`
(`GuardRetries`). `canStart` stands for whatever Start needs that is not
the device (`specs/CameraLifecycle.tla`'s `permitted /\ startWorks`); while
it is false, an open attempt can only come back blocked.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `ApprovedOnly` | Only approved clients are connected. | `ApprovedOnlyTest` in `test/service_test.cc` and `test/client_approvals_test.cc`; `approval.test.mjs`'s stranger |
| `OpenHasConsent` | The camera is opening or open only with consent. | `OpenOnlyWithConsentTest` in `test/camera_controller_test.cc`; `StopEndsTest.StoppingWithTheCameraClosedRetiresAtOnce` checks that stopping revokes it |
| `SubscriptionMatches` | Once the subscription changes are handled, the camera is wanted exactly while some client is connected: one client leaving does not take it from another. | `SubscriptionMatchesTest` in `test/approved_feed_test.cc` and `test/daemon_control_test.cc`; `approval.test.mjs`'s "camera on" and "camera off" |
| `ReopenAfterRetire` | A camera opens only once the previous face model is destroyed. | `ReopenAfterRetireTest` in `test/daemon_control_test.cc` |
| `ExitClean` | The daemon exits only with the camera closed and stopped, and every face model destroyed. | `ExitCleanTest` |
| `StopEnds` | Liveness: stopping, by the person, a signal, a failed model or a failed open, always ends with the daemon exiting. | `StopEndsTest` |
| `LastLeaveCloses` | Liveness: with no client left, the camera closes, unless one joins. | `SubscriptionMatchesTest.OnlyTheFirstJoinAndTheLastLeaveCount`; `OpenOnlyWhenSubscribedTest.TheLastClientLeavingCloses` in `test/camera_controller_test.cc` |
| `JoinOpens` | Liveness, and something must happen: from every point, a connected client, the device and whatever else Start needs, all held, lead to the camera being open (#52). | `JoinOpensTest`, `ReopenAfterRetireTest.OnlyTheOpeningCameraCreatesAModel`; `WantedOpensTest`, `RetriesTest` in `test/camera_controller_test.cc`; `ADeniedPermissionDoesNotStopTheDaemon`, `AFailedStartDoesNotStopTheDaemon` in `test/daemon_control_test.cc` |

`CHECK_DEADLOCK FALSE` stays only because an exited daemon does nothing;
the liveness properties are what say that stopping and closing finish.
Each mutation, and what TLC found (counts are the counterexample's states):

| Mutation | Broke |
|---|---|
| `GuardApproval = FALSE`: a client not approved joins | `ApprovedOnly`, 2 states |
| `GuardFirstLast = FALSE`: every join and leave changes the subscription, the one-client design | `SubscriptionMatches` |
| `GuardConsent = FALSE`: the camera opens without consent | `OpenHasConsent`: a retirement after stopping reopens it |
| `GuardStopRevokes = FALSE`: stopping leaves consent | `ReopenAfterRetire`: `Retry` (#52) reopens the camera after a stop that left consent standing, since `Retry` is `CameraController`'s own method and, like `Subscribe` or `Leave`, has no `stopping` flag of its own to check — it relies on this one; `ExitClean` also fails the same way, checked alone |
| `GuardStopRetires = FALSE`: stopping with the camera closed retires nothing | `StopEnds` |
| `GuardRetireOnce = FALSE`: two retirements at once | `ExitClean`: the daemon exits with a face model still to destroy |
| `GuardAckAfterRetire = FALSE`: a closed camera counts as closed before its model is destroyed | `ReopenAfterRetire` |
| `GuardDeferDeviceBack = FALSE`: a device coming back is let in during a retirement | `ReopenAfterRetire` |
| `GuardOpenOnce = FALSE`: an `Opened` after closing began creates a model | Nothing: the model is destroyed behind it on the same FIFO inference queue, so the guard only saves loading a model no frame will reach |
| `GuardOpenFailStops = FALSE`: a camera with no device to use leaves the daemon running | Nothing: stopping for that cause is a choice, not a protection; without it the camera waits for the device to come back, which `JoinOpens` allows |
| `GuardRetries = FALSE`: a denied permission or a failed start never clears | `JoinOpens` (#52): `ctl.blocked` latches forever, so a session that stays joined, with the device and everything else Start needs, never reopens |
| `OpenIf` never opens | `JoinOpens` |
| `Leave` does not close | `LastLeaveCloses` |
| `Retired` never exits | `StopEnds` |
| No fairness on `Report`, `SessionTask`, `InferenceTask`, `Retried` or `Retry` | `StopEnds`, `LastLeaveCloses` and `JoinOpens` |
| No fairness on `Subscription` | `LastLeaveCloses` and `JoinOpens` |
| `InferenceTask` writes a model outside the type | `TypeOK`, 6 states |

Each liveness mutation was checked with each property alone. Before #63,
`DaemonControl::OnOpenFailed` also retired the model when stopping with the
camera closed; the model shows that case cannot arise, since stopping
revokes consent and a camera that began closing reports `Closed`, so that
code was removed. The failed open's stop moved from the daemon's `main.cc`
into `DaemonControl`, where the spec's `OnOpenFailed` has it.

Finding `GuardStopRevokes = FALSE` now also breaking `ReopenAfterRetire`
(item 10 of #61, #52) answers a question `Retry`'s own addition raised:
`OnDeviceBack` guards its own reopen with `~dc.stopping` because
`DaemonControl` (which it belongs to) knows the daemon is stopping, but
`Retry` is `CameraController`'s own method, scheduled by itself
(`src/camera_controller.cc`'s `schedule_retry_`) rather than routed
through `DaemonControl`, so it has no such flag to check and depends
entirely on `OpenIf`'s existing consent check — exactly the one
`GuardStopRevokes` protects. The model was briefly, mistakenly given
`Retry` an `~dc.stopping` guard of its own to "fix" this, which `CHECK_DEADLOCK
FALSE`'s sibling rule for every other action (none may fire once
`dc.exited`) then caught as the real miss: `Retry` and `ToggleCanStart`
had no `~dc.exited` guard at all, so `ExitClean` failed even with every
other guard on, once `Retry` fired after the daemon had already exited.
Both omissions are fixed; the surviving, correct finding is that
`Retry`'s safety after a stop rests on `GuardStopRevokes` alone, which the
table above now says.

The same mutations, made to the code, fail the tests that stand for the
properties they break:

| Mutation to the code | Fails |
|---|---|
| `Subscription` is not ignored while stopping | `SubscriptionMatchesTest.ChangesAreIgnoredOnceStopping` |
| `Stop` does not revoke consent | Seven tests, among them every `StopEndsTest` and `ExitCleanTest` |
| `Stop` does not retire a closed camera's model | `StopEndsTest.StoppingWithTheCameraClosedRetiresAtOnce`, `AFailedModelStopsTheDaemonAsAFailure`, `ACameraThatFailsToOpenStopsTheDaemonAsAFailure` |
| More than one retirement at a time | `ExitCleanTest.OneRetirementAtATime` |
| `OnClosed` counts the camera closed before the retirement | `ReopenAfterRetireTest.AClosedCameraCountsOnlyOnceTheModelIsGone`, and two others |
| A device coming back is not deferred | `ReopenAfterRetireTest.ADeviceComingBackWaitsForTheRetirement` |
| Every `Opened` creates a model | `ReopenAfterRetireTest.OnlyTheOpeningCameraCreatesAModel`, and two others |
| A camera with no device to use does not stop the daemon | `StopEndsTest.ACameraThatFailsToOpenStopsTheDaemonAsAFailure` |
| A failure exits as a success | `StopEndsTest.AFailedModelStopsTheDaemonAsAFailure`, `ACameraThatFailsToOpenStopsTheDaemonAsAFailure` |
| Every join or every leave changes the subscription (`ApprovedFeed`) | `SubscriptionMatchesTest.OnlyTheFirstJoinAndTheLastLeaveCount` |
| A denied permission or a failed start stops the daemon (#52) | `StopEndsTest.ADeniedPermissionDoesNotStopTheDaemon`, `AFailedStartDoesNotStopTheDaemon` |
| `CameraController::OnOpenFailed` treats every reason as `kNoDevice`, or `Retry` does not clear `blocked_` | `RetriesTest` in `test/camera_controller_test.cc` (the same rows `CameraLifecycle`'s own table lists) |

The model does not cover the OS camera itself, which `CameraLifecycle`
models, nor the secrecy of the token files, which only file permissions
give.

## `UserService`: the menu turns facepalm on and off

Models #37's start from the menu, as `src/user_service.cc` implements it.
**Turn On** is the person's consent: it approves the bundled MCP adapter,
writes a launchd LaunchAgent (macOS) or a systemd `--user` unit (Linux) that
runs the daemon with `--consent`, and loads it. **Turn Off** unloads it,
which stops the daemon, and removes the file, so the next login does not
start it. The service manager restarts a daemon that failed (KeepAlive's
`SuccessfulExit = false`, `Restart=on-failure`) and not one that stopped
cleanly. The daemon's own stop is `DaemonAdmission`'s: one fair step here.
The service manager and a stopping daemon are fair; the person, failures
and logging out are not.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `RunsOnlyWhenOn` | The daemon, which holds the person's consent, runs only while they have it turned on. | `RunsOnlyWhenOnTest` in `test/user_service_test.cc` |
| `FileMatchesMenu` | The service's file is installed exactly while it is turned on. | `FileMatchesMenuTest` |
| `TurnOffStops` | Liveness: turning off stops the daemon. | `TurnOffStopsTest` |
| `OnRuns` | Liveness, and something must happen: turned on, in a session, the daemon runs, and runs again after it fails. | `OnRunsTest`; `LaunchAgentPlistTest` and `SystemdUnitTest` for the restart policy |

TLC found one design gap while the spec was written: turning on again while
the previous daemon was still stopping left it loaded but never started, as
its clean exit is not restarted. `GuardOffWaits` closes it: `launchctl
bootout` and `systemctl --user disable --now` return only once the daemon
has exited, and the menu runs one command at a time.

| Mutation | Broke |
|---|---|
| `GuardUnloadOnTurnOff = FALSE`: turning off leaves the service loaded | `RunsOnlyWhenOn`, 4 states; with it left out, `TurnOffStops` |
| `GuardRemoveOnTurnOff = FALSE`: turning off leaves the file | `FileMatchesMenu`, 3 states; with it left out, `RunsOnlyWhenOn` at the next login |
| `GuardRestartOnFailure = FALSE`: a failed daemon stays down | `OnRuns` |
| `GuardOffWaits = FALSE`: turning on while the old daemon stops | `OnRuns` |
| `GuardNoRestartOnStop = FALSE`: a clean stop is restarted too | Nothing: a clean stop comes only from turning off or logging out, which unload the service, so there is nothing to restart |
| `Start` never enabled | `OnRuns` |
| No fairness on `Start` | `OnRuns` |
| No fairness on `Exit` | `TurnOffStops` |
| `Fail` writes a state outside the type | `TypeOK`, 4 states |

The same mutations, made to the code, fail the tests that stand for the
properties they break:

| Mutation to `UserService` | Fails |
|---|---|
| Turning off leaves the file | Every `TurnOffStopsTest`, `RunsOnlyWhenOnTest.AFileThatWillNotGoIsReported` |
| A service that will not load keeps its file | `FileMatchesMenuTest.AServiceThatWillNotLoadLeavesNoFile`, `OnRunsTest.TurningOnSaysWhichStepFailed` |
| A service still running after the unload counts as off | `RunsOnlyWhenOnTest.AServiceThatWillNotStopStaysOn` |
| The agent restarts after a clean stop; the unit never restarts | `LaunchAgentPlistTest.RunsTheDaemonWithConsentAndRestartsOnFailure`; `SystemdUnitTest.RunsTheDaemonWithConsentAndRestartsOnFailure` |
| Turning on approves no adapter | Every `OnRunsTest`, `FileMatchesMenuTest.AServiceThatWillNotLoadLeavesNoFile` |
| The service runs the daemon without `--consent` | `LaunchAgentPlistTest.RunsTheDaemonWithConsentAndRestartsOnFailure`, both `SystemdUnitTest` |

The model does not cover launchd or systemd themselves, nor macOS's camera
permission for a LaunchAgent, which only a run on a real Mac shows.

## `CameraStatus`: the camera-in-use indicator

Models #61 item 8's indicator, as `src/daemon_status.cc` implements it. The
daemon publishes whether the camera is in use to a status file beside its
socket (`UserPaths::status`, #89's per-user directory: no second token or
path), rewritten atomically whenever a client joins or leaves
(`DaemonAdmission`'s own transitions, taken as given here) and once more
at startup, so a file a previous crash left stale does not linger. The
menu or tray rereads the file on its own one-second timer, independent of
the daemon.

The model splits in two, each its own config, because TLC found that one
property cannot honestly cover both normal running and a crash: a crash
timed to land between a change and the next poll can make the display
miss that one change for good, correctly, since the camera the display
never caught really did go off in the crash. `CameraStatus.cfg` (no
crash) checks that the display always catches up during normal running.
`CameraStatus_Crash.cfg` adds one crash and checks that the file is never
stale while the daemon is running, which is what the startup republish
is for.

| Config | Properties | Distinct states |
|---|---|---|
| `CameraStatus.cfg` | `CameraOnLeadsToDisplayOn`, `CameraOffLeadsToDisplayOff` | 4 |
| `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning` | 13 |

Each mutation, and what TLC found:

| Mutation | Config | Broke |
|---|---|---|
| `GuardPublishOnChange = FALSE`: a subscription change is never published | `CameraStatus.cfg` | `CameraOnLeadsToDisplayOn` and `CameraOffLeadsToDisplayOff` |
| `GuardPollRefreshes = FALSE`: the menu never rereads the file | `CameraStatus.cfg` | Deadlock: once a change is pending, nothing is left enabled to observe it |
| `GuardPublishOnRestart = FALSE`: the daemon does not republish at startup | `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning` |
| `GuardPublishOnChange = FALSE` | `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning` too: a later change while running never corrects the file either |

`src/daemon_status.cc` implements `Publish` and `Poll` as the free
functions `PublishCameraStatus` and `ReadCameraStatus`; the same mutations,
made to the code, fail the tests named for them:

| Mutation to `DaemonStatus` | Fails |
|---|---|
| `PublishCameraStatus` never replaces the file | `PublishTest.WritesOn`, `PublishTest.WritesOff`, `PublishTest.ReplacesWhatWasThereAtomically` |
| The temporary file is left in place rather than renamed | `PublishTest.ReplacesWhatWasThereAtomically` |
| `ReadCameraStatus` reads a missing file as on rather than off | `ReadTest.AMissingFileIsOff` |
| The daemon never republishes at `Engine::Start` | Checked by reading `tools/daemon/main.cc`, which has no unit test of its own (`main.cc` is wiring only, as its other callers are); `Start` calls `PublishStatus(false)` before `service_.Start()`, naming the action it stands for |

`ReadTest.APublishFromAnotherThreadIsSeenWithinTwoSeconds` is the
acceptance check outside the model: a real thread publishes after a short
delay, and a real clock bounds how long a poller may take to see it,
standing for the menu's own one-second timer settling inside the two
seconds the indicator is asked to.

## `PreviewLease`: the camera preview's lease

Models #61 item 8's camera preview, as `src/preview_lease.cc` implements it.
`DaemonAdmission.tla`'s own camera open and close are taken as given
(`CameraOpens`, `CameraCloses`); this checks only the lease that gates
whether a frame is ever encoded or published at all, which a camera
session counter (`camera_session`) and the session a grant names
(`started_session`) enforce: they must match for streaming to be on, so
a camera that closed and reopened needs a fresh `StartPreview` from the
menu. No image reaches a client but the menu through this: the gate
decides only whether to publish to the menu's own local channel, never
whether the approved-token service every other client reaches carries
one (it never does, either way).

| Property | What it says | Test |
|---|---|---|
| `EventuallyOpens` | Something must happen: the camera can always eventually open. | `StartTest.NeedsTheCameraOpen` and the suite overall, through `PreviewLease::CameraOpened` |
| `NeverStreamsWithoutCamera` | The lease never grants streaming while the camera is closed. | `StartTest.NeedsTheCameraOpen` |
| `NeverStreamsWithoutAFreshStart` | Streaming is on only for the session it was last granted in. | `NeverStreamsWithoutAFreshStartTest.AReopenedCameraNeedsANewStart` |
| `StreamingMatchesFormula` | `streaming` always equals `camera = open /\ started_session = camera_session`, checked at every state rather than only eventually (see the mutation table: a liveness version of this let an adversarial schedule hide a real bug). | Every `PreviewLeaseTest` in `test/preview_lease_test.cc` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardStartNeedsCamera = FALSE`: `StartPreview` is let through with the camera closed | `NeverStreamsWithoutCamera` |
| `GuardStreamingTracksLease = FALSE`: `streaming` is never recomputed | `StreamingMatchesFormula` |
| `CameraOpens` never enabled (`camera_session < 0`, never true) | `EventuallyOpens` |

An earlier draft of `StreamingMatchesFormula` was a `<>(streaming = TRUE)`
liveness property instead. TLC found a real gap in that version, not in
the design: an environment that always closes the camera again right
after opening it, forever, can keep `StartPreview` from ever winning the
race against `CameraCloses`, so the property failed even with every
guard correct. The state invariant above says the same thing without
needing to win that race, and is what the mutations in the table above
were run against instead.

`src/preview_publisher.cc` implements `PreviewLease`'s caller, following
no spec of its own (it has no lifecycle beyond the lease's): it polls the
menu's request file, drives `CameraOpened`/`Start`/`Stop`, and encodes
and publishes a frame exactly while `IsStreaming()`, tested directly in
`test/preview_publisher_test.cc` with `PreviewLease` run for real (as
`tuning_test.cc` runs the feelings rule for real), since it is already
proved above and has no I/O of its own to mock.

## `FeedWaitRetirement`: canceled wait ownership

The bounded model checks eight states, with one old wait and one successor,
without fairness assumptions. `NoStaleAccess` and `NoStaleAnswer` hold;
disabling `GuardLifetime` and `GuardGeneration` respectively violates them.
A wait holds a cancellation token after its client record is erased; ordinary
disconnect also cancels that wait before the identity can reconnect.
`reaction_feed_test` checks disconnect, forget and waiting; the model abstracts
away the condition variable and operating-system scheduling.

`CameraLifecycle` also invalidates in-flight tickets when the last subscriber
leaves. With `GuardLastLeaveDiscards` false, TLC violates `NoStaleEvent`; the
controller test `LastLeaveInvalidatesTicketsBeforeAReconnect` exercises it.
The model checks 20,864 distinct states under the existing bounds (item 10
of #61 added more variables since this was last counted).

## `TurnExcursion`: one turn per excursion, and a bound on the suppression

After a turn is reported, `GestureReportGate` suppresses later turns, so a
turn and its return are one report (#34). The suppression is bounded (#62):
it ends once the head has held still back near the turn's origin for 750 ms,
or held still anywhere for 2,000 ms, which makes the pose it settled in the
new origin. Yaw is abstracted as near or away from the origin, and time as
how long the head has held still where it is: `held` is 0 while it moves, 1
after 750 ms (`StillMs`), and 2 after 2,000 ms (`SettledAwayMs`). "Still"
means the model reads no gesture and yaw stays within ten degrees of where
the head settled. `MaxMoves` bounds how often the head moves between turns,
which is the model's form of the assumption that the head eventually holds
still; weak fairness on `Hold` and on the two releases says time then passes
and the release happens. Tracking loss (`Lost`) has no fairness.
`TurnExcursion.cfg` checks `MaxMoves = 2`: 26 distinct states, every
property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `ReleaseNeedsStill` | A turn is released only once the head has held still for 750 ms. | `GestureReportGateTest.ReleaseNeedsStillSoADriftStartsTheHoldAgain`, `AnotherGestureWhileSettledAwayStartsTheHoldAgain`, `ReturnMustRemainNearForTheFullStillTime` in `test/gesture_reader_test.cc` |
| `NoRearmAway` | A release by return happens back near the origin. | `TurnWaitsForReturnAndStillnessBeforeRearming`, `ReturnDistanceWrapsAroundPi`, `NoRearmFromANonFiniteYaw` |
| `NoRearmOnGlance` | A release away from the origin needs the full 2,000 ms, so a glance and its return stay one turn. | `NoRearmOnGlanceShortOfTheSettledTime`, `EachTurnStartsItsOwnHold` |
| `NoRepeat` | No turn is reported while one is latched. | `SuppressedTurnsDoNotMakeTheGeneralGateStill`, `TurnLatchDoesNotSuppressOtherGesturesAndResetClearsIt` |
| `TurnRearms` | Something must happen: every latched turn is released, wherever the head settles, so a later turn can be reported (#62). | `TurnRearmsOnceTheHeadHasSettledAway`, `TheSettledPoseWrapsAroundPi` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardSettleAway = FALSE`: only a return releases, the design before #62 | `TurnRearms`: a turn, then the head held still away, stays latched forever |
| `GuardHoldAway = FALSE`: a release away needs no hold | `ReleaseNeedsStill`, 6 states |
| `SettleAway` needs `held >= 1`, not `held = 2 \/ ~GuardHoldAway` | `NoRearmOnGlance`, 13 states |
| `GuardReturn = FALSE`: a return release needs no return | `NoRearmAway`, 13 states |
| `GuardRepeat = FALSE`: a turn is reported while one is latched | `NoRepeat`, 3 states |
| `Settle` needs no stillness | `ReleaseNeedsStill`, 12 states |
| `Hold` never enabled | `TurnRearms` |
| No fairness on `Hold` | `TurnRearms` |
| No fairness on `Settle \/ SettleAway` | `TurnRearms` |
| No fairness at all | `TurnRearms` |
| `Hold` writes a `held` outside the type | `TypeOK`, 19 states |

`src/gesture_reader.cc` implements the spec in `GestureReportGate`:
`SuppressTurn` names `Away`, `Return`, `Hold`, `Settle` and `SettleAway`, and
the turn's latch in `Update` names `Turn`. The same mutations, made to that
code, fail the tests that stand for the properties they break:

| Mutation to `GestureReportGate` | Fails |
|---|---|
| A head held still away never releases the turn (the design before #62) | `TurnRearmsOnceTheHeadHasSettledAway`, `TheSettledPoseWrapsAroundPi`, `ReleaseNeedsStillSoADriftStartsTheHoldAgain` |
| The settled time is 750 ms, not 2,000 | `NoRearmOnGlanceShortOfTheSettledTime`, and three others |
| A drift past ten degrees does not start the hold again | `ReleaseNeedsStillSoADriftStartsTheHoldAgain` |
| A gesture other than a turn does not start the hold again | `AnotherGestureWhileSettledAwayStartsTheHoldAgain` |
| A new turn keeps the previous turn's hold | `EachTurnStartsItsOwnHold` |
| Yaw distances do not wrap at pi | `ReturnDistanceWrapsAroundPi`, `TheSettledPoseWrapsAroundPi` |
| A return release ignores the origin | `ReturnMustRemainNearForTheFullStillTime`, and four others |

A new turn also clears the return's 750 ms, but no test can tell: the
general gate's own 750 ms of stillness starts on the same frame, so a stale
return hold cannot report a turn any sooner. The model does not prove pose
accuracy or the model's classification.

## `LookAway`: looking away is reported once a look

Models #35's looking-away rule, `LookAwayDetector` in `src/look_away.cc`.
The head's pose is abstracted against the resting pose `ExpressionTracker`
learns: `rest` within half the threshold, `between` up to it, `away` past
it. `held` says the head has stayed away for `hold_ms`. The resting pose
learns throughout (`Absorb`): quickly in the first three seconds of a track
(`Learn`), and over about twenty seconds after, so while it is learned only
a look already reported is absorbed before `hold_ms`. A gap of more than two
seconds starts learning again (`Gap`). `MaxMoves` and `MaxGaps` bound how
often the head moves and tracking restarts, the model's form of "the head
eventually holds a pose"; weak fairness on `Hold`, `Absorb`, `Report` and
`Rearm` says time passes and the rule acts. `Learn` has no fairness: no
property needs it. `LookAway.cfg` checks `MaxMoves = 3`, `MaxGaps = 1`: 134
distinct states, every property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `NoReportWhileLearning` | Nothing is reported while the resting pose is being learned. | `NoReportWhileLearningTest.NothingIsReportedWithoutARestingPose` and `AGapClearsAHoldInProgress` in `test/look_away_test.cc`; `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose`; `ExpressionsTest.HasNoRestingPoseWhileItIsLearned` and `HasNoRestingPoseAfterAGapUntilItIsLearnedAgain` |
| `NoReportBeforeHold` | A nod or a shake, away for less than `hold_ms`, is not reported. | `NoReportBeforeHoldTest`; `LookAwayReaderTest.NodsAreNotALookAway`; `LookAwayReplayTest.RecordedNodsAreNotReported` |
| `OncePerLook` | One report per look away. | `OncePerLookTest`; `LookAwayReplayTest.EachRecordedLookDownIsReportedOnce`, `EachRecordedTurnAwayIsReportedOnce` |
| `RearmOnlyAtRest` | The next look is reported only after the head comes back within half the threshold. | `RearmOnlyAtRestTest` |
| `LookReported` | Something must happen: a learned head held away is reported. | `LookReportedTest`; `LookAwayReaderTest.ReportsATurnAwayHeldForTheHoldTimeOnce`, `ReadsPitchFromTheFaceToo` |
| `LookRearms` | The suppression after a report is bounded: wherever the head settles, the next look can be reported. | `LookRearmsTest`; `ExpressionsTest.TheRestingPoseFollowsAPoseHeldForAMinute` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardLearned = FALSE`: a hold runs while the resting pose is learned | `NoReportWhileLearning`, 4 states |
| `GuardHold = FALSE`: no hold time | `NoReportWhileLearning`, 3 states; with it left out, `NoReportBeforeHold`, 3 states |
| `GuardOnce = FALSE`: a look is reported on every step away | `OncePerLook`, 6 states |
| `GuardHysteresis = FALSE`: rearmed anywhere inside the threshold | `RearmOnlyAtRest`, 7 states |
| `GuardAbsorb = FALSE`: the resting pose never follows a held pose | `LookRearms`: a head that stays away stays reported forever |
| `Report` never enabled | `LookReported` |
| `Rearm` never enabled | `LookRearms` |
| No fairness on `Hold`, `Absorb`, `Report` or `Rearm` | `LookReported` or `LookRearms` |
| `Rearm` writes a `rearmedAt` outside the type | `TypeOK`, 7 states |

`src/look_away.cc` implements the spec, and its comments name the actions.
`Absorb` is `ExpressionTracker`'s resting pose, in `src/expressions.cc`, and
`Learn` and `Gap` are its learning, which `RestingPose()` reports as no
resting pose. The same mutations, made to that code, fail the tests that
stand for the properties they break:

| Mutation | Fails |
|---|---|
| Away is measured from straight ahead while learning | `NoReportWhileLearningTest.NothingIsReportedWithoutARestingPose`, `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose`, `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce` |
| `RestingPose()` is given while learning | `ExpressionsTest.HasNoRestingPoseWhileItIsLearned`, `HasNoRestingPoseAfterAGapUntilItIsLearnedAgain`, `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce` |
| No hold time | `NoReportBeforeHoldTest` (both), `LookAwayReaderTest.NodsAreNotALookAway`, and eleven others |
| Reported on every frame away | `OncePerLookTest`, `LookAwayReplayTest.EachRecordedLookDownIsReportedOnce`, and six others |
| Rearmed at the threshold, not half of it | `RearmOnlyAtRestTest` |
| The resting pose stops following once learned | `ExpressionsTest.TheRestingPoseFollowsAPoseHeldForAMinute`, and two others |
| Never reported | `LookReportedTest` (the three that report), and twelve others |
| Never rearmed | `LookRearmsTest` (both), and the two recorded-look tests |
| Pitch is not read | `LookReportedTest.LookingDownCountsAsAway`, `LookAwayReaderTest.ReadsPitchFromTheFaceToo`, and three others |
| Yaw does not wrap at 180 degrees | `LookReportedTest.YawIsComparedAcrossTheWrapAt180Degrees` |
| The reader judges against straight ahead, not the learned resting pose | `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose` |

The model does not prove pose accuracy, nor that 15 degrees and a second
suit anyone but the one person whose recordings tuned them.

## `CameraLifecycle`: when the camera may be on

Models consent, client subscriptions, the camera device, frames in flight,
and the helper process dying and restarting. The OS can refuse or take away
the device. Two facts are kept apart, as the code keeps them apart (#38):
`camera` is what the controller believes, and `capturing` is whether the
capture device runs; likewise `device` is whether the default (built-in)
camera is attached, and `usable` whether the controller believes a camera is.
Opening is a start task on the session queue, which can run late, after a
permission prompt; closing makes it stale. A frame is copied when captured
and given its consent ticket later, so it records whether its capture has
ended (#40). Each of the design's protections is a `Guard` constant, so a
mutation sets one to `FALSE` in a copy of the config.

Item 10 of #61 adds choosing the camera and its two findings (#13): a second
device, `otherDevice`, such as a Continuity Camera, and `ChosenOther`, the
person's choice, fixed for a run since a changed choice takes effect only on
the daemon's next start. It also adds retrying a denied permission or a
failed start (#52): `permitted` and `startWorks` stand for the two causes
`Start` can fail apart from the device, and `blocked` latches until `Retry`
clears it. `CameraLifecycle.cfg` chooses the default device; the symmetric
`CameraLifecycle_ChosenOther.cfg` chooses the other one.

| Property | What it says | Test |
|---|---|---|
| `OpenOnlyWithConsent` | The camera is opening or open only while consent holds. | `OpenOnlyWithConsentTest` in `test/camera_controller_test.cc` |
| `OpenOnlyWhenSubscribed` | The camera is opening or open only while a client is subscribed. | `OpenOnlyWhenSubscribedTest` |
| `NoEventWithoutConsent` | No reaction is delivered after consent is revoked. | `NoEventWithoutConsentTest` |
| `NoEventToStranger` | A reaction reaches only a subscribed client. | `NoEventToStrangerTest` |
| `DeadHelperHoldsNoCamera` | A dead helper holds no camera. | `DeadHelperHoldsNoCameraTest`: a restarted helper is a new controller, which starts closed |
| `CapturesOnlyChosenDevice` | The device captures only while the device the person chose is attached (#13). | `SessionCameraTest.IgnoresAnotherCameraDisconnecting`, `IgnoresAnotherCameraConnecting`; held to the spec by reading for `AvFoundationSession::Configure` and `V4l2Session::Configure`'s own identity match, which `V4l2SessionTest.ConfigureMatchesTheChosenDeviceAmongSeveral` and `ConfigureGivesNothingWhenNoNodeMatchesTheChoice` exercise directly |
| `DeviceLost`/`DeviceBack`/`OtherDeviceLost`/`OtherDeviceBack` on Linux | A camera connecting or disconnecting while the daemon runs reaches `SessionCamera` on Linux too, the way AVFoundation's own notifications already do on macOS (#111). | `V4l2SessionTest.StartWatchingWiresHotplugCallbacksToTheGivenOnes` holds `V4l2Session` to asking the given `HotplugMonitor` to watch with exactly the callbacks it was given, once asked to (`ConstructingDoesNotStartWatchingUntilToldTo`, #115); `V4l2HotplugTest`'s `ANewCameraFiresOnConnected`, `AKnownCameraDisconnects` and the rest hold `V4l2InotifyMonitor`, the real watch over inotify, to finding and reporting a node's id the same way `Configure`/`List` do, with a node `IN_CREATE` could not yet open retried on `IN_ATTRIB` (`AnAttribAfterACreateThatCouldNotOpenRetriesAndConnects`, #115) |
| `CanOpen` | Something must happen: a wanted, closed camera can start opening, once neither the device nor a denied permission or failed start (#52) stands in the way. | `WantedOpensTest`, `RetriesTest` |
| `RevokingCloses` | Revoking consent leads to the camera being closed. | `RevokingClosesTest` |
| `LastLeaveCloses` | No subscribers leads to the camera being closed. | `OpenOnlyWhenSubscribedTest.TheLastClientLeavingCloses` |
| `WantedOpens` | From every point, consent, a subscriber, the chosen device, the helper, the OS permission and whatever else `Start` needs, all held, lead to the camera being open (#45, #52). | `WantedOpensTest`; for a camera plugged in after none was found (#39), `SessionCameraTest.ReportsAnyCameraConnectingBeforeOneIsPicked`; for a denied permission or a failed start retried, `RetriesTest.ADeniedPermissionIsRetried`, `AFailedStartIsRetried` in `test/camera_controller_test.cc` |
| `NoCaptureWhenClosed` | The device never captures while the controller believes the camera closed (#38). | `SessionCameraTest.StartsNothingWhenPermissionIsGrantedAfterClose`, `StartsOnlyForTheCurrentAttempt`, `ARefusalAfterCloseReportsNothing` in `test/session_camera_test.cc` |
| `NoStaleEvent` | No event comes from a frame copied before consent was last revoked (#40). | `NoStaleEventTest` in `test/camera_controller_test.cc`; the pipeline's tests check the frame's stamped attempt reaches its ticket |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardOpenNeedsConsent = FALSE`: open without consent | `OpenOnlyWithConsent`, 3 states |
| `GuardRevokeCloses = FALSE`: revoking leaves it open | `OpenOnlyWithConsent`, 5 states |
| `GuardRevokeDiscards = FALSE`: a ticketed frame survives revoking | `NoEventWithoutConsent`, 10 states |
| `GuardLastLeaveCloses = FALSE`: the last client leaving leaves it open | `OpenOnlyWhenSubscribed`, 5 states |
| `GuardEmitToSubscribers = FALSE`: a reaction goes to a client that left | `NoEventToStranger`, 8 states |
| `GuardRestartRechecks = FALSE`: a restart reopens what was open | `OpenOnlyWhenSubscribed`, 6 states |
| `Crash` leaves the camera as it was | `OpenOnlyWhenSubscribed`, 5 states; checked alone, `DeadHelperHoldsNoCamera` |
| `GuardStartChecksAttempt = FALSE`: a start task runs after its attempt was closed | `NoCaptureWhenClosed`, 7 states |
| `GuardDiscoverAnyCamera = FALSE`: with no camera picked, a new one is ignored | `WantedOpens` (#39) |
| `GuardTicketChecksCapture = FALSE`: a frame from an ended capture gets a ticket | `NoStaleEvent`, 13 states |
| `GuardReopens = FALSE`: the camera opens only once | `CanOpen`; with it left out, `WantedOpens`, which the old form, `<>[]Wanted => <>(camera = "open")`, let pass (#45) |
| `GuardChoosesByIdentity = FALSE`: `Start` opens any present device, not the chosen one | `CapturesOnlyChosenDevice`, 5 states (#13); on `CameraLifecycle_ChosenOther.cfg`, losing the unchosen device wrongly closes the chosen one's session, which never reopens since only that device's own `DeviceBack` can clear `usable` again, breaking `WantedOpens` |
| `GuardRetries = FALSE`: a denied permission or a failed start never clears | `WantedOpens` (#52) |
| `Start` never enabled | `WantedOpens` |
| No fairness on `Start`, `Open`, `Closed` or `Retry` | `WantedOpens`; for `Closed`, `RevokingCloses` and `LastLeaveCloses` |
| `Open` also requires no restart yet | `CanOpen`, 5 states |
| `Closed` writes a state outside the type | `TypeOK`, 6 states |

`src/camera_controller.cc` implements the spec, and each of its methods names
the action it implements. The same mutations, made to that code, fail the
tests that stand for the properties they break:

| Mutation to `CameraController` | Fails |
|---|---|
| Opens without consent | `OpenOnlyWithConsentTest`, `WantedOpensTest`, `RevokingClosesTest` |
| Revoking does not start closing | `OpenOnlyWithConsentTest`, `RevokingClosesTest`, `WantedOpensTest` |
| Revoking does not void tickets in flight | `NoEventWithoutConsentTest` |
| The last client leaving does not start closing | `OpenOnlyWhenSubscribedTest` |
| A client that leaves stays subscribed | `NoEventToStrangerTest`, `OpenOnlyWhenSubscribedTest` |
| A new controller opens when consent holds | `DeadHelperHoldsNoCameraTest`, and four others |
| The device coming back does not reopen | `WantedOpensTest` |
| Closing does not reopen when wanted | `WantedOpensTest` |
| A frame from an ended attempt gets a ticket | `NoStaleEventTest.AFrameFromBeforeARevokeAndReopenGetsNoTicket` |
| Closing does not end the attempt | `NoStaleEventTest.EachOpenIsANewAttemptAndClosingEndsIt` |
| Losing an open camera does not end its attempt | `NoStaleEventTest.LosingAnOpenCameraEndsItsAttempt` |
| `OnOpenFailed` treats every reason as `kNoDevice` | `RetriesTest.ADeniedPermissionIsRetried`, `AFailedStartIsRetried`, `ANoDeviceFailureIsNotRetried` (#52) |
| `Retry` does not clear `blocked_`, or does not call `OpenIfWanted` | `RetriesTest.ADeniedPermissionIsRetried`, `AFailedStartIsRetried` |
| `OpenIfWanted` does not check `blocked_` | `RetriesTest.WithoutAScheduleABlockedCameraStaysBlocked` |

`src/session_camera.cc` implements the session side: the start task, with
`GuardStartChecksAttempt`; the camera coming back, with
`GuardDiscoverAnyCamera`; and, with `SupportedFrameRates` now one of the
device's own ranges rather than one collapsed minimum and maximum (#43),
the nearest rate across every range. `GuardChoosesByIdentity` is
`AvFoundationSession::Configure` and `V4l2Session::Configure`'s own job
(#13): `SessionCamera` only asks the configured `CaptureSession` for *a*
device, so a wrong choice there, not in `SessionCamera`, is what the guard
stands for. The Mac capture callback stamps each frame with the attempt as
it starts, and `src/frame_pipeline.cc` hands that stamp to the ticket.
`AvFoundationSession::Stop` waits for the capture queue, so a callback
already queued finishes under the attempt it captured for, and a later
`Start` cannot relabel its frame; no unit test reaches that code, so it is
held to the spec's `Capture` by reading, as `Configure`'s own device choice
and `UseFormat`'s widest-coverage format pick are (#13, #43). On Linux,
`V4l2Session::Stop` joins its capture thread before it closes the device,
and the thread stamps each frame with the attempt it was started for:
`V4l2SessionTest.CopiesEachFrameAsBgraStampedWithItsAttempt` and
`StoppingEndsCaptureWithoutAFailure` in `test/linux/v4l2_session_test.cc`
hold it to `Capture` (#83).

`DeviceLost`, `DeviceBack`, `OtherDeviceLost` and `OtherDeviceBack`
themselves had no Linux implementation at all until #111:
`V4l2Session`'s constructor never received the callbacks
`AvFoundationSession`'s already did, so a camera connecting or
disconnecting while the daemon ran went unnoticed on Linux. It now takes
the same two callbacks, and a `HotplugMonitor&` (`src/linux/v4l2_hotplug.h`)
it asks to call them; `V4l2InotifyMonitor`, the real one, watches
`/dev` with inotify for the same four nodes `Configure`/`List` already
probe (docs/decisions.md), reusing their own open-and-query to find a
node's id. `StartWatchingWiresHotplugCallbacksToTheGivenOnes` in
`test/linux/v4l2_session_test.cc` holds `V4l2Session` to asking a fake
`HotplugMonitor` correctly; `test/linux/v4l2_hotplug_test.cc` holds
`V4l2InotifyMonitor` itself to the interface, with inotify faked at link
time the same way libv4l2 already is.

The watch itself starts on `StartWatching`, not the constructor (#115):
`hotplug.Start()` begins a thread that can call `on_connected`/
`on_disconnected` as soon as it runs, and those callbacks are Engine's own
(`tools/daemon/main.cc`), closing over sibling members, `camera_` among
them, that Engine's constructor may not have reached yet when `capture_`'s
own constructor would have started that thread. `Engine::Start()` calls
`StartWatching()` once every member is built. The model does not cover
this ordering, only the device states `DeviceLost`/`DeviceBack` and the
rest name; `ConstructingDoesNotStartWatchingUntilToldTo` in
`test/linux/v4l2_session_test.cc` is the regression test, read for by this
table's own `V4l2Session` constructor row below. `V4l2InotifyMonitor` also
now watches `IN_ATTRIB`, retrying a probed node `IN_CREATE` found it could
not open: udev applies a node's permissions some moments after creating
it, so the node may not have been readable yet (#115).

| Mutation to `SessionCamera`, `FramePipeline` or `V4l2Session` | Fails |
|---|---|
| A start for a closed attempt runs | `SessionCameraTest.StartsNothingWhenPermissionIsGrantedAfterClose`, `StartsOnlyForTheCurrentAttempt`, `ARefusalAfterCloseReportsNothing` |
| Only the camera in use counts coming back | `SessionCameraTest.ReportsAnyCameraConnectingBeforeOneIsPicked` |
| The pipeline asks for a ticket without the frame's stamp | Every `FramePipeline` test that captures a frame |
| `SessionCamera::Start` clamps into one collapsed range instead of the nearest of several | `SessionCameraFrameRateTest.AsksForTheNearerOfTwoSeparateRanges`, `HandlesARangeRoundedOutwardFromAFixedRate`, `PicksTheClosestAmongSeveralRanges` (#43) |
| `V4l2Session::Configure` matches any node instead of the chosen device's id | `V4l2SessionTest.ConfigureGivesNothingWhenNoNodeMatchesTheChoice`, `ConfigureMatchesTheChosenDeviceAmongSeveral` (#13) |
| `V4l2Session::Configure`/`List` do not skip a node that does not query | `V4l2SessionTest.ConfigureSkipsANodeThatDoesNotQuery`, `ListSkipsANodeThatDoesNotQuery` |
| `V4l2Session` does not ask the given `HotplugMonitor` to watch, or not with the callbacks it was given | `V4l2SessionTest.StartWatchingWiresHotplugCallbacksToTheGivenOnes` (#111) |
| `V4l2Session`'s constructor starts the watch itself | `V4l2SessionTest.ConstructingDoesNotStartWatchingUntilToldTo` (#115) |
| `V4l2InotifyMonitor` fires `on_connected` for a node already attached when the watch starts | `V4l2HotplugTest.InitialScanProbesAllFourNodesWithoutFiringAnyCallback` (#111) |
| `V4l2InotifyMonitor` does not filter by the four probed node names, firing for an unrelated entry under /dev | `V4l2HotplugTest.AnUnrelatedNameIsIgnored` (#111) |
| `V4l2InotifyMonitor` does not retry a node `IN_CREATE` could not open, on `IN_ATTRIB` | `V4l2HotplugTest.AnAttribAfterACreateThatCouldNotOpenRetriesAndConnects` (#115) |
| `V4l2InotifyMonitor` reopens or refires an already-known node on `IN_ATTRIB` | `V4l2HotplugTest.AnAttribOnAnAlreadyKnownNodeDoesNotReopenOrRefire` (#115) |

## `FramePipeline`: how a frame reaches the face model

Refines `CameraLifecycle`'s `Capture`, `Emit` and `Drop` for #20's pipeline.
Three serial queues share one frame slot. The capture queue copies a frame
into the slot as RGBA, since the frame is gone when the callback returns.
The control queue, `CameraController`, opens and closes the camera and gives
the frame a ticket, or refuses one when the camera is not open. The
inference queue runs its tasks in order: create the model when the camera
opens, detect on a ticketed frame, and destroy the model when the camera
closes. Frames may arrive at any time, even just after closing, and loading
the model and detecting can each fail. In the code, the model is the list of
frame analysers (#34): `FaceAnalyzer`, which runs the face model once a
frame and gives the result to a `FaceReader` for each setting enabled (head
pose, Idiap's CCDbHG head-gesture model, and facial expressions, #46), made
together, run in turn on each frame, and destroyed together. No action or
invariant changes with the expression reader: the pipeline still only ever
sees the one list of frame analysers.

| Property | What it says | Test |
|---|---|---|
| `NoDetectWithoutModel` | Detect runs only on a model that is ready: not before it loads, after it failed to, or once destroyed. | `NoDetectWithoutModelTest` in `test/frame_pipeline_test.cc` |
| `NeverWrittenWhileRead` | The capture callback never writes the slot while detect reads it. | `NeverWrittenWhileReadTest` |
| `AtMostOneInFlight` | At most one frame is between capture and being freed. | `AtMostOneInFlightTest` |
| `CanDetect` | Something must happen: a ticketed frame next in line, with the model ready, can be detected. | `CanDetectTest` |
| `SlotFreed` | The slot is always freed: after a reaction, a refused ticket, a failed detect, or no model. | `SlotFreedTest` |
| `ModelGoneAfterClose` | Closing the camera leads to the model being destroyed. | `ModelGoneAfterCloseTest` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardOneSlot = FALSE`: a frame is copied in while another is in flight | `AtMostOneInFlight`; with it and `TypeOK` left out, `NeverWrittenWhileRead` |
| `GuardFreeOnRefusal = FALSE`: a refused ticket leaves the slot taken | `SlotFreed` |
| `GuardFreeOnError = FALSE`: a failed detect leaves the slot taken | `SlotFreed` |
| `GuardSkipAbsent = FALSE`: detect runs when the model is absent | `NoDetectWithoutModel` |
| `GuardOneQueue = FALSE`: closing destroys the model at once, on the control queue | `NoDetectWithoutModel` |
| `GuardDestroyOnClose = FALSE`: closing never destroys the model | `ModelGoneAfterClose` |
| `DetectStart` never enabled | `CanDetect` |
| `CaptureEnd` writes a slot state outside the type | `TypeOK` |
| No fairness on `Ticket` | `SlotFreed` |
| No fairness on `CreateModel` | `SlotFreed`: a queued `create` stalls the `detect` behind it |
| No fairness on `DetectStart` | `SlotFreed` and `ModelGoneAfterClose` |
| No fairness on `DestroyModel` | `ModelGoneAfterClose` and `SlotFreed` |
| No fairness on `DetectEnd \/ DetectFails` | `SlotFreed` and `ModelGoneAfterClose` |

The capture callback always returns, but no property needs that, so
`CaptureEnd` has no fairness: without it TLC still passes.

`src/frame_pipeline.cc` implements the spec, and each of its methods names
the action it implements. The same mutations, made to that code, fail the
tests that stand for the properties they break:

| Mutation to `FramePipeline` | Fails |
|---|---|
| A frame is copied in while the slot is taken | `AtMostOneInFlightTest` (both), `NeverWrittenWhileReadTest` |
| A refused ticket does not free the slot | `SlotFreedTest.AfterARefusedTicket`, `PixelsWipedTest.AfterARefusedTicket` |
| A failed analyse does not free the slot | `SlotFreedTest.AfterAFailedAnalyse`, `SlotFreedTest.AfterOneAnalyserFailsAndTheOthersRun` |
| No analysers skips without freeing the slot | `SlotFreedTest.WithNoModel` |
| Closing destroys the analysers at once, on the caller's thread | `NoDetectWithoutModelTest.ClosingWaitsForAnAnalyseAlreadyQueued` |
| Closing never destroys the analysers | `ModelGoneAfterCloseTest.ClosingDestroysTheAnalysers`, `NoDetectWithoutModelTest.AnalysesNothingOnceClosed` |
| A ticketed frame's analyse is never queued | `CanDetectTest` (all three), and ten others |
| A frame's pixels are not wiped when its slot is freed | `PixelsWipedTest` (both; the spec does not model the wipe) |

## `ProbeShutdown`: when the camera probe may exit

The probe exits after the inference queue finishes destroying the face model.
MediaPipe uses process-wide state during destruction, so running process exit
handlers at the same time can abort after the capture summary has been printed
(#71).

Given a directory, the probe also records every frame (#41). The capture
queue copies each frame onto the recorder's serial write queue, which saves
its PNG and then its capture time. At most `Capacity` copies wait there; a
frame past that is dropped. Once the model is retired, control queues the
recorder's finish task behind every write, and exits when it has run: with 0
only when every frame the camera captured was saved. No frame arrives once
the camera has closed, since `AvFoundationSession::Stop` waits for its
capture queue, so `Capture` needs the camera still capturing. The probe's
timer always closes the camera, and everything queued runs; a write may save
or fail, so its two outcomes share one fairness condition. The model assumes
each write returns: a disk that never answers leaves the probe waiting.

| Property | What it says | Test |
|---|---|---|
| `ExitAfterRetirement` | The process exits only once the model is retired. | `ModelGoneAfterCloseTest.CompletionFollowsPendingAnalysisAndDestruction` in `test/frame_pipeline_test.cc`; the probe's own order is held by reading |
| `BacklogBounded` | The backlog of copies never passes its bound. | `BacklogBoundedTest` in `test/frame_recorder_test.cc` |
| `ExitAfterDrain` | The process exits only once every accepted frame has been written or has failed. | `ExitAfterDrainTest` |
| `SuccessMeansComplete` | An exit code of 0 means the recording holds every frame the camera captured. | `SuccessMeansCompleteTest`; the probe's exit code is held by reading |
| `EventuallyExits` | Something must happen: the probe exits. | Nothing yet: no test runs the probe, which needs a camera |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `WaitForRetirement = FALSE`: exit while destruction is still queued | `ExitAfterRetirement`, 5 states |
| `GuardBound = FALSE`: every frame is queued | `BacklogBounded`, 4 states |
| `GuardDrain = FALSE`: the finish task runs ahead of the writes | `ExitAfterDrain`, 8 states |
| `GuardReportLoss = FALSE`: exit with 0 whatever was lost | `SuccessMeansComplete`, 9 states |
| `Exit` never enabled | `EventuallyExits` |
| No fairness on any one of `StopCapture`, the writes, `BeginRetirement`, `FinishRetirement`, `Finish`, `FinishRuns` or `Exit` | `EventuallyExits` |
| `Exit` writes a code outside the type | `TypeOK`, 9 states |

`src/frame_recorder.cc` implements `Capture`, the writes and `FinishRuns`,
and `tools/camera_probe/camera_probe.cc` implements `Finish` and `Exit`; each
names its action. The same mutations, made to `FrameRecorder`, fail the
tests that stand for the properties they break:

| Mutation to `FrameRecorder` | Fails |
|---|---|
| No bound: every frame is queued | `BacklogBoundedTest.DropsAFrameWhileTheBacklogIsFull` |
| A dropped frame is not counted | `BacklogBoundedTest.DropsAFrameWhileTheBacklogIsFull` |
| A written frame does not leave the backlog | `BacklogBoundedTest.TakesFramesAgainOnceOneIsSaved`, `TakesFramesAgainOnceOneHasFailed` |
| The summary is taken at once, not behind the writes | `ExitAfterDrainTest.FinishReportsOnlyAfterEveryRecordedFrame`, and five others |
| A failed save counts as saved | `SuccessMeansCompleteTest.CountsAFrameWhoseSaveFailed`, `BacklogBoundedTest.TakesFramesAgainOnceOneHasFailed` |

## `ReactionFeed`: how readings reach each client

Models #36's delivery. facepalm keeps one timeline of readings: each is an
interpreted reaction with the one-line summary facepalm writes, and an id.
Past `Capacity`, the oldest reading is dropped. Each client, an agent session
or AI Pair, has a cursor. A read returns the readings since that client last
looked, whether it comes from a hook after an agent action or from the MCP's
check-in. A client can instead wait for the next reading, which a new reading
or a timeout ends. A client that connects starts with no backlog. The
person's feedback is on a beat's feeling, not on a reading, and
`FeelingFeedback` models it: this spec's rating of a reading, and its
`RateOnlyShown` and `RateOnlyRecent`, went with the 1 to 5 rating (#35).

| Property | What it says | Test |
|---|---|---|
| `NoRepeat` | No reading is shown to the same client twice. | `NoRepeatTest` in `test/reaction_feed_test.cc` |
| `NoSilentGap` | Every reading made while a client was connected, up to its cursor, was shown to it or counted as missed. | `NoSilentGapTest` |
| `NoWaitPastAReading` | A waiting client is never left waiting while a reading it has not seen exists. | `NoWaitPastAReadingTest` |
| `CanShow` | Something must happen: a connected client with a reading it has not seen can be shown it. | `CanShowTest` |
| `WaitEnds` | Every wait ends, with a reading or a timeout. | `WaitEndsTest` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardAdvanceCursor = FALSE`: a read leaves the cursor where it was | `CanShow`, 5 states; with `CanShow` left out, `NoRepeat`, 5 states |
| `GuardReportMissed = FALSE`: dropped readings are skipped without a word | `NoSilentGap`, 6 states |
| `GuardWakeWaiters = FALSE`: a new reading leaves waiters waiting | `NoWaitPastAReading`, 4 states |
| A read shows nothing | `CanShow`, 5 states |
| A read counts an id outside the type as missed | `TypeOK`, 3 states |
| No fairness on `WaitTimesOut` | `WaitEnds`, 4 states |

These were run again on 2026-11-21, once the rating had gone; the state
counts are the counterexamples' lengths.

`src/reaction_feed.cc` implements the spec, and each of its methods names
the action it implements. The same mutations, made to that code, fail the
tests that stand for the properties they break:

| Mutation to `ReactionFeed` | Fails |
|---|---|
| A read leaves the cursor where it was | `NoRepeatTest`, `NoWaitPastAReadingTest.AMissedReadingAlsoReturnsAtOnce` |
| Dropped readings are not counted as missed | `NoSilentGapTest.DroppedReadingsAreCountedAsMissed`, `NoWaitPastAReadingTest.AMissedReadingAlsoReturnsAtOnce` |
| A new reading does not wake waiters | `NoWaitPastAReadingTest.ANewReadingEndsTheWait`, and two others |
| Disconnecting does not wake the client's wait | `WaitEndsTest.DisconnectingEndsTheWait` |
| A wait ended by a disconnect answers once the name reconnects | `WaitEndsTest.DisconnectingEndsTheWaitEvenWhenTheNameReconnects` |
| A read shows nothing | `CanShowTest.AConnectedClientIsShownANewReading`, and twelve others |

A wait's tests require it to end within 5 seconds of a 10 second timeout, so
a missing wake fails them rather than passing late.

## `FeelingBeat`: a reaction read as a feeling

Models #70, item 3 of #61. A client that has just asked the person a question
or made a proposal marks a beat. The window opens at the next reading; when it
ends, the client is told every reading since it last looked, as a read would
tell it, and one interpretation: a feeling, or unknown. Each reading carries
its cue: a nod, a shake, a puzzled face (brows down or narrowed eyes, held), or
none. The generic rule reads one kind of cue as its feeling, and no cue, cues of
different feelings, or a window missing a dropped reading as unknown.
Disconnecting during a beat ends it with no answer. `during` and `lost` are
ghost variables recording what really happened while the beat was open, and
each answer is checked against them as it is given.

The config leaves the puzzled face out: the rule treats it exactly as a nod or
a shake, so two feelings are enough for a conflict, and a fourth cue made the
state space too large to check in CI. A timeline keeping two is the smallest in
which a beat can hold two cues without dropping one; with a timeline of one,
`GuardConflictUnknown = FALSE` passed, because every conflict was also an
incomplete window.

| Property | What it says | Test |
|---|---|---|
| `OneAnswerPerBeat` | Each beat gets at most one answer, and none once its client has left. | `OneAnswerPerBeatTest` in `test/reaction_feed_test.cc` |
| `ReadsOnlyTheBeat` | With the window whole, cues of one feeling give that feeling, never one from a reading made before the beat. | `ReadsOnlyTheBeatTest` in `test/reaction_feed_test.cc` and `test/feelings_test.cc` |
| `UnknownWhenUnclear` | No cue of a feeling, or cues of different feelings, read as unknown. | `UnknownWhenUnclearTest` in both |
| `UnknownWhenIncomplete` | A window with a reading dropped before the beat ended reads as unknown. | `UnknownWhenIncompleteTest` in both |
| `NoSilentGap` | Marking a beat skips no reading: each is shown or counted as missed. | `ReadsOnlyTheBeatTest.AReadingBeforeTheBeatIsToldButNotRead` |
| `NoRepeat` | No reading is shown to the same client twice, the beat's reply included. | `ReadsOnlyTheBeatTest.AReadingAfterTheBeatGoesToTheNextRead` |
| `CanAgree` | Something must happen: a beat in which the person only nodded can end read as agreement. | `CanAgreeTest` |
| `BeatEnds` | Every beat ends, answered or abandoned. | `BeatEndsTest`, `OneAnswerPerBeatTest.DisconnectingEndsTheBeatUnanswered` |

Each mutation, and what TLC found. The invariant mutations ran without
`BeatEnds`, so the check stops at the first violation:

| Mutation | Broke |
|---|---|
| `GuardWindowStart = FALSE`: the beat reads from the client's cursor | `CanAgree`, 5 states; without it, `UnknownWhenUnclear`, 5 states; without both, `ReadsOnlyTheBeat`, 6 states |
| `GuardDroppedUnknown = FALSE`: a window missing a reading is read anyway | `UnknownWhenIncomplete`, 7 states |
| `GuardConflictUnknown = FALSE`: one of several feelings wins | `UnknownWhenUnclear`, 6 states |
| `GuardCloseBeat = FALSE`: a beat stays open after its answer | `OneAnswerPerBeat`, 6 states |
| `GuardCancelOnLeave = FALSE`: disconnecting leaves the beat open | `OneAnswerPerBeat`, 8 states |
| `GuardKeepBacklog = FALSE`: marking a beat moves the cursor past the backlog | `NoSilentGap`, 4 states |
| Every beat answers unknown | `CanAgree`, 4 states |
| A beat answers a value that is not a feeling | `TypeOK`, 6 states |
| The beat's reply leaves the cursor where it was | `NoRepeat`, 6 states |
| No fairness on `EndBeat` | `BeatEnds`, 10 states |

`src/reaction_feed.cc` implements `Mark`, `EndBeat` and `Answer` in
`ReactionFeed::Beat` and `ReactionFeed::Answer`, and `src/feelings.cc` the
rule, `Interpret`. The same mutations, made to that code, fail the tests that
stand for the properties they break:

| Mutation to the code | Fails |
|---|---|
| The window opens at the client's cursor | `ReadsOnlyTheBeatTest.AReadingBeforeTheBeatIsToldButNotRead`, `UnknownWhenIncompleteTest.AReadingDroppedBeforeTheBeatDoesNot` |
| Marking a beat moves the cursor past the backlog | the same two |
| A dropped reading of the window is ignored | `UnknownWhenIncompleteTest.ADroppedReadingMakesTheBeatUnknown` |
| Cues of different feelings: the last wins | `UnknownWhenUnclearTest.CuesOfDifferentFeelingsAreUnknown`, `UnknownWhenUnclearTest.ANodAndAShakeAreUnknown` |
| Disconnecting does not end the beat | `OneAnswerPerBeatTest.DisconnectingEndsTheBeatUnanswered` |
| Every beat reads as unknown | `CanAgreeTest.ANodDuringTheBeatIsAgreement`, and five others |
| A reading ends the beat before its window | `BeatEndsTest.ABeatEndsWhenItsWindowDoes`, and two others |

A beat's tests make their readings once the beat is seen open, inside a 500 ms
window, and fail rather than hang when the beat ends before it is seen open.

## `FeelingFeedback`: the person's feeling, taken once and kept

Models #35, item 4 of #61. A beat's reply carries an interpretation
(`FeelingBeat`); the person picks how they really felt, and the client sends
`feel <id> <feeling>`. facepalm takes it only for an interpretation it gave
that client, the first time, and within five minutes (`Expire` is that time
passing), then counts it in the person's own file, which the daemon alone
writes. `reset feelings` forgets every count. A save writes the counts to a
temporary file, syncs it, renames it over the file, and syncs the folder; one
save runs at a time. Any step can fail, and the daemon can stop at any moment
(`Restart`): a rename the folder sync has not kept may be lost then, and so may
data never synced. The model holds the counts as the set of corrections they
hold, each an interpretation and a feeling: which ones the file and the reads
hold is what matters here, and how they tune a read is `src/tuning.cc`'s, held
by its own tests. A feeling for a beat whose evidence cannot say which reaction
it was about (mixed reactions, or a dropped reading) is taken but counted
nowhere; the model leaves that out.

| Property | What it says | Test |
|---|---|---|
| `OnlyGiven` | A feeling is taken only for an interpretation given to that client, in this connection. | `OnlyGivenTest` in `test/reaction_feed_test.cc` |
| `OncePerInterpretation` | At most one feeling is taken for each interpretation, even when its save failed. | `OncePerInterpretationTest` |
| `OnlyRecent` | A feeling comes within five minutes of the beat. | `OnlyRecentTest` |
| `NoTornFile` | The file is always whole: a crash leaves the old counts or the new. | `AckedKeptTest` in `test/feeling_store_test.cc`, `FeelingStoreFileTest.KeepsPicksInAPrivateFileAcrossRuns` |
| `ReadsTheFile` | The reads use the counts in the file, never ones it failed to save. | `ReadsTheFileTest`, `FeelingStoreFileTest.PicksMadeAtOnceAreAllKept` |
| `AckedKept` | A feeling answered ok stays in the file, through a crash, until a reset. | `AckedKeptTest`, `ReadsTheFileTest.APickRenamedInButNotDurableIsReadButNotAcknowledged` |
| `ResetForgets` | A reset answered ok has cleared every count there was when it was asked. | `ResetForgetsTest` in both |
| `CanLearn` | Something must happen: a fresh feeling from a client with nothing under way is taken. | `OnlyGivenTest.AFeelingForTheClientsInterpretationIsLearned` |
| `Answered` | Liveness: every feel and reset taken is answered, ok or not. | `CanAcceptTest.AClientWaitsAndSaysHowThePersonFelt`, `CanAcceptTest.AClientResetsThePersonsFeelings` in `test/service_test.cc` |
| `TypeOK` | Every variable keeps its type. | none: the C++ types hold it |

The one writer is two locks in the code: the store's mutex, within the daemon,
and `<path>.lock`, held while the store is open, so a second daemon cannot open
the file (`FeelingStoreOpenTest.RefusesALockAnotherWriterHolds`,
`FeelingStoreFileTest.ASecondStoreCannotOpenTheSameFile`, and the integration
test `a second service cannot open the same feelings file`). The model has one
daemon, so `GuardOneWriter` stands for both.

Each mutation, and what TLC found. The invariant mutations ran without
`Answered`, so the check stops at the first violation; the state counts are
the counterexamples' lengths:

| Mutation | Broke |
|---|---|
| `GuardGivenOnly = FALSE`: any client's interpretation takes a feeling | `OnlyGiven`, 3 states |
| `GuardOnce = FALSE`: an interpretation takes a feeling again | `OncePerInterpretation`, 8 states |
| `GuardRecent = FALSE`: a feeling is taken after the time limit | `OnlyRecent`, 5 states |
| `GuardOneWriter = FALSE`: two saves run at once | `ReadsTheFile`, 11 states; without it, `NoTornFile`, 12 states (the other save renames a temporary file this one left part written) |
| `GuardTempFile = FALSE`: a save writes over the file | `NoTornFile`, 5 states; without it, `ReadsTheFile`, 5 states |
| `GuardSyncBeforeRename = FALSE`: the temporary file is renamed unsynced | `NoTornFile`, 6 states (a crash loses its data) |
| `GuardAckDurable = FALSE`: ok once renamed, before the folder sync | `AckedKept`, 9 states (a crash loses the rename) |
| `GuardCommitOnRename = FALSE`: the reads take the counts when a save begins | `ReadsTheFile`, 5 states; without it, `AckedKept`, 20 states |
| `GuardResetEmpties = FALSE`: a reset saves what there is | `ResetForgets`, 14 states |
| `Feel` never takes a feeling | `CanLearn`, 2 states |
| A feel names an interpretation outside the type | `TypeOK`, 4 states |
| No fairness on the save's steps and replies | `Answered`, 11 states |

`src/reaction_feed.cc` implements `Give`, `Expire`, `Feel` and `Reset` in
`ReactionFeed::Beat`, `Expire`, `Feel` and `Reset`; `src/feeling_store.cc`
implements `Begin` to `ReplyError` in `FeelingStore::Learn`, `Forget`,
`Commit` and `Save`; and `src/service.cc` answers the protocol's lines. The
same mutations, made to that code, fail the tests that stand for the
properties they break:

| Mutation to the code | Fails |
|---|---|
| Disconnecting keeps the interpretations | `OnlyGivenTest.AnInterpretationGivenBeforeAReconnectIsNot` |
| A feeling does not use up its interpretation | `OncePerInterpretationTest.ASecondFeelingIsRefused`, and two others |
| A feeling is taken however late | `OnlyRecentTest.NotOnceFiveMinutesHavePassed` |
| No feeling is ever taken | `OnlyGivenTest.AFeelingForTheClientsInterpretationIsLearned`, and five others |
| `Learn` takes no lock | `FeelingStoreFileTest.PicksMadeAtOnceAreAllKept` |
| The lock file is not locked | `FeelingStoreOpenTest.RefusesALockAnotherWriterHolds`, `FeelingStoreFileTest.ASecondStoreCannotOpenTheSameFile` |
| A save writes over the file | `AckedKeptTest.APickIsAcknowledgedOnlyOnceItsSaveIsDurable`, and three others |
| The temporary file is not synced | `AckedKeptTest.APickIsAcknowledgedOnlyOnceItsSaveIsDurable`, `ReadsTheFileTest.APickWhoseSaveFailsIsNotRead` |
| ok once renamed | `ReadsTheFileTest.APickRenamedInButNotDurableIsReadButNotAcknowledged` |
| The reads take a failed save | `ReadsTheFileTest.APickWhoseSaveFailsIsNotRead`, `ResetForgetsTest.AFailedResetKeepsThePicks` |
| A reset saves what there is | `ResetForgetsTest.ForgettingSavesNoPicksAndRestoresTheGenericRead`, `FeelingStoreFileTest.KeepsPicksInAPrivateFileAcrossRuns` |
| The beat ignores the person's picks | `TunedReadTest.TheBeatIsReadAsThePersonsPicksSay`, `TunedReadTest.ABeatWithNoReactionHasEvidenceToo` |

The tuning rule itself is pure computation, outside the model; mutations to it
fail its tests in `test/tuning_test.cc`:

| Mutation to `Tuning` | Fails |
|---|---|
| The person's majority wins without the held-out check | `TuningReadTest.TwoPicksAreNotEnough`, `TuningReadTest.ASplitThatDoesNotBeatTheGenericReadKeepsIt` |
| A plurality is enough for the person's read | `TuningReadTest.ASplitThatDoesNotBeatTheGenericReadKeepsIt` |
| One pick is enough | `TuningReadTest.TwoPicksAreNotEnough` |

The integration test `test/integration/tuning.test.mjs` runs the whole path
against the real feed, store and service: three `annoyance` picks for a frown at
question beats change the next frown's read at a question, not at a proposal;
they last when the service restarts; and `reset feelings` restores `confused`.

## `Service`: the background service and its connections

Models #37's service. It accepts connections on a socket only its user can
open, and gives each its own thread. A connection's first line names its
client and shows its token; only a token the person approved is let in,
and several connections may show the same one, so two sessions of one
approved app are two clients. A client let in is connected to
`ReactionFeed`; the thread then reads, waits for a reading, or gives a
feeling. The peer can hang up at any time. A thread blocked in a wait or a
beat is not reading its socket, so a watcher (`Noticed`) sees the hang-up
and disconnects the client, which ends the wait at once: a client gone does
not keep its session, and the camera, for the up to 60 seconds its wait
could last. Stopping the service closes every connection, and must not
hang on a waiting one, since a wait can last its whole timeout.

A beat (`FeelingBeat`) is a wait to this model: its thread blocks until the
window ends or the client is disconnected, so `Wait` and `Answered` stand for
it too, and `StopWakesWaitersTest.StoppingEndsABeatAndTheConnection` checks
that stopping ends one. The model itself is unchanged.

| Property | What it says | Test |
|---|---|---|
| `UniqueNames` | No two connections speak for the same client. | `UniqueNamesTest` in `test/service_test.cc`; `ConnectingANameAlreadyConnectedIsRefused` in `test/reaction_feed_test.cc`; `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClientOnlyOnce`, `AHangUpDuringABeatDisconnectsTheClientOnlyOnce` (#115) |
| `ApprovedOnly` | Every named connection showed an approved token. | `ApprovedOnlyTest` in `test/service_test.cc` and `test/client_approvals_test.cc`; `approval.test.mjs` |
| `ApprovedAdmitted` | Something must happen: an approved token with a new name is let in, even while another connection shows the same token. | `ApprovedAdmittedTest.TwoConnectionsShowingOneTokenAreTwoClients`; `ApprovedAdmittedTest.ATokenAClientHoldsIsApproved` in `test/client_approvals_test.cc`; `approval.test.mjs` |
| `FeedMatchesConns` | The feed's clients are exactly the named connections still open. | `FeedMatchesConnsTest`; `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClientOnlyOnce`, `AHangUpDuringABeatDisconnectsTheClientOnlyOnce` (#115) |
| `StopWakesWaiters` | Once stopping, no thread is left blocked in a wait. | `StopWakesWaitersTest` |
| `NothingAfterStop` | Once stopped, every connection is closed. | `NothingAfterStopTest`, `NothingAfterStopLateTest` |
| `CanAccept` | Something must happen: a running service can accept a connection into a free slot. | `CanAcceptTest` |
| `StopEnds` | Stopping always finishes. | `StopEndsTest` |
| `HangUpEnds` | A connection whose peer hung up is closed, and its client gone from the feed, even in the middle of a wait or a beat. | `HangUpEndsTest` in `test/service_test.cc`; `AHangUpEndsAnAwaitInAnotherThread` and the other `AwaitHangUp` tests in `test/unix_socket_test.cc`; `approval.test.mjs`'s adapter killed mid-wait |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardUniqueNames = FALSE`: a name already connected is accepted | `UniqueNames`, 5 states |
| `GuardApproval = FALSE`: a token not approved is let in | `ApprovedOnly`, 3 states |
| One connection per token, the design before several clients | `ApprovedAdmitted`, 4 states |
| Every hello refused | `ApprovedAdmitted`, 2 states |
| `GuardLeaveOnClose = FALSE`: a closed connection's client stays in the feed | `FeedMatchesConns`, 5 states |
| `GuardStopWakes = FALSE`: stopping leaves a wait to its timeout | `StopWakesWaiters`, 5 states; with it left out, `StopEnds` |
| `GuardRefuseWhenStopping = FALSE`: connections are accepted while stopping | `StopEnds`: new ones can keep it from finishing |
| `Accept` never enabled | `CanAccept`, in the initial state |
| `Accept` writes a connection state outside the type | `TypeOK`, 2 states |
| `GuardHangUpWakes = FALSE`: a hang-up during a wait is noticed only when the wait ends, the design before | `HangUpEnds` |
| No fairness on `Noticed` | `HangUpEnds` |
| No fairness on `Closed` | `StopEnds` |
| No fairness on `Stopped` | `StopEnds` |
| `Noticed` also removes the name from `feed`, as the code did before #115 | `FeedMatchesConns`, 6 states; `UniqueNames`, 8 states |

`src/service.cc` implements the spec, and each of its methods names the
action it implements. `src/unix_socket.cc` gives it a Unix domain socket,
whose file only its user can open; its tests run on real sockets. The same
mutations, made to the service's code, fail the tests that stand for the
properties they break:

| Mutation to `Service` | Fails |
|---|---|
| A name already connected is accepted | `UniqueNamesTest.ANameAlreadyConnectedIsRefused` |
| Any token is let in | `ApprovedOnlyTest.ATokenNotApprovedIsRefusedBeforeTheFeed`, `AHelloWithoutATokenShowsAnEmptyOne` |
| Any client's file approves any token (`ClientApprovals`) | `ApprovedOnlyTest.ATokenNoClientHoldsIsNotApproved`, `ATokenOfAnotherLengthIsNotApproved`, `ApprovedAdmittedTest.ATokenAClientHoldsIsApproved` |
| A closed connection's client stays in the feed | `FeedMatchesConnsTest.AHangUpTakesTheClientOutOfTheFeed` |
| Stopping does not disconnect the clients, so a wait runs on | `StopWakesWaitersTest.StoppingEndsAWaitAndTheConnection` |
| A connection arriving while stopping is served | `NothingAfterStopLateTest.AConnectionArrivingWhileStoppingIsShut` |
| The watcher does not disconnect a client that hung up | `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClient`, `AHangUpDuringABeatDisconnectsTheClient` |
| A line arriving counts as a hang-up (`UnixStream`) | `UnixSocketTest.ALineArrivingEndsAnAwaitAndIsStillRead` |
| Nothing is ever accepted | Twelve tests, every one that connects |
| `Leave` disconnects a connection's name a second time once another caller already has (#115) | `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClientOnlyOnce`, `AHangUpDuringABeatDisconnectsTheClientOnlyOnce` |

Each test that waits on another thread gives up after 5 seconds, so a
mutation fails it rather than hanging it.

## `HookBroker`: action hooks share an MCP session

`HookBroker.cfg` checks one hook peer and one native request over 20 states.
A canceled peer cannot send queued work (`GuardPeer`), a closed broker cannot
send (`GuardOwner`), and shutdown cannot abandon a dispatched native read
(`GuardRetire`). Each disabled guard violates its corresponding invariant.
The socket and integration tests cover busy sessions, unacknowledged deliveries,
disconnects, and the shared cursor. The model covers safety ordering only.

## `SingleInstance`: the Linux tray's single-instance guard

Models #98's fix, as `tools/menu/main_linux.cc` implements it through
`src/single_instance.h`: launching facepalm-menu while one already runs must
not open a second tray icon. A launch claims a lock (a `QLockFile` under the
user's state directory, `src/user_paths.h`) before it creates a window; one
that finds it already held asks the running instance, over a
`QLocalServer`/`QLocalSocket`, to raise its Settings window when it was asked
for one itself (`--settings`), otherwise it exits without starting a
redundant tray. `QLockFile::tryLock()` is one atomic call, so no other launch
can see the lock as free in the moment between seeing that and claiming it,
and it reclaims a lock left by a process that has since died rather than
wedging every later launch.

`SingleInstance.cfg` checks two launches, each free to run with or without
`--settings`, over 77 states, with no bound on retries after a lost handoff.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `AtMostOnePrimary` | At most one launch ever holds the tray's claim at a time. | `NoInstanceClaimsTheLockAndBecomesPrimary` and `ALostHandoffRetriesAndClaimsTheLock` in `test/single_instance_test.cc`; `RunWithWatcher` and `RunSecondLaunch` in `test/linux_tray_integration.cc` (one `StatusNotifierItem` registered) |
| `LockMatchesPrimary` | The lock names the live primary, and only it. | Covered by the same tests as `AtMostOnePrimary`: a mock that let two callers both win `TryLock` would fail them |
| `LaunchSettles` | Liveness: every launch settles, either as the tray or handed off to one. | Every `ClaimSingleInstanceTest` in `test/single_instance_test.cc` returns a role; `RunSecondLaunch` (the second process exits) |
| `RequestServed` | Liveness, and something must happen: a launch that asked for Settings eventually gets it shown. | `AnInstanceWithSettingsForwardsToTheRunningOne`, `ALostHandoffRetriesAndClaimsTheLock`, `ALostHandoffRetriesAndForwardsToTheNextOwner`; `RunSecondLaunch` (the first instance's window is raised) |

The code bounds a lost handoff's retries at `kMaxSingleInstanceAttempts`,
which the model leaves unbounded (fairness alone proves it settles); when a
handoff keeps losing its owner past that bound, `ClaimSingleInstance` gives
up and exits without having shown anything, which `GivesUpAfterRepeatedLost
Handoffs` covers and the model does not need to, since it is a finite patience
the code chose, not a correctness requirement.

| Mutation | Broke |
|---|---|
| `GuardAtomicClaim = FALSE`: a non-atomic probe splits the lock's check and claim into two steps | `AtMostOnePrimary` and `LockMatchesPrimary`, 7 states: two launches both observe the lock free and both claim it |
| `GuardStaleReclaimed = FALSE`: a dead owner's lock can never be reclaimed | `LaunchSettles` and `RequestServed`: the launch after a crash is wedged in `"trying"` forever |
| `GuardForwardOnSettings = FALSE`: a launch that wants Settings never asks the running instance | `RequestServed`: it exits having never had Settings shown |
| No fairness on the code's own steps | `LaunchSettles` and `RequestServed`: a launch can stay `"trying"` forever even with the lock free |

The same mutations, made to the code, fail the tests that stand for the
properties they break:

| Mutation to `ClaimSingleInstance` | Fails |
|---|---|
| Returns `kPrimary` without calling `TryLock` | `NoInstanceClaimsTheLockAndBecomesPrimary` (mock `.Times` expectations go unmet) |
| Never calls `RequestSettings` when `wants_settings` is true | `AnInstanceWithSettingsForwardsToTheRunningOne`, `ALostHandoffRetriesAndForwardsToTheNextOwner` |
| Does not retry after `RequestSettings` returns false | `ALostHandoffRetriesAndClaimsTheLock`, `ALostHandoffRetriesAndForwardsToTheNextOwner` |
| Retries without bound | `GivesUpAfterRepeatedLostHandoffs` (the mock's `.Times(kMaxSingleInstanceAttempts)` expectation is exceeded) |

The model does not cover `QLockFile`'s or `QLocalServer`'s own behavior, nor
what happens when the state directory itself cannot be created; only a run
on a real Linux desktop, through `test/linux_tray_integration.cc`'s
`RunSecondLaunch`, shows the real tray icon and Settings window.

## `Calibration`: an optional calibration session

Models the short guided session item 12 of #61 adds (#36, #35): the resting
pose, then each screen's centre and four corners, saved to a private profile
only once every target is captured. `step` counts the targets from 0;
`captured` is whether the running step has a pose yet; `camera` abstracts
the frame pipeline that feeds poses in (#38), since a session with no frames
has nothing to capture. `Calibration.cfg` checks `Steps = 3` (the resting
pose and two points) and `MaxOutages = 1` (one camera loss and its
recovery): 30 distinct states, every property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `NoAdvanceWithoutCapture` | Every target needs its own pose before the session moves past it. | `NoAdvanceWithoutCaptureTest` in `test/calibration_test.cc` |
| `NoSaveOnCancel` | Cancelling never saves the session in progress; the old profile is kept. | `NoSaveOnCancelTest` |
| `NoRunningWithoutCamera` | A session only runs while the camera is up: starting needs it, and losing it ends the session. | `NoRunningWithoutCameraTest` |
| `SessionEnds` | Something must happen: a running session always finishes or is cancelled. | `CalibrationWalksEveryTargetTest.TheLastStepFinishesTheSession`, `NoSaveOnCancelTest.CancellingNeverSaves` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardCapture = FALSE`: a step can be finished with no pose captured | `NoAdvanceWithoutCapture`, 27 states |
| `GuardStartCamera = FALSE`: a session can start with the camera down | `NoRunningWithoutCamera`, 17 states |
| `GuardCameraLost = FALSE`: losing the camera does not end a running session | `NoRunningWithoutCamera`, 26 states |
| `GuardCancelDiscards = FALSE`: cancelling saves the session in progress | `NoSaveOnCancel`, 17 states |
| No fairness on `Finish` | `SessionEnds`: a session at its last step, never finished |

`src/calibration.h`'s `CalibrationController::Start`, `Next` and `Cancel`
implement the spec's `Start`, `Advance`/`Finish` and `Cancel`;
`CalibrationSession::Update` is `Capture`, and `OnCameraLost` and
`OnCameraUp` are `CameraLost` and `CameraBack`. `src/calibration_store.cc`
saves the profile atomically (write, sync, rename, sync the directory) to a
private file (mode 0600), exactly once, on `Finish`; a failed save leaves
the capture in place so `Next` can be retried, which the model does not
represent. `OnCameraLost` and `OnCameraUp` are not called by
`src/calibration_reader.cc` (which only ever sees a pose while the camera is
already up): they are for whoever owns the camera's lifecycle in the
daemon's composition root to call, which is outside this change (item 7's
`tools/daemon/main.cc`); until it does, a session only ever sees the camera
up. `src/service.cc` dispatches a `calibrate` request to the controller it
is given, or refuses it when it is given none, which is also outside the
model (a session is always available in it). The model does not prove the
saved profile maps a pose to the right screen; that is
`NearestCalibratedScreen`'s job, covered directly by
`NearestCalibratedScreenTest` and the acceptance test in
`test/calibration_store_test.cc`,
`ACalibratedProfileLocatesAScreenAnUncalibratedGuessMisses`, which replays
fixture head poses through a full start-capture-next-finish-save-load-locate
cycle on a real file.
