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
| `CameraLifecycle.cfg` | two clients, one helper restart | holds | 1,588 |
| `DaemonAdmission.cfg` | three clients, two approved; each client and the OS queue at most two changes | holds, with `StopEnds`, `LastLeaveCloses` and `JoinOpens` | 7,908 |
| `FeedWaitRetirement.cfg` | one canceled wait and its successor | holds | 8 |
| `FramePipeline.cfg` | the camera opening twice | holds | 93 |
| `HookBroker.cfg` | one hook peer and one MCP session | holds | 20 |
| `LookAway.cfg` | three moves, one tracking gap | holds | 134 |
| `McpSocket.cfg` | one session, up to two requests | holds | 4 |
| `ProbeShutdown.cfg` | four frames, a backlog of two | holds | 185 |
| `RatingPersistence.cfg` | one rating, a sync that may fail | holds | 6 |
| `ReactionFeed.cfg` | two clients, three readings, a timeline keeping one | holds | 2,654,148 |
| `Service.cfg` | three connections, two client names, an approved token and a stranger's | holds | 396 |
| `TurnExcursion.cfg` | yaw near or away, two moves a turn, two hold lengths | holds | 26 |

## `McpSocket`: an MCP session owns one socket

The model has one session and up to two abstract requests. TLC checks four
reachable states with no fairness assumption. `NoSendAfterClose` and
`OneRequest` hold; setting `GuardClosed` or `GuardSingle` false violates the
respective invariant. Cancellation closes the local socket while the native
wait may still be running. Socket session tests cover serialization, active
and queued cancellation, terminal failure and bounded requests. The model does
not prove the remote service has retired or that a host displayed a delivery.

## `RatingPersistence`: acknowledge only synced feedback

The bounded model checks six states, with one serialized append and no fairness
assumptions. `OnlyAckSynced` and `NoWriteAfterFailure` hold; disabling
`GuardSync` or `GuardPoison` violates the respective invariant. The log tests
exercise partial writes, sync failures and the disabled sink. The model does
not prove filesystem crash recovery; a failed sync can leave an unacknowledged
record.

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
camera session (opened, open failed, closed, device lost, device back), and
the frame pipeline's word that a face model was destroyed. The session queue
opens and closes the camera, and the inference queue creates and destroys
the face model. Each queue is FIFO, and the order between the control
queue's three sources is left free, which allows more interleavings than the
code has. The model covers stopping, a retirement under way, a device coming
back during one, a failed open, a lost device, and a `Stop` re-entered from
a failed open. Opening may succeed or fail each time. The queues are weakly
fair; the person, the clients and the OS are not. `MaxQueue = 2` bounds what
the clients and the OS may queue, so the state space is finite.

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
| `JoinOpens` | Liveness, and something must happen: a connected client gets the camera open, unless every client leaves, the daemon stops, or the device is gone. | `JoinOpensTest`, `ReopenAfterRetireTest.OnlyTheOpeningCameraCreatesAModel`; `WantedOpensTest` in `test/camera_controller_test.cc` |

