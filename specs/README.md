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
| `CameraLifecycle.cfg` | two clients, one helper restart | holds | 1,644 |
| `FramePipeline.cfg` | the camera opening twice | holds | 93 |
| `ReactionFeed.cfg` | two clients, three readings, a timeline keeping one | holds | 2,654,148 |
| `Service.cfg` | three connections, two client names | holds | 396 |

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

## `DaemonAdmission`: consent, approval and retirement

The bounded model checks 26 states with one client and no fairness assumptions.
`OpenHasConsent`, `OpenHasApproval` and `StoppedAfterRetirement` hold; each
corresponding disabled guard violates its invariant. It abstracts physical
camera closure and model retirement as separate actions. The controller,
approved-feed and daemon-control tests cover the integration; the model does
not prove OS camera behavior or the secrecy of a token file.

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

## `TurnExcursion`: suppress repeated turns until return

The bounded model abstracts yaw as near or away and the elapsed settling time
as one action. TLC checks six distinct states with no fairness assumption.
`NoRearmAway` and `NoRepeat` hold. Setting `GuardReturn` or `GuardRepeat` to
false violates the corresponding invariant. Tests in `gesture_reader_test`
cover elapsed time, interruption, tracking loss, angle wrap and the general
gesture gate. The model does not prove pose accuracy or model classification.

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
handlers at the same time can abort after the capture summary has been printed.

`ProbeShutdown.cfg` checks one terminal camera close. Weak fairness assumes
the queued teardown starts, the model closes, and the control queue processes
completion. The model checks ordering after camera closure and assumes model
destruction returns.

| Property | Result |
|---|---|
| `ExitAfterRetirement` and `EventuallyExits` | Hold across four distinct states |
| `WaitForRetirement = FALSE` | Violates `ExitAfterRetirement` in two steps while destruction is still queued |

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
client, which connects that client to `ReactionFeed`; the thread then reads,
waits for a reading, or rates. The peer can hang up at any time, but a
thread blocked in a wait is not reading its socket, so it notices only once
the wait ends. Stopping the service closes every connection, and must not
hang on a waiting one, since a wait can last its whole timeout.

| Property | What it says | Test |
|---|---|---|
| `UniqueNames` | No two connections speak for the same client. | `UniqueNamesTest` in `test/service_test.cc`; `ConnectingANameAlreadyConnectedIsRefused` in `test/reaction_feed_test.cc` |
| `FeedMatchesConns` | The feed's clients are exactly the named connections still open. | `FeedMatchesConnsTest` |
| `StopWakesWaiters` | Once stopping, no thread is left blocked in a wait. | `StopWakesWaitersTest` |
| `NothingAfterStop` | Once stopped, every connection is closed. | `NothingAfterStopTest`, `NothingAfterStopLateTest` |
| `CanAccept` | Something must happen: a running service can accept a connection into a free slot. | `CanAcceptTest` |
| `StopEnds` | Stopping always finishes. | `StopEndsTest` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardUniqueNames = FALSE`: a name already connected is accepted | `UniqueNames`, 5 states |
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
