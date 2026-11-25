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
| `CalibrationMenu.cfg` | one menu flow against one daemon session, a cold start's camera bounded to 2 retries, every guard on | holds, with `ClosingCompletes` and `CameraWaitResolves` | 78 |
| `CameraLifecycle.cfg` | two clients, one helper restart, a second device, and the OS permission and Start's own readiness, with the default device chosen | holds | 20,512 |
| `CameraLifecycle_ChosenOther.cfg` | the same bounds, with the other device chosen (#13) | holds | 20,512 |
| `CameraStatus.cfg` | one daemon, the camera wanted and released, the menu polling | holds, with `CameraOnLeadsToDisplayOn` and `CameraOffLeadsToDisplayOff` | 4 |
| `CameraStatus_Crash.cfg` | one crash or clean shutdown, one restart (#184) | holds, with `NeverStaleWhileRunning` and `CameraOffLeadsToDisplayOff` | 21 |
| `CameraStatus_OpenFailure.cfg` | one failed camera open and its recovery (#185) | holds, with `OpenFailLeadsToDisplayOff` and `OpenRecoversLeadsToDisplayOn` | 10 |
| `DaemonAdmission.cfg` | three clients, two approved; each client and the OS queue at most two changes; a camera that can be blocked and retried (item 10 of #61, #52) | holds, with `StopEnds`, `LastLeaveCloses` and `JoinOpens` | 25,404 |
| `CameraRetry.cfg` | two generation changes, delay bounded to four units | holds | 1,237 |
| `CameraRetryRetirement.cfg` | consent, demand, repeated loss and delayed retirement | holds, with `Recovery` | 25 |
| `PreviewRecovery.cfg` | Boolean preview, recovery and identity state | holds | 11 |
| `CalibrationPresentation.cfg` | one flow, two frozen targets, at most three point steps | holds; request is not compositor placement | 33 |
| `CalibrationRetirement.cfg` | one attempt and terminal connection release | holds | 4 |
| `FeedWaitRetirement.cfg` | one canceled wait and its successor | holds | 8 |
| `FeelingBeat.cfg` | two clients, three readings, a timeline keeping two, one beat each, cues of a nod, a shake and none, #142's gaze inert (`MaxLooks = 0`, one kind) | holds | 3,815,485 |
| `FeelingBeat_Gaze.cfg` | one client, two readings, up to two Look steps, both kinds and every zone reachable (#142, #144) | holds | 20,352 |
| `FeelingFeedback.cfg` | two clients, two interpretations, two feelings, one reset, one restart | holds | 264,176 |
| `FramePipeline.cfg` | the camera opening twice | holds | 93 |
| `HookBroker.cfg` | one hook peer and one MCP session | holds | 20 |
| `LookAway.cfg` | three moves, one tracking gap | holds | 922 |
| `McpSocket.cfg` | one session, up to two requests, backoff bounded to two ticks | holds, with `LinkRecovers` | 65 |
| `PreviewLease.cfg` | a stale-after bound of two frames | holds, with `MenuDeathEndsStreaming`, `PreviewStreamsWhenWanted`, `PreviewAloneOpensCamera` and `AbandonedPreviewReleasesCamera` | 384 |
| `ProbeShutdown.cfg` | four frames, a backlog of two | holds | 185 |
| `ReactionFeed.cfg` | two clients, three readings, a timeline keeping one | holds | 12,171 |
| `Service.cfg` | three connections, two client names, an approved token and a stranger's | holds, with `StopEnds` and `HangUpEnds` | 9,776 |
| `SingleInstance.cfg` | two launches, three failed handoffs | holds, with `LaunchSettles` and `RequestSettles` | 177 |
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
`MaxBackoff = 2`: 65 distinct states, every property holds.

Issue #129 corrects two things about `LinkDown` found by matching it
against `mcp/session.js`'s own `disconnect()`, and adds a third, new piece:

-   `backoff' = 0` let `ReconnectSucceeds`/`ReconnectFails` fire on the very
    next step, an immediate redial code never allows: `disconnect()` always
    bumps this session's own backoff on every disconnect, not only a
    reconnect attempt that goes on to fail. By the time `LinkDown` can fire
    at all, `backoff` is always exactly `disconnect()`'s own minimum (`link
    = "up"` only comes from `ReconnectSucceeds`, which always resets it
    first), so `backoff' = 1` is a plain correction, not a guarded design
    choice the way `GuardBackoffBounded` is -- no mutation row needed for
    something that is not a choice.
-   `remoteWaiting' = FALSE` asserted the remote side's own wait ends the
    instant the local link drops; nothing in `session.js` tells the remote
    side anything when a link merely drops (`disconnect()` only ever tears
    down the local socket), so this was never true. `LinkDown` now leaves
    `remoteWaiting` unchanged, the same as `Cancel` already does, for the
    reason `RemoteRetires`'s own comment already gives: the remote side's
    "native wait... is still running" and "eventually retires on its own,"
    regardless of which of the two is what ended the local side.
-   `knownId`, `generation`, `learnedGeneration` and `stale` model
    `session.js`'s own `noteInterpretation()`/`knowsInterpretation()`: the
    adapter's own gate against sending a feeling back for an id it no
    longer remembers, since `src/reaction_feed.cc`'s own
    `next_interpretation_` restarts at 1 on every daemon run, so an id
    learned before a lost link could mean an entirely different beat once
    reconnected. `Learn` is `noteInterpretation()`, `Tell` is
    `knowsInterpretation()`'s own gate on `tell_feeling`/`ask_feeling`
    sending `feel <id> ...` at all. `GuardForgetsIdsOnLinkDown` is
    `disconnect()`'s own `knownInterpretations.clear()`; `generation`,
    bumped by every real `LinkDown` regardless of the guard, is the ghost
    that holds it accountable, the same way `menu_alive` holds
    `specs/PreviewLease.tla`'s own freshness check accountable.

Issue #133 asks the model the same question again: `session.js`'s
`knownInterpretations` now stores a `{read, picked}` record for each known
id, not just the fact of knowing it, so `tell_feeling`, `ask_feeling` and a
new `get_feeling` can report the person's pick back to the agent alongside
Facepalm's own read. `Learn`, `Tell` and `GuardForgetsIdsOnLinkDown` model
only whether an id is known at all, never what is stored alongside it, and
`disconnect()`'s own `.clear()` forgets the whole record the same way it
already forgot the whole `Set` entry -- what changed is the payload one
`knownId` now stands for, not when it becomes `TRUE` or `FALSE`. No action,
guard or property needed a change, and no new mutation row applies.

| Property | What it says | Test (`mcp/test/session.test.js`) |
|---|---|---|
| `NoSendAfterClose` | A request never reaches the wire once this session is closed, or while its link is down. | `'active cancellation is terminal and never reconnects'`, `'refused hello and pre-cancelled calls do not send requests'` |
| `OneRequest` | At most one request is on the wire at a time. | `'bounds pending requests and recovers after they settle'` |
| `BackoffBounded` | The wait before a retry never grows past `MaxBackoff`. | `'offline never bounds beyond its own cap'` |
| `LinkRecovers` | Something must happen: a lost link, on a session never itself closed, always comes back up (or the session closes first, giving up the right to retry at all). | `'reconnects once its backoff has run out, without the caller doing anything (#117)'`, `'backoff grows on each failure in a row, and resets once a connection succeeds (#117)'` |
| `NoStaleTell` (#129) | A feeling is never sent back for an interpretation id learned before the link last dropped, so one daemon run's beat can never train on another's. | `'an interpretation id is forgotten once the link is lost, and known again once relearned (#129)'` in `mcp/test/session.test.js`; `'tell_feeling refuses an interpretation this session no longer remembers, unsent (#129)'` in `mcp/test/server.test.js` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardClosed = FALSE`: a request can reach the wire once closed, or while the link is down | `NoSendAfterClose`, 2 states |
| `GuardSingle = FALSE`: a second request can reach the wire while one is already there | `OneRequest`, 10 states |
| `GuardBackoffBounded = FALSE`: a failed reconnect can set a backoff past `MaxBackoff` | `BackoffBounded`, 9 states |
| No fairness on `BackoffTick` and `ReconnectSucceeds` | `LinkRecovers`: a link down forever, stuttering once its own backoff is set |
| `GuardForgetsIdsOnLinkDown = FALSE` (#129): `knownId` is never cleared by `LinkDown` | `NoStaleTell`, 5 states |

`src/daemon_calibration.h`'s own `OnCameraLost`/`OnCameraUp`
(specs/Calibration.tla's `CameraLost`/`CameraBack`) are a close cousin: a
different link, modeled separately since the camera's and the MCP
session's are not the same one going down. The model does not prove the
remote service has retired, that a host displayed a delivery, or anything
about the token a reconnect sends (`mcp/session.js`'s `currentToken`, read
fresh on every attempt so an approval that lands after this session
started reaches it with no restart, is outside the model, a single opaque
`hello`). It also does not model `tell_feeling`'s/`ask_feeling`'s own
message for an unapproved client (`mcp/session.js`'s `UnapprovedError`,
issue #129): that is a plain lookup against one wire string
(`src/service.cc`'s `Greet`), with no lifecycle of its own to check.

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

An open failure, including no device, leaves the daemon running and uses
`Retry` (`GuardRetries`). `canStart` includes the device and all other
requirements: while it is false, an attempt returns a failure; when it
remains true, a wanted camera eventually opens.

For #192, `Fail` records the first failure reason even if a clean stop has
already begun. Retirement retains it and exit reports it. The model uses
`model` and `camera` as representative causes; it does not model the text.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `ApprovedOnly` | Only approved clients are connected. | `ApprovedOnlyTest` in `test/service_test.cc` and `test/client_approvals_test.cc`; `approval.test.mjs`'s stranger |
| `OpenHasConsent` | The camera is opening or open only with consent. | `OpenOnlyWithConsentTest` in `test/camera_controller_test.cc`; `StopEndsTest.StoppingWithTheCameraClosedRetiresAtOnce` checks that stopping revokes it |
| `SubscriptionMatches` | Once the subscription changes are handled, the camera is wanted exactly while some client is connected: one client leaving does not take it from another. | `SubscriptionMatchesTest` in `test/approved_feed_test.cc` and `test/daemon_control_test.cc`; `approval.test.mjs`'s "camera on" and "camera off" |
| `ReopenAfterRetire` | A camera opens only once the previous face model is destroyed. | `ReopenAfterRetireTest` in `test/daemon_control_test.cc` |
| `ExitClean` | The daemon exits only with the camera closed and stopped, and every face model destroyed. | `ExitCleanTest` |
| `FirstFailure` | The first failure remains the cause while stopping. | `StopEndsTest.AFailedModelStopsTheDaemonAsAFailure` |
| `FailureReported` | Exit reports the retained reason. | `StopEndsTest.AFailedModelStopsTheDaemonAsAFailure`, `StopEndsTest.AFailureDuringCleanStopIsReported` |
| `StopEnds` | Liveness: stopping, by the person, a signal or a failed model, always ends with the daemon exiting. | `StopEndsTest` |
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
| `GuardNoDeviceRecovers = FALSE`: a camera with no device stops the daemon | `NoDeviceDoesNotStop` |
| `GuardFailureReason = FALSE`: a later failure overwrites the first | `FirstFailure` |
| `GuardFailureReported = FALSE`: exit drops the reason | `FailureReported` |
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
| `Stop` does not retire a closed camera's model | `StopEndsTest.StoppingWithTheCameraClosedRetiresAtOnce`, `AFailedModelStopsTheDaemonAsAFailure` |
| More than one retirement at a time | `ExitCleanTest.OneRetirementAtATime` |
| `OnClosed` counts the camera closed before the retirement | `ReopenAfterRetireTest.AClosedCameraCountsOnlyOnceTheModelIsGone`, and two others |
| A device coming back is not deferred | `ReopenAfterRetireTest.ADeviceComingBackWaitsForTheRetirement` |
| Every `Opened` creates a model | `ReopenAfterRetireTest.OnlyTheOpeningCameraCreatesAModel`, and two others |
| A failure exits as a success | `StopEndsTest.AFailedModelStopsTheDaemonAsAFailure` |
| Every join or every leave changes the subscription (`ApprovedFeed`) | `SubscriptionMatchesTest.OnlyTheFirstJoinAndTheLastLeaveCount` |
| A denied permission or a failed start stops the daemon (#52) | `StopEndsTest.ADeniedPermissionDoesNotStopTheDaemon`, `AFailedStartDoesNotStopTheDaemon` |
| `Retry` does not clear `blocked_` | `RetriesTest` in `test/camera_controller_test.cc` (the same rows `CameraLifecycle`'s own table lists) |

The model does not cover the OS camera itself, which `CameraLifecycle`
models, nor the secrecy of the token files, which only file permissions
give.

`Subscription`'s own `joined` boolean generalizes beyond `ApprovedFeed`'s
own sessions as of #176: `src/camera_demand.h`'s `CameraDemand` now calls
`DaemonControl::Subscription` through the very same entry point for a
second source, the camera preview's own live request, combining it with
`ApprovedFeed`'s signal first so `Subscription` is still called only on
the combined result's own first-source/last-source edge -- the same
contract `ApprovedFeed` already met alone. No action or invariant here
changed: the model already took `joined` as an opaque admission signal,
never as something only `ApprovedFeed` could produce. `PreviewLease`'s
own section below models the combination itself
(`PreviewAloneOpensCamera`, `AbandonedPreviewReleasesCamera`).

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

`wanted` is that demand edge; `camera` is whether the camera is
genuinely open (`CameraLifecycle.tla`'s own state, also taken as given).
Before #184/#185, the two were the very same variable, which only held
because no crash and no open failure were modeled; the model now splits
them, and splits into three configs rather than two, because TLC found
no one property can honestly cover normal running, a daemon death, and
an open failure all at once:

-   `CameraStatus.cfg` (`ModelsCrash = FALSE`, `ModelsOpenFailure =
    FALSE`): the display always catches up with the camera during
    ordinary running, `wanted` and `camera` flipping atomically, this
    spec's shape before either issue.
-   `CameraStatus_Crash.cfg` (`ModelsCrash = TRUE`, `ModelsOpenFailure =
    FALSE`) adds one daemon death -- a bare `Crash` (killed, or dying
    with no chance to run any more code) or a clean `Shutdown` (a signal
    it has time to react to) -- and checks that the file is never stale
    while the daemon is running (which is what the startup republish is
    for), and now also that a dead daemon's display eventually catches
    up too, with no next run needed (#184). `ModelsOpenFailure` stays
    FALSE here: TLC found that mixing it in broke `CameraOffLeadsTo
    DisplayOff` for a reason that has nothing to do with a crash -- the
    early publish's own honest, momentary disagreement between
    `published` and a not-yet-open `camera`, which `NeverStaleWhileRunning`
    would also misread as staleness.
-   `CameraStatus_OpenFailure.cfg` (`ModelsCrash = FALSE`,
    `ModelsOpenFailure = TRUE`) is where that disagreement is instead
    the point (#185): `OpenFails` and `OpenSucceeds` model a denied or
    failed open and a later retry recovering from one, keyed to
    `open_failed` rather than to raw `camera`, for the reason just
    given.

| Config | Properties | Distinct states |
|---|---|---|
| `CameraStatus.cfg` | `CameraOnLeadsToDisplayOn`, `CameraOffLeadsToDisplayOff` | 4 |
| `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning`, `CameraOffLeadsToDisplayOff` | 21 |
| `CameraStatus_OpenFailure.cfg` | `OpenFailLeadsToDisplayOff`, `OpenRecoversLeadsToDisplayOn` | 10 |

Each mutation, and what TLC found:

| Mutation | Config | Broke |
|---|---|---|
| `GuardPublishOnChange = FALSE`: a subscription change is never published | `CameraStatus.cfg` | `CameraOnLeadsToDisplayOn` and `CameraOffLeadsToDisplayOff` |
| `GuardPollRefreshes = FALSE`: the menu never rereads the file | `CameraStatus.cfg` | Deadlock: once a change is pending, nothing is left enabled to observe it |
| `GuardPublishOnRestart = FALSE`: the daemon does not republish at startup | `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning` |
| `GuardPublishOnChange = FALSE` | `CameraStatus_Crash.cfg` | `NeverStaleWhileRunning` too: a later change while running never corrects the file either |
| `GuardReadChecksAlive = FALSE` (#184): the reader trusts a published "on" with no pid check | `CameraStatus_Crash.cfg` | `CameraOffLeadsToDisplayOff`: with no fairness on `Restart` (a daemon that stops cleanly may never run again), a bare `Crash` or a `Shutdown` whose own publish is itself turned off leaves the display stuck on forever |
| `GuardPublishOnShutdown = FALSE` (#184): a clean stop never publishes off | `CameraStatus_Crash.cfg` | Nothing: `GuardReadChecksAlive` alone already saves every trace this model can construct, since it also catches a bare `Crash`, which this guard never could. Noted here rather than extending the model to simulate the real reason this write still matters anyway -- a pid can be reused by an unrelated process before a reader ever checks it, which this model does not go as far as simulating |
| `GuardPublishOnOpenFailed = FALSE` (#185): `Engine::OpenFailed` never corrects the early publish | `CameraStatus_OpenFailure.cfg` | `OpenFailLeadsToDisplayOff` |
| `GuardPublishOnOpenSucceeds = FALSE` (#185): `Engine::Opened` never republishes on | `CameraStatus_OpenFailure.cfg` | `OpenRecoversLeadsToDisplayOn` |

`src/daemon_status.cc` implements `Publish` and `Poll` as the free
functions `PublishCameraStatus` and `ReadCameraStatus`; the same mutations,
made to the code, fail the tests named for them:

| Mutation to `DaemonStatus` | Fails |
|---|---|
| `PublishCameraStatus` never replaces the file | `PublishTest.WritesOn`, `PublishTest.WritesOff`, `PublishTest.ReplacesWhatWasThereAtomically` |
| The temporary file is left in place rather than renamed | `PublishTest.ReplacesWhatWasThereAtomically` |
| `ReadCameraStatus` reads a missing file as on rather than off | `ReadTest.AMissingFileIsOff` |
| `ReadCameraStatus` reads "on" as on with no pid check at all (#184) | `ReadTest.APublishedPidThatIsDeadIsOff` |
| A pid line missing, not a number, or with trailing characters is read as alive rather than off | `ReadTest.AMissingPidLineIsOff`, `ReadTest.APidLineThatIsNotANumberIsOff`, `ReadTest.APidLineWithTrailingCharactersIsOff` |
| `ProcessAlive` treats a zero or negative pid as alive | `ReadTest.AZeroOrNegativePidIsOff` |
| `ProcessAlive` never treats `EPERM` (a pid that exists but is owned by someone else) as alive | `ReadTest.APidOwnedBySomeoneElseIsStillOn` |
| The daemon never republishes at `Engine::Start` | Checked by reading `tools/daemon/main.cc`, which has no unit test of its own (`main.cc` is wiring only, as its other callers are); `Start` calls `PublishStatus(false)` before `service_.Start()`, naming the action it stands for |
| The daemon's own clean-shutdown callback never publishes off (#184) | Checked by reading `tools/daemon/main.cc`: the `exit_` callback `DaemonControl` is given calls `PublishStatus(false)` before `std::_Exit`, naming `CameraStatus!Shutdown` |
| `Engine::OpenFailed` never publishes off (#185) | Checked by reading `tools/daemon/main.cc`, naming `CameraStatus!OpenFails` |
| `Engine::Opened` never publishes on (#185) | Checked by reading `tools/daemon/main.cc`, naming `CameraStatus!OpenSucceeds` |

`ReadTest.APublishFromAnotherThreadIsSeenWithinTwoSeconds` is the
acceptance check outside the model: a real thread publishes after a short
delay, and a real clock bounds how long a poller may take to see it,
standing for the menu's own one-second timer settling inside the two
seconds the indicator is asked to.

As of #176, "`DaemonAdmission`'s own transitions" above includes the
camera preview's own demand (`src/camera_demand.h`), not only an
approved client's: `camera` here still stands for whatever
`CameraController` believes, whatever is behind it, so no action or
invariant changed, only which code calls `Publish`. `src/menu_state.h`'s
`DecideMenuState` decides what the camera-status line itself reads when
the service is off (`"Facepalm off"`, not `"Camera off"`, since nothing
is publishing to it at all then); that decision has no lifecycle of its
own, so it is covered by `test/menu_state_test.cc` alone, not a spec.

## `PreviewLease`: the camera preview's lease

Models #61 item 8's camera preview, as `src/preview_lease.cc` implements it,
and #114's fix to it: a preview request left behind by a menu that quit
without cleaning up, crashed, or whose session logged out, must not keep
the daemon streaming to nobody. `DaemonAdmission.tla`'s own camera open
and close are taken as given (`CameraOpens`, `CameraCloses`); this checks
only the lease that gates whether a frame is ever encoded or published at
all. No image reaches a client but the menu through this: the gate
decides only whether to publish to the menu's own local channel, never
whether the approved-token service every other client reaches carries
one (it never does, either way).

Issue #176 found that `CameraOpens` and `CameraCloses` being taken as
wholly free, external facts had let a real bug hide in plain sight:
`tools/menu/main.mm`'s `startPreview` and `tools/menu/main_linux.cc`'s
`CameraPreview::Start` refused to even ask unless the camera was already
in use for some other reason, with an error naming an action ("turn on
the camera") no menu item performs, since nothing in this model said the
preview's own request was itself a reason to open the camera. `Demand`
is every reason the camera may be wanted now: `client_demand` stands for
`DaemonAdmission.tla`'s own admission (an approved client; still taken as
given, still proved there, now an externally toggled fact the way that
spec's own `canStart` is, rather than the whole of what `CameraOpens`
needs), and the preview's own live, fresh request joins it, gated by the
fix itself, `GuardPreviewIsDemand`. `CameraOpens` fires only once
something is actually wanted; `CameraCloses` now waits for every source
to agree nothing is (`~Demand`), needing its own fairness
(`WF_vars(CameraCloses)`) in turn, since nothing else ever required it to
run. In code, `src/camera_demand.h`'s `CameraDemand` is `Demand` itself:
it combines an approved client subscribing (`ApprovedFeed`'s own
first-join/last-leave, unchanged) with the preview's own request, polled
on a timer of its own (`tools/daemon/main.cc`'s
`StartPreviewDemandPoll`, independent of frame capture, which cannot run
before the camera is admitted in the first place) into the one signal
`DaemonControl::Subscription` already needed, firing only on that
combined signal's own edge -- extending the daemon's existing admission
entry point rather than adding a second channel. The menu side still
needs a grace period of its own (`kPreviewCameraGraceMisses` in
`tools/menu/main.mm`, `kCameraGraceMisses` in
`tools/menu/main_linux.cc`): `Start` no longer waits for the camera to
already be on, so its own first few polls of the camera-in-use indicator
may still read off while the daemon's admission and status publish catch
up with the request it just made, which a plain instant self-close would
otherwise have misread as the preview's own camera going away again.

Issue #114 adds a second gate: `requested` (the request file's presence)
must also be fresh, `age` (ticks since a live menu's own `StartPreview` -- its
first ask, or its 0.5 s refresh timer, tools/menu/main.mm and
tools/menu/main_linux.cc -- last reset it) having stayed within
`StaleAge`. `menu_alive` is the model's own ghost of whether the menu
that made the request still exists: nothing in the daemon ever reads it
directly, but it is what the two new liveness properties below hold the
freshness check accountable to. While the menu is alive, `StartPreview`
keeps resetting `age` to zero, so a live menu's own request never goes
stale on its own; `age` only climbs once `menu_alive` is FALSE, and only
`Frame` -- the daemon's own per-frame check, `src/preview_publisher.cc`'s
`PreviewPublisher::OnFrame` -- advances it, since that check runs only
while the camera is open and frames are flowing, the only time a stale
request could otherwise still be seen to stream.

An earlier revision also tracked which camera session (a counter bumped
on every `CameraOpens`) a grant was made for, and compared it against the
current one before letting streaming stay on. The #61 item 8 Fable
review (#114) found the comparison had no real counterpart in code
(`src/preview_lease.cc`'s own `camera_session_`/`started_session_` were
set but never read): `CameraOpened` and `CameraClosed` already force
streaming off directly, on every transition, so nothing a later grant's
session number could still be stale against ever survives one. Both the
model and the class dropped the counters rather than inventing a check
for code that already worked without it, which is also why
`NeverStreamsWithoutAFreshStart`, named in an earlier draft of this
table, is gone: its concern is now `NeverStreamsOnAStaleRequest`'s alone.

| Property | What it says | Test |
|---|---|---|
| `NeverStreamsWithoutCamera` | The lease never grants streaming while the camera is closed. | `StartTest.NeedsTheCameraOpen` |
| `NeverStreamsOnAStaleRequest` (#114) | Streaming needs the request both there and fresh. | `NeverStreamsOnAStaleRequestTest.AReopenedCameraNeedsANewStart` |
| `MenuDeathEndsStreaming` (#114) | Liveness: once the menu that made a request is gone, streaming always eventually stops, and -- since only a live menu's own `StartPreview` can turn it back on -- stops for good. This is the acceptance bar's "nothing streams without a live menu window", phrased as a leads-to rather than an instant cutoff: the daemon can only learn of the menu's death indirectly, on its own per-frame check, within the couple of seconds `GuardRequestFresh`'s own grace period already allows for. | `IsFreshPreviewRequestTest.AbandonedLongAgoIsStale` in `test/preview_request_test.cc`; hand-checked against `tools/menu/main.mm`'s `applicationWillTerminate:` and `tools/menu/main_linux.cc`'s `QCoreApplication::aboutToQuit`, which remove a clean quit's own request outright rather than waiting on this grace period at all |
| `PreviewStreamsWhenWanted` (#114) | Liveness, and something must happen: the camera, the request and the menu behind it, all held from some point on, lead to the preview actually streaming. `[](<>[]Wanted => <>streaming)`, as `specs/DaemonAdmission.tla`'s own `JoinOpens` uses the shape, rather than a plain `~>` a momentary ask could satisfy only by chance. | `OnFrameTest.GrantsAndPublishesOnceTheCameraIsOpenAndItIsRequested` in `test/preview_publisher_test.cc` |
| `NeverFrameWithoutStreaming` (#129) | The frame file on disk never outlives the preview: not a crash's leftover at startup, and not a publish still in flight once something else has already stopped streaming. | `OnFrameTest.SkipsPublishingAFrameEncodedBeforeTheCameraClosedMidEncode` in `test/preview_publisher_test.cc`, `PublishTest.SkipsTheWriteOnceStreamingHasStopped` in `test/preview_lease_test.cc` |
| `NeverReadingsWithoutStreaming` (#134) | The readings text beside the frame never outlives the preview either, for the same two reasons as the frame -- a crash's leftover, or a write landing after a transition already cleared it -- since both go through the one `PreviewLease::Publish`. | `PublishTest.SkipsTheWriteOnceStreamingHasStopped` in `test/preview_lease_test.cc`, which already proves the shared gate regardless of what it writes; `tools/daemon/main.cc`'s `Engine::PublishPreviewReadings` calls that same `Publish`, the way `PreviewPublisher::OnFrame` does for the frame, and is otherwise composition, tested the way `clients_`'s and `approved_`'s own forwarding is -- by reading it, not a unit of its own |
| `PreviewAloneOpensCamera` (#176) | Liveness, and something must happen: the camera turns on for an open preview, attributed to the preview itself (`~client_demand` rules out some other client happening to also want it) -- the acceptance bar's first half, and exactly #176's own bug once the guard below is off. | `CameraDemandTest.PreviewDemandAloneIsWanted`; `src/camera_demand.h`'s own doc comment names the admission entry point it drives |
| `AbandonedPreviewReleasesCamera` (#176) | Liveness: a closed or abandoned preview (the window closed, the menu crashed, or the lease simply expired) never keeps the camera on by itself -- the acceptance bar's second half, read the same way `MenuDeathEndsStreaming` reads "abandoned". | `CameraDemandTest.TheLastSourceLeavingReleasesIt`, `CameraDemandTest.OneSourceLeavingWhileTheOtherStillWantsItStaysOn` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardStartNeedsCamera = FALSE`: `StartPreview` is let through with the camera closed | `NeverStreamsWithoutCamera`, 2 states |
| `GuardStreamingTracksLease = FALSE`: `streaming` is never recomputed | `PreviewStreamsWhenWanted`: nothing ever grants, 3 states |
| `GuardRequestFresh = FALSE` (#114): streaming never checks the request's age | `MenuDeathEndsStreaming`: a request a dead menu left behind streams forever once granted |
| No fairness on `Frame` (#114): the daemon's own per-frame check is never required to run | `PreviewStreamsWhenWanted` (nothing is ever forced to grant) and `MenuDeathEndsStreaming` (nothing is ever forced to age a stale one away) |
| `GuardPublishChecksStreaming = FALSE` (#129): `Publish` writes the frame unconditionally | `NeverFrameWithoutStreaming`, 46 states; `PublishReadings` shares the same guard (#134), so `NeverReadingsWithoutStreaming` breaks too, in fewer steps (21 states), since it has no `pending_publish` of its own to wait on first |
| `GuardStartClearsFrame = FALSE` (#129): a crash's leftover frame is never cleared at startup | `NeverFrameWithoutStreaming`, violated by the initial state itself; `readings_exists`' own `Init` shares the same guard (#134), so `NeverReadingsWithoutStreaming` is violated by the initial state too |
| No fairness on `Publish` (#129): a frame once committed to is never required to actually publish | `PreviewStreamsWhenWanted` and `MenuDeathEndsStreaming`, the same way missing `Frame` fairness does: `Frame` cannot run again, by its own `~pending_publish` guard, while a publish stays forever outstanding |
| `GuardPreviewIsDemand = FALSE` (#176): the preview's own request never counts as demand | `PreviewAloneOpensCamera`: with no approved client ever subscribed, nothing ever opens the camera, reproducing #176 exactly |
| No fairness on `CameraCloses` (#176): the camera's own admission is never required to re-check `Demand` | `AbandonedPreviewReleasesCamera`: with nothing holding the camera open but a dead menu's own stale request and the fresh check already caught by `GuardRequestFresh` above, the camera simply stays open forever once opened, `CameraCloses` never forced to run even though always enabled -- leaving `CameraCloses` wholly unconditioned and fair instead, rather than waiting for `~Demand`, was tried first and rejected: it let the camera close for an unrelated reason quietly satisfy this property even with `GuardRequestFresh` broken, masking the very finding that property exists to catch |

Issue #129 adds a third gate: whether the frame file itself, on disk, is
ever left behind once the preview is not streaming. `frame_exists` and
`pending_publish` model `PreviewPublisher::OnFrame`'s own two steps --
deciding to encode a frame (`Frame`, which now also carries
`pending_publish' = streaming'`) and then publishing it (`Publish`,
its own action so `CameraOpens`, `CameraCloses`, `StopPreview` and a
later `Frame`'s own revoke can land in between, the way the control
queue really can during a real encode). The #129 Fable review found
the two were not serialized at all: `OnFrame` checked `IsStreaming()`,
encoded, and only then published, so a camera close landing during the
encode -- which already clears the frame through `PreviewLease`'s own
`streaming_changed` hook -- could still be undone by that same call's
publish going on to write it back once the encode finished.
`PreviewLease::Publish` now re-checks `streaming` itself, under the
same mutex every transition to not-streaming already takes, at the
moment of the write rather than trusting the decision `OnFrame` made
when the encode began; `GuardPublishChecksStreaming` is that re-check.
`GuardStartClearsFrame` is the fix's other half: `Engine::Start` now
removes any frame a previous crash left behind before the daemon can
stream again, matching `CameraStatus!Restart` already doing the same
for the camera-in-use indicator; without it, `Init` is free to start
with one already sitting there.

Issue #134 adds a second file beside the frame: the text the preview
shows, the readings as they happen and the latest beat's feeling.
`readings_exists` models it, written by `src/preview_readings.h`'s own
line-building, published through the very same `PreviewLease::Publish`
`src/preview_publisher.cc` already calls for the frame -- one mutex, one
method, called twice over -- which is why `readings_exists` shares
`GuardPublishChecksStreaming` and `GuardStartClearsFrame` rather than
needing guards of its own, and why `PublishReadings` needs no
`pending_publish` of its own either: unlike encoding a frame, nothing in
`src/preview_readings.h` ever releases the lock between building the
line and writing it, so there is no gap for `CameraOpens`, `CameraCloses`
or `StopPreview` to land inside, the way there is between `Frame`
deciding to encode and `Publish` writing the result. `PublishReadings`
is therefore modeled as one atomic step, the decision and the write
together, with no counterpart to `Frame` of its own.

`StreamingMatchesFormula`, an earlier draft's exact `streaming = ...`
state invariant checked at every step rather than only eventually, is
gone along with the session counters: without them, the one step
between `CameraOpens` (which forces `streaming` off directly, matching
`CameraOpened()`) and the next `Frame` (which alone re-grants it) is a
real, if momentary, gap no formula checked at every single state can
state as an equality -- nothing outside this class ever observes it,
since `PreviewPublisher::OnFrame` is the only caller of `IsStreaming()`,
`Start()` and `Stop()`, and it always corrects it within the very call
that would otherwise see it. `GuardStreamingTracksLease = FALSE` is
still caught just as surely, now by `PreviewStreamsWhenWanted`: an
earlier draft of that same liveness was a plain `<>(streaming = TRUE)`
instead, and TLC found a real gap in that version, not in the design:
an environment that always closes the camera again right after opening
it, forever, can keep `StartPreview` from ever winning the race against
`CameraCloses`, so the property failed even with every guard correct.
`[](<>[]Wanted => <>streaming)` says the same thing without needing to
win that race, since `Wanted` must settle, not just occur.

`GuardRequestFresh = FALSE` does *not* make TLC flag
`NeverStreamsOnAStaleRequest` broken, even though it names `Fresh`
directly: with the guard off, `Fresh` itself reads TRUE regardless of
`age`, so the broken `Fresh` appears on both sides and the invariant
stays trivially consistent with it. `MenuDeathEndsStreaming` is what
actually catches it, since it checks against `menu_alive`, a variable
the mutation cannot touch -- the same lesson dropping
`StreamingMatchesFormula` already taught, that a property checked
against the same broken piece it is meant to guard proves nothing.

`EventuallyOpens` (`<>(camera = "open")`, "something must happen, so a
model that can never open it fails this") is gone as of #176, the same
way `StreamingMatchesFormula` went: it was only ever honest while
`CameraOpens` was wholly free, needing no cause at all. Once `CameraOpens`
fires only on `Demand`, the model can no longer open the camera for no
reason, so an unconditional "it always eventually does" would be false on
its face; `PreviewAloneOpensCamera` is the conditional replacement that
also serves as the non-vacuity check `EventuallyOpens` gave, now
attributing the open to its real cause instead of asserting one
unconditionally.

`src/preview_publisher.cc` implements `PreviewLease`'s caller, following
no spec of its own (it has no lifecycle beyond the lease's): it polls the
menu's request file, drives `CameraOpened`/`Start`/`Stop`, and encodes a
frame while `IsStreaming()`, then hands its own write to
`PreviewLease::Publish` (#129) rather than calling it directly, so the
lease itself is what decides whether the write actually happens. Tested
directly in `test/preview_publisher_test.cc` with `PreviewLease` run for
real (as `tuning_test.cc` runs the feelings rule for real), since it is
already proved above and has no I/O of its own to mock. `src/preview_request.cc`
implements the request's own freshness check (`IsFreshPreviewRequest`,
specs/PreviewLease.tla's `Fresh`), which `tools/daemon/main.cc` wires as
the `requested` callback `PreviewPublisher` polls every frame, in place of
the plain file-existence check #114 found insufficient. As of #176, the
very same check also runs on `Engine::StartPreviewDemandPoll`'s own timer,
independent of frame capture, so `src/camera_demand.h`'s `CameraDemand`
hears about a live request even while the camera is still closed; `main.cc`
has no unit test of its own for this wiring, following `DaemonStatus`'s own
precedent above, since it is composition read directly rather than
exercised.

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
The model checks 20,512 distinct states under the existing bounds (item 10
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

## `LookAway`: looking away is reported once a look, with its kind

Models #35's and #141's looking-away rule, `LookAwayDetector` in
`src/look_away.cc`. The gaze (composed head and eye signal, #137, head pose
alone in terms this model reads either way) is abstracted against the
resting pose `ExpressionTracker` learns: `rest` within half the threshold,
`between` up to it, `away` past it, or `gone` with no face to read at all
(#141). `held` says the gaze has stayed away (or gone) long enough to be
reported at all, rather than read as a glance; `heldLong` says it stayed
long enough past that to read `kAway` rather than `kAverted`; `otherScreen`
and `sawGone` are sticky, set once this look's gaze is ever read inside
another calibrated screen's box or with no face. Reporting now happens
only once, at `Return`, when the gaze (or the resting pose) reaches
`rest`: #141's own merge of the old model's separate mid-look `Report` and
later `Rearm`, since there is no longer an earlier moment to report at or
a later one to rearm on its own. `ReturnByMove` is a conscious move all
the way home, bounded like any other move; `ReturnByAbsorb` is the
resting pose catching up to a gaze that never itself returned (the old
model's `Absorb`), and reports through the very same effect `ReturnByMove`
does, so it can never swallow a look silently -- `RestHasNothingPending`
is what checks that (#141's own finding: the old model's `Absorb` could
report nothing and still be safe, because `Report` had already fired
earlier if it was going to; with reporting moved to `Return`, a quiet
`Absorb` would have nothing left to fire it). Both now also need
`resting = "learned"` to fire at all (#157): `LookAwayDetector::Update`'s
own `!resting` branch holds everything rather than computing a return
against no resting pose, so Return itself must wait for one too, or a
look held across a gap (below) could report before relearning finished.
The resting pose learns throughout: quickly in the first three seconds of
a track, and again after a gap (`Learn`, either way), and over about
twenty seconds once learned. A gap of more than two seconds starts
learning afresh (`Gap`); #157 (found tuning #139's fixtures) is that an
earlier version of this model, and of `LookAwayDetector::Update`'s own
`!resting` branch, cleared `held`, `heldFromBetween`, `heldLong`,
`otherScreen` and `sawGone` right there, silently dropping a look already
open rather than merely holding it until relearning caught up -- neither
`Gap` nor `Learn` clears them now, so a look survives either transition
and is still reported once the gaze (or the newly learned resting pose)
reads as home. `heldAcrossGap` is this issue's own ghost, snapshotting
`held` into a gap; `GapNeverDropsAHeldLook` checks that nothing but
`Return`'s own report ever clears it again. `MaxMoves` and `MaxGaps` bound
how often the gaze moves and tracking restarts, the model's form of "the
gaze eventually holds a pose"; weak fairness on `Hold`, `ReturnByAbsorb`
and `Learn` says time passes, the rule acts, and relearning itself
finishes, so a look stalled behind any of the three is not stuck forever.
`Move`, `HoldLong`, `SeeOtherScreen` and `ReturnByMove` have no fairness of
their own: no property needs it, `ReturnByMove` included, since
`ReturnByAbsorb` alone already carries `LookReported` and `LookRearms`.
`LookAway.cfg` checks `MaxMoves = 3`, `MaxGaps = 1`: 922 distinct states,
every property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `NoReportWhileLearning` | Nothing is reported while the resting pose is being learned. | `NoReportWhileLearningTest` in `test/look_away_test.cc`; `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose`, `OnNoFaceBeforeAnyFaceIsNotYetALook`; `ExpressionsTest.HasNoRestingPoseWhileItIsLearned` and `HasNoRestingPoseAfterAGapUntilItIsLearnedAgain` |
| `NoReportBeforeHold` | A glance, away (or gone) for less than `hold_ms`, is not reported. | `NoReportBeforeHoldTest`; `GoneKindTest.ABriefNoFaceThatReturnsBeforeTheHoldTimeIsAGlance`; `LookAwayReaderTest.ABriefOnNoFaceIsAGlance`; `LookAwayReplayTest.ABriefGestureNoFaceIsAGlance`, `RecordedNodsAreNotReported` |
| `RestHasNothingPending` | Once truly home, nothing from the look that just ended is left running. | `OncePerLookTest`; `LookRearmsTest`; `LookAwayReaderTest.TwoLooksAreTwoReports` |
| `GapNeverDropsAHeldLook` | A look already held when a gap starts is not simply dropped by it. | `GapNeverDropsAHeldLookTest` in `test/look_away_test.cc`; `LookAwayReaderTest.ALookThatOutlastsTheTrackersOwnRelearningStillReportsGone`; `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce`'s own third, wide turn |
| `NoHoldFromBetween` | A hold can only run once the gaze has truly passed the full threshold or gone, never merely from the gap between the two. | `RearmOnlyAtRestTest.HoveringRightAtTheThresholdIsOneLook`; `OtherScreenKindTest.TheBoxIsOnlyReadOnceTrulyAway` |
| `GoneWinsPriority` | Leaving (no face) beats anything else read while the look was open. | `GoneKindTest.GoneWinsOverOtherScreenAndDuration` |
| `OtherScreenWinsPriority` | Short of leaving, another screen's box beats how long the look ran. | `OtherScreenKindTest.OtherScreenWinsOverDurationEvenWhenBackQuickly` |
| `LookReported` | Something must happen: a learned gaze held away eventually moves on or needs relearning. | `LookReportedTest`; `LookAwayReaderTest.ReportsATurnAwayOnceItReturns`, `ReadsPitchFromTheFaceToo` |
| `LookRearms` | The suppression a look being open carries is bounded: it eventually closes. | `LookRearmsTest`; `LookAwayReaderTest.TwoLooksAreTwoReports`; `ExpressionsTest.TheRestingPoseFollowsAPoseHeldForAMinute` |

Each mutation, and what TLC found (`LookAway.cfg`'s own `MaxMoves = 3`,
`MaxGaps = 1` bounds throughout):

| Mutation | Broke |
|---|---|
| `GuardLearned = FALSE`: a hold runs while the resting pose is learned | Nothing, 1,112 states: `ReturnByMove`/`ReturnByAbsorb`'s own new `resting = "learned"` guard (#157) now independently keeps `NoReportWhileLearning` even when a hold itself ran too early, so this guard alone no longer needs `GuardLearned` to back it up. Left in the model and the code (`Hold`'s own `resting = "learned" \/ ~GuardLearned`, `Away`'s own `!learning` in `src/expressions.cc`) as defence in depth, named here rather than left silently unbitten |
| `GuardHold = FALSE`: no hold time | `NoReportBeforeHold`, 37 states |
| `GuardHysteresis = FALSE`: a hold can start merely between the two thresholds | `NoHoldFromBetween`, 34 states |
| `GuardAbsorb = FALSE`: the resting pose never follows a held gaze | `LookReported`/`LookRearms`, 458 states: a gaze that stays away stays open forever once `MaxMoves` runs out |
| `GuardGoneWins = FALSE`: leaving does not win the kind | `GoneWinsPriority`, 125 states |
| `GuardOtherScreenWins = FALSE`: another screen's box does not win the kind | `OtherScreenWinsPriority`, 216 states |
| `GuardOtherScreenAway = FALSE`: another screen's box is read even merely between or gone | `RestHasNothingPending`, 34 states |
| `ReturnByAbsorb` reports nothing (the old model's own quiet `Absorb`, restored) | `RestHasNothingPending`, 105 states: #141's own check, that `Absorb` still can't swallow an unreported look |
| `Gap` clears `held`, `heldFromBetween`, `heldLong`, `otherScreen` and `sawGone` again (#157's own bug, restored) | `GapNeverDropsAHeldLook`, 101 states |
| `Learn` clears `held` again (the same bug, one step later: a look surviving `Gap` would otherwise be dropped the moment relearning finished instead) | `GapNeverDropsAHeldLook`, 182 states |
| `ReturnByMove`/`ReturnByAbsorb` omit the `resting = "learned"` guard (restoring `ReturnByAbsorb`'s old third disjunct, `resting = "learning"`) | `NoReportWhileLearning`, 180 states |
| No fairness on `Hold` or `ReturnByAbsorb` | `LookReported`/`LookRearms`, 922 states |
| No fairness on `Learn` | `LookReported`/`LookRearms`, 922 states: a look held across a gap could otherwise wait on relearning forever |

`src/look_away.cc` implements the spec, and its comments name the actions.
`ReturnByAbsorb` is `ExpressionTracker`'s resting pose, in
`src/expressions.cc`, and `Learn` and `Gap` are its learning, which
`RestingPose()` reports as no resting pose. The same mutations, made to
that code, fail the tests that stand for the properties they break:

| Mutation | Fails |
|---|---|
| Away is measured from straight ahead while learning | `NoReportWhileLearningTest.NothingIsReportedWithoutARestingPose`, `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose`, `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce` |
| `RestingPose()` is given while learning | `ExpressionsTest.HasNoRestingPoseWhileItIsLearned`, `HasNoRestingPoseAfterAGapUntilItIsLearnedAgain`, `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce` |
| No hold time | `NoReportBeforeHoldTest`, `GoneKindTest.ABriefNoFaceThatReturnsBeforeTheHoldTimeIsAGlance`, and nine others |
| Reports from the between zone too | `RearmOnlyAtRestTest.HoveringRightAtTheThresholdIsOneLook`, `OtherScreenKindTest.TheBoxIsOnlyReadOnceTrulyAway` |
| The resting pose stops following once learned | `ExpressionsTest.TheRestingPoseFollowsAPoseHeldForAMinute`, and two others |
| `LookAwayDetector::Return` resets its own flags without computing a kind (the swallow bug `ReturnByAbsorb`'s mutation, above, models) | Every test under `OtherScreenKindTest` and `GoneKindTest`, and `LookRearmsTest` |
| Never reported | `LookReportedTest` (the ones that report), and a dozen others |
| Never rearms (`away_`/`held_` never reset) | `LookRearmsTest`, `LookAwayReaderTest.TwoLooksAreTwoReports`, and the two recorded-look tests |
| `kGone` never wins over `kOtherScreen`/`kAway`/`kAverted` | `GoneKindTest.GoneWinsOverOtherScreenAndDuration` |
| `kOtherScreen` never wins over `kAway`/`kAverted` | `OtherScreenKindTest.OtherScreenWinsOverDurationEvenWhenBackQuickly` |
| The other-screen box is read merely between or with no face | `OtherScreenKindTest.TheBoxIsOnlyReadOnceTrulyAway` |
| Pitch is not read | `LookReportedTest.LookingDownCountsAsAway`, `LookAwayReaderTest.ReadsPitchFromTheFaceToo`, and three others |
| Yaw does not wrap at 180 degrees | `LookReportedTest.YawIsComparedAcrossTheWrapAt180Degrees` |
| The reader judges against straight ahead, not the learned resting pose | `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose` |
| The reader reads this frame's own freshly relearned resting pose, not the one from before a gap (#141's own fix, so a look spanning the gap is not lost) | `LookAwayReaderTest.OnNoFacePastTheTrackersOwnGapStillReportsGone` |
| `LookAwayDetector::Update`'s own `!resting` branch clears `away_`, `saw_other_screen_` and `saw_gone_` again (#157's own bug, restored) | `GapNeverDropsAHeldLookTest` (both), `GoneKindTest.NoFaceFromTheVeryFirstFrameStartsTheLookToo`, `LookAwayReaderTest.ALookThatOutlastsTheTrackersOwnRelearningStillReportsGone`, `LookAwayReplayTest.EachRecordedTurnAwayIsReportedOnce` |
| `LookAwayReader::OnNoFace`/`ReplayLookingAway`/`ReplayEyesAndLookAway` call `LookAwayDetector::Update` for a no-face line before any real frame has set a timestamp (#157's own residual: a placeholder timestamp, not a resting-pose question) | `LookAwayReaderTest.OnNoFaceBeforeAnyFaceIsNotYetALook` |

The model does not prove gaze accuracy, nor that 15 degrees, a second, 5 to
8 seconds and a calibrated screen's own margin suit anyone but the one
person whose recordings tune them (#139).

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
| `DeviceLost`/`DeviceBack`/`OtherDeviceLost`/`OtherDeviceBack` on Linux | A camera connecting or disconnecting while the daemon runs reaches `SessionCamera` on Linux too, the way AVFoundation's own notifications already do on macOS (#111), both only once `StartWatching` is called (#115, #121). | `V4l2SessionTest.StartWatchingWiresHotplugCallbacksToTheGivenOnes` holds `V4l2Session` to asking the given `HotplugMonitor` to watch with exactly the callbacks it was given, once asked to (`ConstructingDoesNotStartWatchingUntilToldTo`, #115); `V4l2HotplugTest`'s `ANewCameraFiresOnConnected`, `AKnownCameraDisconnects` and the rest hold `V4l2InotifyMonitor`, the real watch over inotify, to finding and reporting a node's id the same way `Configure`/`List` do, with a node `IN_CREATE` could not yet open retried on `IN_ATTRIB` (`AnAttribAfterACreateThatCouldNotOpenRetriesAndConnects`, #115); `AvFoundationSessionTest.StartWatchingWiresDeviceChangeCallbacksToTheGivenOnes` (`test/avfoundation_session_check.mm`) holds `AvFoundationSession` to the macOS analog, with `ConstructingDoesNotWatchDevicesUntilStartWatchingIsCalled` the regression test for its own constructor (#121) |
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

`AvFoundationSession`'s own device-change observers had the identical
hazard on macOS (#121), found reading #122's Linux fix for #115:
`AVCaptureDeviceWasConnectedNotification`/`WasDisconnectedNotification`
are posted with `object:nil`, so, registered in the constructor, either
could fire for any device as soon as it ran, calling back into `camera_`
in `tools/daemon/main.cc` before `Engine`'s own constructor had built it.
A new `StartWatching` registers them instead, called from
`Engine::Start()` alongside `V4l2Session::StartWatching`, once every
member is built; the runtime-error observer stays in the constructor,
since it is scoped to this session's own `AVCaptureSession` (`object:`)
and so cannot fire before `Start` makes that session run. The model does
not cover this ordering either, only the device states themselves;
`ConstructingDoesNotWatchDevicesUntilStartWatchingIsCalled` in
`test/avfoundation_session_check.mm` is the regression test, read for by
this table's own `AvFoundationSession` constructor row below.

| Mutation to `SessionCamera`, `FramePipeline`, `V4l2Session` or `AvFoundationSession` | Fails |
|---|---|
| A start for a closed attempt runs | `SessionCameraTest.StartsNothingWhenPermissionIsGrantedAfterClose`, `StartsOnlyForTheCurrentAttempt`, `ARefusalAfterCloseReportsNothing` |
| Only the camera in use counts coming back | `SessionCameraTest.ReportsAnyCameraConnectingBeforeOneIsPicked` |
| The pipeline asks for a ticket without the frame's stamp | Every `FramePipeline` test that captures a frame |
| `SessionCamera::Start` clamps into one collapsed range instead of the nearest of several | `SessionCameraFrameRateTest.AsksForTheNearerOfTwoSeparateRanges`, `HandlesARangeRoundedOutwardFromAFixedRate`, `PicksTheClosestAmongSeveralRanges` (#43) |
| `V4l2Session::Configure` matches any node instead of the chosen device's id | `V4l2SessionTest.ConfigureGivesNothingWhenNoNodeMatchesTheChoice`, `ConfigureMatchesTheChosenDeviceAmongSeveral` (#13) |
| `V4l2Session::Configure`/`List` do not skip a node that does not query | `V4l2SessionTest.ConfigureSkipsANodeThatDoesNotQuery`, `ListSkipsANodeThatDoesNotQuery` |
| `V4l2Session` does not ask the given `HotplugMonitor` to watch, or not with the callbacks it was given | `V4l2SessionTest.StartWatchingWiresHotplugCallbacksToTheGivenOnes` (#111) |
| `V4l2Session`'s constructor starts the watch itself | `V4l2SessionTest.ConstructingDoesNotStartWatchingUntilToldTo` (#115) |
| `AvFoundationSession`'s constructor registers the device-change observers itself | `AvFoundationSessionTest.ConstructingDoesNotWatchDevicesUntilStartWatchingIsCalled` (#121) |
| `AvFoundationSession::StartWatching` does not register the observers, or not with the callbacks the constructor was given | `AvFoundationSessionTest.StartWatchingWiresDeviceChangeCallbacksToTheGivenOnes` (#121) |
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

Where the person looked (#142, #144): a beat's reply also carries the
majority gaze over its window, as context, never as the generic rule's
input -- `Rule` and `Applied` never take gaze, so the feeling they give is
unaffected by this. `gaze` is the ambient zone the person is looking in
right now (`Look` changes it, standing for a camera frame's own composed
gaze, src/screen_context.h's `Gaze`); `gazeDuring[c]` is the ghost a beat's
reply reports, reset to `gaze` when a client marks a beat and overwritten
by every `Look` while that beat stays open, so it ends up holding the zone
the beat *ends* with -- the model's own stand-in for "the majority over the
window" (the real code, `ScreenContextReporter::Between`, tallies one; a
short window's gaze rarely flips, #137's own median filter and dwell
already damp that, so the last sample is a reasonable proxy for the two
properties below). `kind[c]` is the kind a client marked its current beat
with (`Mark` now takes one); `Kinds`, like `Cues`, is a CONSTANT, so
`FeelingBeat.cfg` holds it to one element and leaves `MaxLooks = 0` --
`FeelingBeat_Gaze.cfg` is the one config that spends the branching both
kinds and a real `Look` would otherwise cost every config. The one
exception that does read gaze is `GuardAvertedUnknown`, the thinking guard:
a question beat that ends with gaze averted is read unknown however the
cues went (#142: "eyes never decide a feeling by rule", #137's own
comment). `Averted(c)` names that trigger on its own, apart from the
guard, so a mutation that skips or over-applies it is caught by an
invariant that does not just restate its own formula.

Its own content rectangle (#144) -- whether gaze was mostly inside one a
client gives alongside a beat -- is not modeled here, the same way
`src/screen_context.h`'s own comment already excuses `CoarseGazeFrom` from
a spec entry: it is a stateless computation with no lifecycle of its own,
tested directly instead (`ContentHitFromTest` in `test/screen_context_test.cc`).
The y-up-to-y-down flip a y-up platform source applies before handing
over any `ScreenRect` or `MousePosition` (#168, #169's own
`YDownFromYUp`, `src/screen_context.h`) is the same kind of stateless
computation, tested directly in the same file (`YDownFromYUpTest`).

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
| `ContextNeverChangesFeeling` | Gaze never changes the feeling a beat reports, except through the one guarded exception (#142); blink rate, a long blink and widened eyes (#145/#165) never change it at all, guarded or not -- the spec's own model still has no term for any of the three, so the code's own `EyeSignsContextTest` is the only thing proving it holds for them. | `GazeContextTest.NoGazeSourceAnswersUnknownWithNoContent`, `.TheInterpretationCarriesTheWindowsGaze`, `.AProposalEndingAvertedKeepsItsCuesFeeling`; `EyeSignsContextTest.TheEvidenceReadCarriesTheWindowsEyeSigns`, `.TheEvidenceLearnedCarriesTheWindowsEyeSigns` (the evidence tunes on them; `delivery.interpretation`'s own `feeling` never does) |
| `ThinkingGuardFires` | The thinking guard fires exactly when it should: a question beat ending averted, with the guard on, always reads unknown. | `GazeContextTest.AQuestionEndingAvertedReadsUnknown` |
| `BeatEnds` | Every beat ends, answered or abandoned. | `BeatEndsTest`, `OneAnswerPerBeatTest.DisconnectingEndsTheBeatUnanswered` |

Each mutation, and what TLC found. The invariant mutations ran without
`BeatEnds`, so the check stops at the first violation. The last two rows
are `FeelingBeat_Gaze.cfg`'s own, since `FeelingBeat.cfg`'s `MaxLooks = 0`
holds `ContextNeverChangesFeeling` and `ThinkingGuardFires` vacuously true
there (`gaze` never moves, so neither can ever trigger):

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
| `GuardAvertedUnknown = FALSE` (on `FeelingBeat_Gaze.cfg`): the thinking guard never fires | `ThinkingGuardFires`, 6 states |
| `EndBeat`'s own gate moved off `Averted(c)` -- widened to also fire on `kind[c] = "question"` alone, or on gaze away too (on `FeelingBeat_Gaze.cfg`; the mutation itself, never checked in, since `Averted(c)` is the one gate the real code follows) | `ContextNeverChangesFeeling`, 6 states |

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
| The thinking guard never fires | `GazeContextTest.AQuestionEndingAvertedReadsUnknown` |
| Gaze changes the feeling outside the thinking guard (such as applying it to a proposal too) | `GazeContextTest.AProposalEndingAvertedKeepsItsCuesFeeling` |

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
hold is what matters here, and how they tune a read -- now weighing the beat's
own gaze too (#143), never changing `FeelingBeat`'s own `Rule` -- is
`src/tuning.cc`'s, held by its own tests. A feeling for a beat whose evidence
cannot say which reaction it was about (mixed reactions, or a dropped reading)
is taken but counted
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
| Read never falls back to every gaze of the evidence (#143) | `TuningGazeTest.BelowTheMinimumForOneGazeFallsBackToEveryGazeOfIt`, `TuningGazeTest.PicksAcrossGazesCanStillNameAFeelingNoGenericRuleGives` |
| Read always falls back, ignoring an evidence's own gaze even once it has enough (#143) | `TuningGazeTest.AtOrAboveTheMinimumForOneGazeReadsItAlone` |
| The fallback pools a different sign or beat too, not only gaze (#143) | `TuningGazeTest.PicksTuneOnlyTheirOwnSignAndBeatAcrossGazes` |
| A v1 file (no gaze word) is migrated with gaze unknown, rather than read as is or refused (#143) | `TuningTextTest.ReadsAV1FileWithEveryContextFieldUnknownAndSavesItAsV3` |
| Read never falls back to every blink rate, long blink or widened-eyes value of the evidence (#145) | `TuningEyeSignsTest.BelowTheMinimumForOneExactBucketFallsBackToEveryOne` |
| Read always falls back, ignoring an evidence's own blink rate, long blink or widened eyes even once it has enough (#145) | `TuningEyeSignsTest.AtOrAboveTheMinimumForOneExactBucketReadsItAlone` |
| The fallback pools a different sign or beat too, not only the three eye signs (#145) | `TuningEyeSignsTest.PicksTuneOnlyTheirOwnSignAndBeatAcrossTheEyeSigns` |
| A v2 file (no blink-rate, long-blink or widened-eyes word) is migrated with the three unknown, rather than read as is or refused (#145) | `TuningTextTest.ReadsAV2FileWithTheNewFieldsUnknownAndSavesItAsV3` |

The integration test `test/integration/tuning.test.mjs` runs the whole path
against the real feed, store and service: three `annoyance` picks for a frown at
question beats change the next frown's read at a question, not at a proposal;
they last when the service restarts; and `reset feelings` restores `confused`.
Gaze, blink rate, a long blink and widened eyes all default to `unknown` in
that test (no screen context or window reader source), so its own counts
file carries all four words; `TuningTextTest` and
`FeelingStoreOpenTest.ReadsAV1FileAndSavesItBackAsV3` cover the gaze word
and the version 1-to-2 bump (#143),
`FeelingStoreOpenTest.ReadsAV2FileAndSavesItBackAsV3` the three new words
and the 2-to-3 bump (#145): unlike `src/calibration_store.cc`'s own
precedent for #137's bump, a pre-#143 or pre-#145 file is read, not
refused, since refusing it would throw away the person's own picks for a
file they never asked to change -- a v1 file's picks read with every
context field `unknown`, a v2 file's with the three new ones `unknown`
and gaze kept, and the next save always writes the file back as v3.

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

`calibOwner` is the one connection, if any, that owns the daemon's single
live calibration session (`specs/Calibration.tla`), tied to it by
`CalibrateStart` the way `src/service.cc`'s `CalibrateReply` ties it to
whichever connection's `calibrate start` opened it (#128): `CalibrateEnds`
clears it once any connected client finishes or cancels the session (`a
calibrate request needs no client identity`, its own comment says), but
`Closed`, that connection going away itself, is the one chance left to
clear it when nobody else does, cancelling the session along with it.

| Property | What it says | Test |
|---|---|---|
| `UniqueNames` | No two connections speak for the same client. | `UniqueNamesTest` in `test/service_test.cc`; `ConnectingANameAlreadyConnectedIsRefused` in `test/reaction_feed_test.cc`; `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClientOnlyOnce`, `AHangUpDuringABeatDisconnectsTheClientOnlyOnce` (#115) |
| `ApprovedOnly` | Every named connection showed an approved token. | `ApprovedOnlyTest` in `test/service_test.cc` and `test/client_approvals_test.cc`; `approval.test.mjs` |
| `ApprovedAdmitted` | Something must happen: an approved token with a new name is let in, even while another connection shows the same token. | `ApprovedAdmittedTest.TwoConnectionsShowingOneTokenAreTwoClients`; `ApprovedAdmittedTest.ATokenAClientHoldsIsApproved` in `test/client_approvals_test.cc`; `approval.test.mjs` |
| `FeedMatchesConns` | The feed's clients are exactly the named connections still open. | `FeedMatchesConnsTest`; `HangUpEndsTest.AHangUpDuringAWaitDisconnectsTheClientOnlyOnce`, `AHangUpDuringABeatDisconnectsTheClientOnlyOnce` (#115) |
| `StopWakesWaiters` | Once stopping, no thread is left blocked in a wait. | `StopWakesWaitersTest` |
| `NothingAfterStop` | Once stopped, every connection is closed. | `NothingAfterStopTest`, `NothingAfterStopLateTest` |
| `CanAccept` | Something must happen: a running service can accept a connection into a free slot. | `CanAcceptTest` |
| `NoDanglingCalibOwner` | The calibration session's owner, when it has one, is never a slot that has already gone free (#128). | `ServiceWithCalibrationTest.ADisconnectCancelsTheSessionItStarted`, `...WithNoSessionStartedCancelsNothing`, `...AfterItsOwnSessionAlreadyFinishedCancelsNothing`, `...AfterAnotherConnectionCancelledItCancelsNothing` |
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
| `GuardCalibCancelOnClose = FALSE`: a closed connection leaves `calibOwner` naming a slot about to go free | `NoDanglingCalibOwner`, 490 states |
| `GuardHangUpWakes = FALSE`: a hang-up during a wait is noticed only when the wait ends, the design before | `HangUpEnds` |
| No fairness on `Noticed` | `HangUpEnds` |
| No fairness on `Closed` | `StopEnds` |
| No fairness on `Stopped` | `StopEnds` |
| `Noticed` also removes the name from `feed`, as the code did before #115 | `FeedMatchesConns`, 6 states; `UniqueNames`, 8 states |

`src/service.cc` implements the spec, and each of its methods names the
action it implements: `CalibrateReply` is `CalibrateStart`/`CalibrateEnds`,
and `LeaveCalibration`, called from `Serve` once a connection's own loop
ends, is `Closed`'s own `calibOwner` clause. `src/unix_socket.cc` gives it
a Unix domain socket, whose file only its user can open; its tests run on
real sockets. The same mutations, made to the service's code, fail the
tests that stand for the properties they break:

| Mutation to `Service` | Fails |
|---|---|
| A name already connected is accepted | `UniqueNamesTest.ANameAlreadyConnectedIsRefused` |
| Any token is let in | `ApprovedOnlyTest.ATokenNotApprovedIsRefusedBeforeTheFeed`, `AHelloWithoutATokenShowsAnEmptyOne` |
| Any client's file approves any token (`ClientApprovals`) | `ApprovedOnlyTest.ATokenNoClientHoldsIsNotApproved`, `ATokenOfAnotherLengthIsNotApproved`, `ApprovedAdmittedTest.ATokenAClientHoldsIsApproved` |
| A closed connection's client stays in the feed | `FeedMatchesConnsTest.AHangUpTakesTheClientOutOfTheFeed` |
| Stopping does not disconnect the clients, so a wait runs on | `StopWakesWaitersTest.StoppingEndsAWaitAndTheConnection` |
| A connection arriving while stopping is served | `NothingAfterStopLateTest.AConnectionArrivingWhileStoppingIsShut` |
| `LeaveCalibration` never cancels the session a disconnecting connection started (#128) | `ServiceWithCalibrationTest.ADisconnectCancelsTheSessionItStarted` |
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

`SingleInstance.cfg` checks two launches and at most three failed handoffs.
TLC explored 177 distinct states. Weak fairness applies to enabled implementation
steps; launches, crashes and quits are not fair. A live owner can still fail a
handoff. Success means the owner processed the request, not that a compositor
showed or focused the window. The model abstracts the socket and its deadline.

| Property | Evidence |
|---|---|
| `TypeOK`, `AtMostOnePrimary`, `LockMatchesPrimary` | Atomic lock and cleanup; `ClaimSingleInstanceTest` and `RunSecondLaunch` |
| `SuccessMeansProcessed` | A successful settings handoff requires processing before acknowledgement |
| `LaunchSettles`, `RequestSettles` | Processing, primary ownership, or explicit failure after three attempts |

Mutations checked with TLC: disabling atomic claim violates `AtMostOnePrimary`;
disabling stale reclaim violates `LaunchSettles`; disabling forwarding or
acknowledging before processing violates `SuccessMeansProcessed`. The model
is bounded evidence, not a proof of Qt, OS lock behavior, or desktop visibility.
The integration test exercises real Qt IPC separately.

## `SettingsSocket`: accepted request lifetime (#204)

A connection either processes one complete valid request, rejects invalid
input, or closes after a bounded read deadline. The finite model has a
single connection and two abstract timer ticks; weak fairness assumes the
Qt event loop eventually delivers the timer. It does not model scheduling
latency, byte transport, window visibility, or process termination. TLC checks
7 distinct states. Removing the deadline violates `ReadSettles`; removing the
one-shot guard violates `AtMostOnce`. The real Qt IPC test exercises
fragmentation, invalid input, and a stalled partial request separately.

## `Calibration`: an optional calibration session

Models the short guided session item 12 of #61 adds (#36, #35), now reading
eyes and head together (#137, superseding #136): the resting pose, #137's
gains step (fixate a dot, turn the head slowly), then each screen's centre
and four corners (read as gaze, not head pose alone), saved to a private
profile only once every target is captured. `step` counts the targets from
0; `captured` is whether the running step has a sample yet; `weak`
abstracts a captured sample too thin to use (the gains step's own too-
little-head-travel check, or a screen's point too close to its own
centre -- what #137's spread check and #136's original finding both
read), read nondeterministically on every capture (split into
`CaptureGood`/`CaptureWeak` so `SF_vars(CaptureGood)`'s strong fairness
can require a retried step to eventually get a good sample); `camera`
abstracts the frame pipeline that
feeds samples in (#38), since a session with no frames has nothing to
capture; `applied` and `staleApply` abstract whether the daemon's own
live reader (`ScreenContextReporter`) is already reading a just-finished
profile, or still the one from before (#136's "only after Turn Off and
On"), fixed by `Restart` -- the daemon's composition root swapping in
whatever `on_finished` last handed it -- modeled as always available, so
`applied` never lags forever even with `GuardApplyImmediately` off.
`Calibration.cfg` checks `Steps = 3` (the resting pose and two points,
abstracting away which of the two distinct kinds of weak step this is --
the model does not need the real head and eye numbers to prove what
happens once a step is weak) and `MaxOutages = 1` (one camera loss and its
recovery), every guard on: 42 distinct states, every property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `NoAdvanceWithoutCapture` | Every target needs its own sample before the session moves past it. | `NoAdvanceWithoutCaptureTest` in `test/calibration_test.cc` |
| `NoSaveWithWeakStep` | A weak sample -- too little head travel in the gains step, or too little spread from a screen's own centre -- is never advanced past or saved over (#137, #136). | `NoSaveWithWeakStepTest` |
| `NoSaveOnCancel` | Cancelling never saves the session in progress; the old profile is kept. | `NoSaveOnCancelTest` |
| `NoRunningWithoutCamera` | A session only runs while the camera is up: starting needs it, and losing it ends the session. | `NoRunningWithoutCameraTest` |
| `NoStaleApply` | A freshly saved profile is read by the live reader at once, never waiting for a restart (#136, fixed by #137's `ScreenContextReporter::SetProfile` on finish). | `CalibrationWalksEveryTargetTest.OnFinishedIsToldTheSavedProfile` |
| `SessionEnds` | Something must happen: a running session always finishes or is cancelled. | `CalibrationWalksEveryTargetTest.TheLastStepFinishesTheSession`, `NoSaveOnCancelTest.CancellingNeverSaves` |
| `ProfileEventuallyApplied` | Something must happen: a freshly saved profile is always eventually read by the live reader, with or without `GuardApplyImmediately`. | Covered by the same promptness the `NoStaleApply` test above shows; nothing exercises the deferred-apply path, since the daemon's own `on_finished` always applies at once |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardCapture = FALSE`: a step can be finished with no sample captured | `NoAdvanceWithoutCapture`, 27 states |
| `GuardWeakStep = FALSE`: a weak gains step or corner is advanced past and saved over anyway | `NoSaveWithWeakStep` |
| `GuardStartCamera = FALSE`: a session can start with the camera down | `NoRunningWithoutCamera`, 17 states |
| `GuardCameraLost = FALSE`: losing the camera does not end a running session | `NoRunningWithoutCamera`, 26 states |
| `GuardCancelDiscards = FALSE`: cancelling saves the session in progress | `NoSaveOnCancel`, 17 states |
| `GuardApplyImmediately = FALSE`: a finished session's profile is saved but not read by the live reader until a restart | `NoStaleApply`, 44 states generated, finite counterexample |
| No fairness on `Finish` | `SessionEnds`: a session at its last step, never finished |

`src/calibration.h`'s `CalibrationController::Start`, `Next` and `Cancel`
implement the spec's `Start`, `Advance`/`Finish`/`Refuse` and `Cancel`;
`CalibrationSession::Update` is `CaptureGood`/`CaptureWeak`, and
`OnCameraLost` and `OnCameraUp` are `CameraLost` and `CameraBack`. Every one
of those runs under `CalibrationSession`'s own `mutex_` (#128's third
finding), taken for the whole body of each: `Start`, `Next` and `Cancel`
reach it from a connection's own thread (`src/service.cc`), `Update` from
the inference queue, and `OnCameraLost`/`OnCameraUp` from the daemon's
control queue (`src/daemon_calibration.h`), with no other ordering between
any two of them otherwise, so this is what makes each one the single
indivisible step this spec's own actions already assume it is; a
two-thread stress test,
`CalibrationSessionTest.UpdateRunsSafelyAlongsideStartAndNextOnAnotherThread`,
walks a full session (every weak-step threshold at 0, so the race itself,
not the weak-step checks `NoSaveWithWeakStepTest` already covers, is what
this test is for) while `Update` floods it with samples the whole time,
caught by ThreadSanitizer (CI's `thread-sanitizer` job) before this fix's
mutex existed. `Next`'s own weak-step check (`AdvanceOrRefuse`,
`src/calibration.cc`) is `Refuse` when it finds the gains step's own
`EstimateGains` (`src/eye_gaze.h`) refuses outright, or a corner's gaze
(`ComposeGaze`) too close to its own screen's centre; either way the step's
capture is reset for a retry, exactly as `Refuse` does. `Restart` is
`tools/daemon/main.cc`'s `Engine`: `CalibrationSession`'s own `on_finished`
callback, given at construction, calls `ScreenContextReporter::SetProfile`
with the freshly saved profile, synchronously, inside the same `Next` call
that saves it, so the model's separate `Restart` action is in the code one
indivisible step earlier than the model draws it -- `GuardApplyImmediately`
is always on in the code, with no deferred path to exercise, which is why
`ProfileEventuallyApplied`'s own test coverage is `NoStaleApply`'s. The
model's own `profile' = "new"` is already a single, indivisible
replacement, never a mutation of whatever `profile` named before (#167):
`Engine::OnCalibrationFinished` makes this true in the code too, giving
`calibration_profile_` (and `reporter_`/`look_away_reader_`, through
`SetProfile`) a wholly fresh `shared_ptr<const CalibrationProfile>` on
every finish, rather than copy-assigning into the `CalibrationProfile`
object a reading already in progress, on the camera's own thread, might
be mid-read of with no lock `OnCalibrationFinished`'s own caller (a
connection's thread, inside `CalibrationSession`'s mutex) takes.
`ScreenContextReporter` and `LookAwayReader` each take their own snapshot
of the shared_ptr under their own lock (`ScreenContextReporter`'s
existing `mutex_`; `LookAwayReader`'s own new `profile_mutex_`) before
reading it, so the object a reading names is never torn or freed under
it; no spec change, since the model's own replacement was already
atomic.
`ScreenContextReporterTest.SetProfileRunsSafelyAlongsideJsonOnAnotherThread`
and
`LookAwayReaderTest.SetProfileRunsSafelyAlongsideOnFaceOnAnotherThread`
are each this fix's own two-thread stress test, caught by ThreadSanitizer
(CI's `thread-sanitizer` job) before it, the same way
`CalibrationSessionTest.UpdateRunsSafelyAlongsideStartAndNextOnAnotherThread`
above already is for `CalibrationSession` itself. `src/
calibration_store.cc` saves the profile atomically (write, sync, rename,
sync the directory) to a private file (mode 0600), exactly once, on
`Finish`; a failed save leaves the capture in place so `Next` can be
retried, which the model does not represent. `OnCameraLost` and
`OnCameraUp` are not called by `src/calibration_reader.cc` (which only ever
sees a sample while the camera is already up): they are for whoever owns
the camera's lifecycle in the daemon's composition root to call, which is
`tools/daemon/main.cc`'s `Engine`, through `src/daemon_calibration.h`'s
`DaemonCalibration` (#116): `Engine::Open` calls `OnCameraUp` exactly when
the frame pipeline's own model is created, and `Engine::Close` calls
`OnCameraLost` before it is destroyed, with one more call at
`Engine::Start` for the camera being closed at startup, before either has
ever run once (`DaemonCalibrationTest` covers the forwarding itself,
mocking `CalibrationController`, since `Engine` has no test file of its
own, #64). `src/service.cc` dispatches a `calibrate` request to the
controller it is given, or refuses it when it is given none: `Engine`
gives it `DaemonCalibration::Controller()`, which is only ever null when
`tools/daemon/main.cc`'s `CalibrationStorePath` found no `--home` to save a
profile under, the same as the preview paths beside it. The model does not
prove the saved profile maps a gaze to the right screen, nor the gains
estimate or the eye composition themselves; those are `NearestCalibratedScreen`'s,
`EstimateGains`'s and `ComposeGaze`'s own jobs (src/eye_gaze.h, tested
directly, the same stateless-computation reasoning `specs/README.md`'s
`screen_context.h` entry already gives for `CoarseGazeFrom`), covered by
`NearestCalibratedScreenTest`, `EstimateGainsTest`, `ComposeGazeTest` and
the acceptance test in `test/calibration_store_test.cc`,
`ACalibratedProfileLocatesAScreenAnUncalibratedGuessMisses`, which replays
fixture head poses (and a passing gains step) through a full
start-capture-next-finish-save-load-locate cycle on a real file.

## `CalibrationMenu`: the menu wizard's own view of a calibration session

Models the client-side lifecycle `src/calibration_flow.h`'s `CalibrationFlow`
adds for #118 (item 12 of #61, #36, #35), now also covering #135 (every
screen gets its own window move and prompt) and the result line #137 ends
on: the menu's "Calibrate..." wizard (`tools/menu/main.mm`,
`tools/menu/main_linux.cc`) is a different thing from the session
`specs/Calibration.tla` already models in the daemon, since
`CalibrationFlow` only ever learns what the daemon's reply to its own last
request said -- it gets no push when the daemon's session ends on its own
(a camera loss, say). `state` is the flow's own view: `"closed"` (no window,
or a previous outcome dismissed), `"opening"` (hello succeeded but
`calibrate start` was refused only because the camera is not up yet, #128,
and this is retrying), `"prompting"` (a step is shown), `"closing"` (Cancel
or the window's own close button sent `calibrate cancel` and is waiting for
its reply), `"done"` or `"failed"`. `sessionLive` abstracts whether the
daemon still has a session this flow owns; `cameraUp` and `cameraWaits`
(bounded by `MaxCameraWaits`) abstract the daemon's own camera and how many
retries this flow has already spent waiting for it; `resultShown` and
`unshownResult` abstract whether the wizard has the count #135/#137 ends on
("Calibrated N screens."); `windowOnScreen` and `unmovedWindow` abstract
whether the window has been moved onto the screen the step now showing
names (#135), earned again for every step shown, not only once per flow.
`CalibrationMenu.cfg` checks one flow against one daemon session, a cold
start bounded to 2 retries, every guard on: 78 distinct states, every
property holds.

| Property | What it says | Test |
|---|---|---|
| `TypeOK` | Every variable stays in its type. | None needed: it constrains the model only |
| `NoStalePrompt` | Whatever the wizard shows as "prompting" is backed by a session the daemon still has. | `CalibrationFlowTest.NextTransitionsToFailedOnAnUnexpectedIdleReply`, `...OnAHangUpWithoutACancel` |
| `NoOrphanOnClose` | Closing the window never leaves a session running with nobody left to answer for it. | `CalibrationFlowTest.WindowClosedSendsCancelWhilePrompting`, `...IsANoOpOutsidePrompting` |
| `SessionEndsWithFlow` | A flow that has reported done or failed never still has a session the daemon thinks is running (#128): whatever ended this flow's own view also ended, or already found ended, the one it was responsible for. A refusal (#177) is not such an ending: the daemon's session stays running, and so does the flow's own "prompting". | `CalibrationFlowTest.NextStaysPromptingWithTheDaemonsHintOnARefusal`, `...NextCarriesARefusalHintWithPunctuation` |
| `ResultShownOnDone` | A finished flow always shows the count it ended on (#135, #137): the wizard never reaches "done" silently (#136's finding). | `CalibrationFlowTest.NextTransitionsToDoneWithTheScreensCalibrated` |
| `WindowMovedWhilePrompting` | Every step shown while prompting has had the window moved onto the screen it names (#135). | Not unit-testable: `tools/menu/calibration_window_macos.mm`'s `moveToScreenForStep:` and `calibration_window_linux.cc`'s `MoveToScreenForStep` are AppKit/Qt glue, untested by this repo's own convention (the window files carry no test file); a hand check on a real Mac and Ubuntu with two screens shows the window moving |
| `ClosingCompletes` | Something must happen: once Cancel or the window's close button has asked the daemon to end the session, that always finishes. | Every `CalibrationFlowTest` case that calls `Cancel()` or `WindowClosed()` observes a terminal `View()` before returning, since `CalibrateClientIo::ReadLine` is synchronous in the real POSIX and Qt glue (`tools/posix_calibrate_client_io.h`, `tools/menu/main_linux.cc`'s `QtCalibrateClientIo`) and in every mock |
| `CameraWaitResolves` | Something must happen: a cold start that keeps the wizard waiting for the camera always resolves, once the camera comes up or the wait is spent (#128). | `CalibrationFlowTest.StartRetriesWhenRefusedOnlyForTheCameraThenReachesPrompting`, `...GivesUpAfterMaxCameraWaitsWithTheCameraStillDown` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardFailOnDisconnect = FALSE`: a failed or hung-up `calibrate next` leaves the wizard showing the step that is no longer real | `NoStalePrompt`, 5 states |
| `GuardCancelOnClose = FALSE`: the window's close button tears the window down without ever sending `calibrate cancel` | `NoOrphanOnClose`, 8 states |
| `GuardRefusalStaysOpen = FALSE`: a refused `calibrate next` reaches `"failed"` instead of staying `"prompting"` on the same step, the design `ApplyStatus` had before #177's fix | `SessionEndsWithFlow`, 21 states |
| `GuardShowResult = FALSE`: a finished flow never sets its own result text | `ResultShownOnDone` |
| `GuardMoveWindow = FALSE`: a step never moves the window onto the screen it names | `WindowMovedWhilePrompting` |
| No fairness on `CancelSucceeds`/`CancelFails` | `ClosingCompletes`: a flow stuck in `"closing"` forever, stuttering once its own cancel request is on the wire |
| No fairness on `RetrySucceeds`/`RetryWaits`/`RetryGivesUp` | `CameraWaitResolves`: a flow stuck in `"opening"` forever, stuttering once the camera comes up |

`CalibrationFlow::Start` is `OpenSucceeds`/`OpenFails`/`OpenWaitsForCamera`,
retried as `RetrySucceeds`/`RetryWaits`/`RetryGivesUp` while
`CameraComesUp` is the daemon's own camera coming up on its own schedule;
`Next` is `NextContinues`/`NextFinishes`/`NextFails`/`NextRefused`, `Cancel`
is `UserCancel` and `WindowClosed` is `WindowClosed`, each followed
eventually by `CancelSucceeds` or `CancelFails`; `CalibrationFlow`'s own
synchronous `ReadLine` call (not an async callback) is exactly what makes
`ClosingCompletes` true in the code without needing the model's fairness
assumption to be a real one: a call that blocks until the daemon answers,
or the socket ends, can never be stuck in `"closing"` with nothing further
enabled, unlike a design that fired the cancel and returned immediately.
The same synchronicity is why `"opening"` is never itself observed through
`CurrentView()`: `Start`'s retry loop (`kMaxCameraWaits` attempts,
`kCameraWaitInterval` apart, a real `std::this_thread::sleep_for` by
default) runs to a `"prompting"` or `"failed"` conclusion inside the one
call `tools/menu/main.mm`'s and `main_linux.cc`'s button handlers already
make synchronously, so what the model sees as several steps is, in the
code, one. `NextRefused` is likewise one call, and one of #177's own fix:
`src/calibration.cc`'s `Next` only ever resets the step it refused
(`ResetStep`, or for a failed save, leaves it exactly captured), never the
session, so the daemon's reply comes back as a normal, still-running
`Status` with `refusal` set (`protocol::CalibrationStatusJson`'s own
`"refused"` field, #177), not the `{"error":...}` the design before this
fix could not tell apart from the session itself having ended; `ApplyStatus`
reads it into `View::hint` and stays `"prompting"`, so `GuardRefusalStaysOpen`'s
fix leaves `sessionLive` and `state` alone within the one action, matching
the code exactly -- there is no cancel left for this call to send.
`NextRefused` also covers #137's weak-step refusal (`specs/Calibration.tla`'s
`Refuse`), since `ParseCalibrationReply` (`src/calibration_flow.cc`) reads a
weak-step refusal exactly the way it reads any other refusal, needing no
action of its own. In #203, retry presentation retains the full screen and
point instruction after the sentence-cased refusal reason, exactly once.
`menu_field_format_test.cc` checks this composition; `calibration_test.cc`
checks the corner-reading explanation without changing acceptance thresholds.
The macOS view measures the wrapped text before sizing and centering its
window. These presentation changes do not add a flow transition.

The model's `windowOnScreen` abstracts intended placement, not a compositor
acknowledgement. A private two-output Wayland test reproduced that a Linux
`QWidget::move` request can leave the dialog on the first output. Neither this
model nor an assigned Qt screen proves visible placement; #203 tracks that
separate verification gap.

`NextFinishes` is also where `resultShown'`
is set (`ApplyStatus` reading `Status::screens` into `View::screens`, the
result both #135 and #137 end on); `OpenSucceeds`, `RetrySucceeds` and
`NextContinues` are also where `windowOnScreen'` is set, standing for
`tools/menu/calibration_window_macos.mm`'s `moveToScreenForStep:` and
`calibration_window_linux.cc`'s `MoveToScreenForStep`, called every time
`Render` shows a `kPrompting` view, before the label's own text -- both
compute
the screen from `View::screen` against `[NSScreen screens]` or
`QGuiApplication::screens()`, primary/primary screen first then the rest,
the same order `src/macos/screen_context_macos.mm`'s own
`MacosScreenSource::Screens()` enumerates in on a Mac -- both keying
"primary" off `[NSScreen screens].firstObject`, the true primary, rather
than `[NSScreen mainScreen]`, the key window's own screen, which the
wizard's own window becomes as it moves between monitors and which used
to flip the ordering mid-session (#168, #169) -- so the daemon's own
screen index lines up with AppKit's. On macOS, `primary` is captured once
at `Start` (`primaryClicked:`'s own `_primaryScreen`) and reused for
every later step, rather than re-read on each render, so moving the
window never changes which screen orders or names the rest. `StepPrompt`
in both names the
screen (`ScreenDescription`: its position relative to the main one, and
its display name) the same call builds. `primaryClicked:`/`PrimaryClicked`
pass `[NSScreen screens].count`/`QGuiApplication::screens().size()` to
`Start`, the real screen count rather than #136's hardcoded one (#135).
The model does not cover the socket, hello or approval mechanics
themselves (`src/calibrate_cli.h`'s `CalibrateClientIo`, already modeled
by nothing more than "succeeds or fails", is the seam `calibrate_cli_test.cc`
and `calibration_flow_test.cc` both mock), nor the daemon's own session,
which `specs/Calibration.tla` already proves, nor which connection the
daemon ties that session to, which `specs/Service.tla` does; only a hand
check on a real Mac and Ubuntu, with two screens, against a live daemon,
shows the window itself moving and naming them.

### MenuReadiness (#192)

`MenuReadiness.tla` separates installed intent, socket readiness and current
manager failure. A socket means transport readiness only; it can disappear
immediately after a menu refresh. The model does not describe QProcess
ownership or retirement. The real-child
`ServiceHealthCheck.TimesOutAndRetiresBeforeAnotherQuery` check covers the
single pending query, timeout, kill and retirement before another query.

| Property | Implementation test |
|---|---|
| `ReadyOnly` | `DecideMenuStateTest.FailedServiceIsNotReady`, `StartingServiceIsNotReady` |
| `FailureOnly` | `ServiceManagerHealthTest.ParsesOnlyAuthoritativeActiveState`, `DecideMenuStateTest.StartingServiceIsNotReady` |
| `OffOnly` | `DecideMenuStateTest.ServiceOffWithCameraIdleAndPreviewClosed`, `ServiceOnWithCameraIdleAndPreviewClosed` |
| `ReadyEnabled` | `DecideMenuStateTest.ServiceOnWithCameraIdleAndPreviewClosed`, `ReachableSocketWinsOverStaleFailure` |
| `FailureShown` | `DecideMenuStateTest.FailedServiceIsNotReady` |

| Configuration | Bounds and TLC result |
|---|---|
| `MenuReadiness.cfg` | Boolean state, 5 distinct states, 18 generated; all invariants pass |
| `GuardReady = FALSE` | `ReadyOnly` violated |
| `GuardFailureTruth = FALSE` | `FailureOnly` violated |
| `GuardOffTruth = FALSE` | `OffOnly` violated |
| `GuardShowReady = FALSE` | `ReadyEnabled` violated |
| `GuardShowFailure = FALSE` | `FailureShown` violated |

## Camera recovery (#195)

`CameraLifecycle` models no-device failures as blocked until the existing
retry action. With two clients and one restart, TLC checked 20,512 states;
turning `GuardRetries` off violates `WantedOpens`. `DaemonAdmission` models
no-device recovery and only retires after an open camera is lost. With three
clients and queue bound two, TLC checked 25,404 states; turning
`GuardNoDeviceRecovers` off violates `NoDeviceDoesNotStop`. The `canStart`
environment stands for availability of both a device and its other start
requirements; it must eventually remain true for `JoinOpens` to apply.

`CameraRetry` bounds generation changes at two and the delay at four units.
TLC checked 1,237 states; disabling identity violates `NoStaleOpen`, and
disabling the cap violates `BackoffBounded`. `CameraRetryRetirement` checks
consent, demand, repeated-loss and delayed-retirement interleavings in 25
states. Disabling `GuardHoldUntilRetired` violates
`NoReopenDuringRetirement`; disabling `GuardRetry` violates `Recovery`.
These finite models abstract the actual timer and do not prove the OS or
process lifetime.
The timer is fair only when the scheduler eventually dispatches it. The
controller tests cover cancellation, backoff and stale callbacks; daemon
control tests cover the retirement boundary. `camera_recovery_integration`
connects both real owners to controlled camera, timer and model ports and
checks the blocked-loss and delayed-retirement trace end to end.

| Model property | Named implementation test |
|---|---|
| `CameraRetry.NoStaleOpen` | `RetriesTest.StaleRetryCannotAffectNewDemand`, `RevokedConsentInvalidatesTheOldRetry` in `test/camera_controller_test.cc` |
| `CameraRetry.BackoffBounded` | `RetriesTest.BackoffIsBoundedAndResetsAfterOpen` in `test/camera_controller_test.cc` |
| `CameraRetryRetirement.NoReopenDuringRetirement` | `RetriesTest.FiredTimerCannotReopenDuringLaterRetirement`; `CameraRecoveryIntegration.RetryCannotOvertakeModelRetirement` |
| `CameraRetryRetirement.Recovery` | `CameraRecoveryIntegration.DemandAfterRetirementArmsNewRetry`, `DeviceBackDuringRetirementIsDeferred` |
| `DaemonAdmission.NoDeviceDoesNotStop` | `StopEndsTest.ACameraThatFailsToOpenLeavesTheDaemonRunning` in `test/daemon_control_test.cc` |

### PreviewRecovery (#195)

`PreviewRecovery.tla` models an open preview retaining its demand while a
verified live service retries, clearing a stale frame, and displaying a
current cause only with identity evidence. The complete Boolean model has
11 distinct states and 23 generated states. Each guard mutation was checked
independently with TLC:

| Mutation | Violated invariant | Implementation evidence |
|---|---|---|
| `GuardRetain = FALSE` | `RetryRetains` | `CameraHealthTest.VerifiedRetryKeepsPreviewAndNamesReason` |
| `GuardClear = FALSE` | `NoStaleDuringRetry` | Preview refresh clears the displayed image before checking recovery |
| `GuardAttribution = FALSE` | `ReasonNeedsIdentity` | `CameraHealthTest.UnverifiedFailureIsHistoricalAndNeverCurrent` |

The model abstracts the socket and service manager to an identity boolean;
peer credentials and manager output are tested separately. Calibration has a
bounded attempt: its connection stays open during the retries, then the
wizard reports a camera failure and offers a fresh attempt. An open preview
continues to refresh its request while the verified daemon is retrying.
The preview widget's displayed pixels and its behavior with real hardware are
not exercised by these model and solitary tests; they require a manual check.

### CalibrationRetirement (#195)

`CalibrationRetirement` models terminal states releasing their connection.
TLC checked 7 generated states and 4 distinct states, without fairness
assumptions. Setting `GuardClose = FALSE` violates `TerminalReleases`. The
model abstracts physical sockets; `calibration_transport_integration` checks
EOF with a real Unix socket. The flow test checks evidence capture before
closing.

| Model property | Named implementation test |
|---|---|
| `TerminalReleases` | `CalibrationTransportIntegration.FailedStartClosesRealSocket`; `CalibrationFlowTest.NextTransitionsToDoneOnADoneReply`, `CancelSendsCancelAndReturnsToClosedOnOk`, `WindowClosedSendsCancelWhilePrompting` |
| Capture before release | `CalibrationFlowTest.FailureEvidenceIsCapturedBeforeClose` |

## `CalibrationPresentation`: output request handoff (#203)

Models one existing flow and logical mapping intent across at most three point
steps on two frozen targets. Same-target steps and retries do not remap. A
handoff invalidates the old mapping intent before requesting the new target;
close during a handoff invalidates its pending request. A topology change
cancels the attempt and calls for a restart instead of reinterpreting indices.
The compositor can deny placement: the model does not establish physical
visibility or retirement. Real compositor behavior, physical camera ownership,
and a new attempt after cancellation are outside the model. Terminal states may
deadlock; no fairness or liveness property is assumed.

TLC checks eight invariants over 33 distinct states. Disabling GuardRetire,
GuardPreserveFlow, GuardTarget, GuardSame, GuardIdentity, GuardTopology, and
GuardGuidance respectively violates OneMappingIntent, LiveFlow,
ShownRequestsTarget, NoSameTargetRemap, NoLateReopen, LostRequiresRestart,
and GuidanceScheduled. The queued guidance action models intent to show the
message, not compositor visibility or the user closing it before it runs.
This is a proposed model until the implementation and tests cover its transitions.