`CHECK_DEADLOCK FALSE` stays only because an exited daemon does nothing;
the liveness properties are what say that stopping and closing finish.
Each mutation, and what TLC found (counts are the counterexample's states):

| Mutation | Broke |
|---|---|
| `GuardApproval = FALSE`: a client not approved joins | `ApprovedOnly`, 2 states |
| `GuardFirstLast = FALSE`: every join and leave changes the subscription, the one-client design | `SubscriptionMatches`, 7 states: one client leaving takes the camera from another |
| `GuardConsent = FALSE`: the camera opens without consent | `OpenHasConsent`, 10 states: a retirement after stopping reopens it |
| `GuardStopRevokes = FALSE`: stopping leaves consent | `ExitClean`, 14 states; with it left out, `StopEnds` and `LastLeaveCloses` |
| `GuardStopRetires = FALSE`: stopping with the camera closed retires nothing | `StopEnds` |
| `GuardRetireOnce = FALSE`: two retirements at once | `ExitClean`, 6 states: the daemon exits with a face model still to destroy |
| `GuardAckAfterRetire = FALSE`: a closed camera counts as closed before its model is destroyed | `ReopenAfterRetire`, 11 states |
| `GuardDeferDeviceBack = FALSE`: a device coming back is let in during a retirement | `ReopenAfterRetire`, 7 states |
| `GuardOpenOnce = FALSE`: an `Opened` after closing began creates a model | Nothing: the model is destroyed behind it on the same FIFO inference queue, so the guard only saves loading a model no frame will reach |
| `GuardOpenFailStops = FALSE`: a failed open leaves the daemon running | Nothing: stopping on a failed open is a choice, not a protection; without it the camera waits for the device to come back, which `JoinOpens` allows |
| `OpenIf` never opens | `JoinOpens` |
| `Leave` does not close | `LastLeaveCloses` |
| `Retired` never exits | `StopEnds` |
| No fairness on `Report`, `SessionTask`, `InferenceTask` or `Retired` | `StopEnds`, `LastLeaveCloses` and `JoinOpens` |
| No fairness on `Subscription` | `LastLeaveCloses` and `JoinOpens` |
| `InferenceTask` writes a model outside the type | `TypeOK`, 6 states |

Each liveness mutation was checked with each property alone. Before #63,
`DaemonControl::OnOpenFailed` also retired the model when stopping with the
camera closed; the model shows that case cannot arise, since stopping
revokes consent and a camera that began closing reports `Closed`, so that
code was removed. The failed open's stop moved from the daemon's `main.cc`
into `DaemonControl`, where the spec's `OnOpenFailed` has it.

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
| A failed open does not stop the daemon | `StopEndsTest.ACameraThatFailsToOpenStopsTheDaemonAsAFailure` |
| A failure exits as a success | `StopEndsTest.AFailedModelStopsTheDaemonAsAFailure`, `ACameraThatFailsToOpenStopsTheDaemonAsAFailure` |
| Every join or every leave changes the subscription (`ApprovedFeed`) | `SubscriptionMatchesTest.OnlyTheFirstJoinAndTheLastLeaveCount` |

The model does not cover the OS camera itself, which `CameraLifecycle`
models, nor the secrecy of the token files, which only file permissions
give.

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
The updated model checks 1,588 distinct states under the existing bounds.

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
| `NoRearmAway` | A release by return happens back near the origin. | `TurnWaitsForReturnAndStillnessBeforeRearming`, `ReturnDistanceWrapsAroundPi` |
| `NoRearmOnGlance` | A release away from the origin needs the full 2,000 ms, so a glance and its return stay one turn. | `NoRearmOnGlanceShortOfTheSettledTime`, `EachTurnStartsItsOwnHold` |
| `NoRepeat` | No turn is reported while one is latched. | `SuppressedTurnsDoNotMakeTheGeneralGateStill`, `TurnLatchDoesNotSuppressOtherGesturesAndResetClearsIt` |
| `TurnRearms` | Something must happen: every latched turn is released, wherever the head settles, so a later turn can be reported (#62). | `TurnRearmsOnceTheHeadHasSettledAway`, `TheSettledPoseWrapsAroundPi` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardSettleAway = FALSE`: only a return releases, the design before #62 | `TurnRearms`: a turn, then the head held still away, stays latched forever |
| `GuardHoldAway = FALSE`: a release away needs no hold | `NoRearmOnGlance`, 3 states |
| `GuardReturn = FALSE`: a return release needs no return | `NoRearmAway`, 4 states |
| `GuardRepeat = FALSE`: a turn is reported while one is latched | `NoRepeat`, 3 states |
| `Settle` needs no stillness | `ReleaseNeedsStill`, 4 states |
| `Hold` never enabled | `TurnRearms` |
| No fairness on `Hold` | `TurnRearms` |
| No fairness on `Settle \/ SettleAway` | `TurnRearms` |
| No fairness at all | `TurnRearms` |
| `Hold` writes a `held` outside the type | `TypeOK`, 3 states |

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
| `NoReportWhileLearning` | Nothing is reported while the resting pose is being learned. | `NoReportWhileLearningTest` in `test/look_away_test.cc`; `LookAwayReaderTest.AHeadTurnedFromTheStartIsItsRestingPose`; `ExpressionsTest.HasNoRestingPoseWhileItIsLearned` and `HasNoRestingPoseAfterAGapUntilItIsLearnedAgain` |
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
capture device runs; likewise `device` is whether a camera is attached, and
`usable` whether the controller believes one is. Opening is a start task on
the session queue, which can run late, after a permission prompt; closing
makes it stale. A frame is copied when captured and given its consent ticket
later, so it records whether its capture has ended (#40). Each of the
design's protections is a `Guard` constant, so a mutation sets one to
`FALSE` in a copy of the config.

| Property | What it says | Test |
|---|---|---|
| `OpenOnlyWithConsent` | The camera is opening or open only while consent holds. | `OpenOnlyWithConsentTest` in `test/camera_controller_test.cc` |
| `OpenOnlyWhenSubscribed` | The camera is opening or open only while a client is subscribed. | `OpenOnlyWhenSubscribedTest` |
| `NoEventWithoutConsent` | No reaction is delivered after consent is revoked. | `NoEventWithoutConsentTest` |
| `NoEventToStranger` | A reaction reaches only a subscribed client. | `NoEventToStrangerTest` |
| `DeadHelperHoldsNoCamera` | A dead helper holds no camera. | `DeadHelperHoldsNoCameraTest`: a restarted helper is a new controller, which starts closed |
| `CanOpen` | Something must happen: a wanted, closed camera can start opening. | `WantedOpensTest` |
| `RevokingCloses` | Revoking consent leads to the camera being closed. | `RevokingClosesTest` |
| `LastLeaveCloses` | No subscribers leads to the camera being closed. | `OpenOnlyWhenSubscribedTest.TheLastClientLeavingCloses` |
| `WantedOpens` | From every point, consent, a subscriber, the device and the helper, all held, lead to the camera being open (#45). | `WantedOpensTest`; for a camera plugged in after none was found (#39), `SessionCameraTest.ReportsAnyCameraConnectingBeforeOneIsPicked` |
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
| `Start` never enabled | `WantedOpens` |
| No fairness on `Start`, `Open` or `Closed` | `WantedOpens`; for `Closed`, `RevokingCloses` and `LastLeaveCloses` |
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

`src/session_camera.cc` implements the session side: the start task, with
`GuardStartChecksAttempt`, and the camera coming back, with
`GuardDiscoverAnyCamera`. The Mac capture callback stamps each frame with the
attempt as it starts, and `src/frame_pipeline.cc` hands that stamp to the
ticket. `AvFoundationSession::Stop` waits for the capture queue, so a
callback already queued finishes under the attempt it captured for, and a
later `Start` cannot relabel its frame; no unit test reaches that code, so
it is held to the spec's `Capture` by reading.

| Mutation to `SessionCamera` or `FramePipeline` | Fails |
|---|---|
| A start for a closed attempt runs | `SessionCameraTest.StartsNothingWhenPermissionIsGrantedAfterClose`, `StartsOnlyForTheCurrentAttempt`, `ARefusalAfterCloseReportsNothing` |
| Only the camera in use counts coming back | `SessionCameraTest.ReportsAnyCameraConnectingBeforeOneIsPicked` |
| The pipeline asks for a ticket without the frame's stamp | Every `FramePipeline` test that captures a frame |

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
or a timeout ends. The person can rate a reading the client was shown, 1 to
5, for five minutes after it was shown, and the rating goes back to facepalm
for #35; `Expire` is that time passing. A client that connects
starts with no backlog, and keeps what it was shown before, so its earlier
ratings stand.

| Property | What it says | Test |
|---|---|---|
| `NoRepeat` | No reading is shown to the same client twice. | `NoRepeatTest` in `test/reaction_feed_test.cc` |
| `NoSilentGap` | Every reading made while a client was connected, up to its cursor, was shown to it or counted as missed. | `NoSilentGapTest` |
| `RateOnlyShown` | A client rates only a reading it was shown, so a rating judges the summary the person saw. | `RateOnlyShownTest` |
| `RateOnlyRecent` | A rating comes within five minutes of the reading being shown, so the person still remembers it; older ones are forgotten. | `RateOnlyRecentTest` |
| `NoWaitPastAReading` | A waiting client is never left waiting while a reading it has not seen exists. | `NoWaitPastAReadingTest` |
| `CanShow` | Something must happen: a connected client with a reading it has not seen can be shown it. | `CanShowTest` |
| `WaitEnds` | Every wait ends, with a reading or a timeout. | `WaitEndsTest` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardAdvanceCursor = FALSE`: a read leaves the cursor where it was | `CanShow`, 4 states; with `CanShow` left out, `NoRepeat`, 6 states |
| `GuardReportMissed = FALSE`: dropped readings are skipped without a word | `NoSilentGap`, 5 states |
| `GuardWakeWaiters = FALSE`: a new reading leaves waiters waiting | `NoWaitPastAReading`, 6 states |
| `GuardRateOnlyShown = FALSE`: any reading can be rated | `RateOnlyShown`, 5 states |
| `GuardRateOnlyRecent = FALSE`: a reading can be rated however long ago it was shown | `RateOnlyRecent`, 8 states |
| A read shows nothing | `CanShow`, 3 states |
| A read counts an id outside the type as missed | `TypeOK`, 3 states |
| No fairness on `WaitTimesOut` | `WaitEnds` |

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
| Any reading can be rated | `RateOnlyShownTest.AReadingNotShownToTheClientIsNot` |
| A rating ignores the five minutes | `RateOnlyRecentTest.NotOnceFiveMinutesHavePassed` |
| A read shows nothing | `CanShowTest.AConnectedClientIsShownANewReading`, and nine others |

A wait's tests require it to end within 5 seconds of a 10 second timeout, so
a missing wake fails them rather than passing late.

## `Service`: the background service and its connections

Models #37's service. It accepts connections on a socket only its user can
open, and gives each its own thread. A connection's first line names its
client and shows its token; only a token the person approved is let in,
and several connections may show the same one, so two sessions of one
approved app are two clients. A client let in is connected to
`ReactionFeed`; the thread then reads, waits for a reading, or rates. The
peer can hang up at any time, but a thread blocked in a wait is not reading
its socket, so it notices only once the wait ends. Stopping the service
closes every connection, and must not hang on a waiting one, since a wait
can last its whole timeout.

| Property | What it says | Test |
|---|---|---|
| `UniqueNames` | No two connections speak for the same client. | `UniqueNamesTest` in `test/service_test.cc`; `ConnectingANameAlreadyConnectedIsRefused` in `test/reaction_feed_test.cc` |
| `ApprovedOnly` | Every named connection showed an approved token. | `ApprovedOnlyTest` in `test/service_test.cc` and `test/client_approvals_test.cc`; `approval.test.mjs` |
| `ApprovedAdmitted` | Something must happen: an approved token with a new name is let in, even while another connection shows the same token. | `ApprovedAdmittedTest.TwoConnectionsShowingOneTokenAreTwoClients`; `ApprovedAdmittedTest.ATokenAClientHoldsIsApproved` in `test/client_approvals_test.cc`; `approval.test.mjs` |
| `FeedMatchesConns` | The feed's clients are exactly the named connections still open. | `FeedMatchesConnsTest` |
| `StopWakesWaiters` | Once stopping, no thread is left blocked in a wait. | `StopWakesWaitersTest` |
| `NothingAfterStop` | Once stopped, every connection is closed. | `NothingAfterStopTest`, `NothingAfterStopLateTest` |
| `CanAccept` | Something must happen: a running service can accept a connection into a free slot. | `CanAcceptTest` |
| `StopEnds` | Stopping always finishes. | `StopEndsTest` |

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
| No fairness on `Closed` | `StopEnds` |
| No fairness on `Stopped` | `StopEnds` |

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
| Nothing is ever accepted | Twelve tests, every one that connects |

Each test that waits on another thread gives up after 5 seconds, so a
mutation fails it rather than hanging it.

## `HookBroker`: action hooks share an MCP session

`HookBroker.cfg` checks one hook peer and one native request over 20 states.
A canceled peer cannot send queued work (`GuardPeer`), a closed broker cannot
send (`GuardOwner`), and shutdown cannot abandon a dispatched native read
(`GuardRetire`). Each disabled guard violates its corresponding invariant.
The socket and integration tests cover busy sessions, unacknowledged deliveries,
disconnects, and the shared cursor. The model covers safety ordering only.
