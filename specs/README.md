# TLA+ specs

facepalm's TLA+ specs model the parts with a lifecycle or concurrency before
their code is written, as `CLAUDE.md` requires. Each spec's section lists its
invariants with the test that proves the code obeys each one, and the
mutations that prove each one bites. The layout follows ai-pair's
`specs/README.md`.

## Tools

TLC 1.8.0 runs on Java 21 or later. `scripts/download_tlc.sh` fetches its jar
and accepts it only with the pinned SHA-256, the one ai-pair's CI uses:

```shell
bash scripts/download_tlc.sh build-tla/tla2tools.jar \
  7e53aa636dac8a3cb0533f343115c0548e03fc0ce5edc7739becf58d3d0fb84f
bash specs/check.sh
```

`specs/check.sh` checks every config; `Foo_Bar.cfg` checks `Foo.tla`. Set
`TLC` to run TLC some other way. On macOS, Homebrew's `openjdk` is keg-only:
put `/opt/homebrew/opt/openjdk/bin` on the `PATH`.

## Results

| Config | Bounds | Result | Distinct states |
|---|---|---|---|
| `CameraLifecycle.cfg` | two clients, one helper restart | holds | 148 |
| `FramePipeline.cfg` | the camera opening twice | holds | 93 |
| `ReactionFeed.cfg` | two clients, three readings, a timeline keeping one | holds | 132,884 |

## `CameraLifecycle`: when the camera may be on

Models consent, client subscriptions, the camera device, frames in flight,
and the helper process dying and restarting. The OS can refuse or take away
the device. Each of the design's protections is a `Guard` constant, so a
mutation sets one to `FALSE` in a copy of the config.

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
| `WantedOpens` | Consent, a subscriber, the device and the helper, all held, lead to the camera being open. | `WantedOpensTest` |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardOpenNeedsConsent = FALSE`: open without consent | `OpenOnlyWithConsent`, 3 states |
| `GuardRevokeCloses = FALSE`: revoking leaves it open | `OpenOnlyWithConsent`, 5 states |
| `GuardRevokeDiscards = FALSE`: a frame in flight survives revoking | `NoEventWithoutConsent`, 8 states |
| `GuardLastLeaveCloses = FALSE`: the last client leaving leaves it open | `OpenOnlyWhenSubscribed`, 5 states |
| `GuardEmitToSubscribers = FALSE`: a reaction goes to a client that left | `NoEventToStranger`, 7 states |
| `GuardRestartRechecks = FALSE`: a restart reopens what was open | `OpenOnlyWhenSubscribed`, 6 states |
| `Crash` leaves the camera as it was (checked alone) | `DeadHelperHoldsNoCamera` |
| `Open` also requires no restart yet | `CanOpen` |
| `Closed` writes a state outside the type | `TypeOK` |
| No fairness on `Closed` | `RevokingCloses`; checked alone, `LastLeaveCloses` |
| No fairness on `Open`, or on `Opened` | `WantedOpens` |

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

## `FramePipeline`: how a frame reaches the face model

Refines `CameraLifecycle`'s `Capture`, `Emit` and `Drop` for #20's pipeline.
Three serial queues share one frame slot. The capture queue copies a frame
into the slot as RGBA, since the frame is gone when the callback returns.
The control queue, `CameraController`, opens and closes the camera and gives
the frame a ticket, or refuses one when the camera is not open. The
inference queue runs its tasks in order: create the model when the camera
opens, detect on a ticketed frame, and destroy the model when the camera
closes. Frames may arrive at any time, even just after closing, and loading
the model and detecting can each fail.

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
| A frame is copied in while the slot is taken | `AtMostOneInFlightTest`, `NeverWrittenWhileReadTest` |
| A refused ticket does not free the slot | `SlotFreedTest.AfterARefusedTicket` |
| A failed detect does not free the slot | `SlotFreedTest.AfterAFailedDetect` |
| A detect with no model skips without freeing the slot | `SlotFreedTest.WithNoModel` |
| A detect runs with no model | `NoDetectWithoutModelTest`, on the null model |
| Closing destroys the model at once, on the caller's thread | `ModelGoneAfterCloseTest.ClosingDestroysTheModel`, `NoDetectWithoutModelTest.ClosingWaitsForADetectAlreadyQueued` |
| Closing never destroys the model | `ModelGoneAfterCloseTest.ClosingDestroysTheModel`, `NoDetectWithoutModelTest.DetectsNothingOnceClosed` |
| A ticketed frame's detect is never queued | `CanDetectTest`, and six others |
| A frame's pixels are not wiped when its slot is freed | `PixelsWipedTest` (both; the spec does not model the wipe) |

## `ReactionFeed`: how readings reach each client

Models #36's delivery. facepalm keeps one timeline of readings: each is an
interpreted reaction with the one-line summary facepalm writes, and an id.
Past `Capacity`, the oldest reading is dropped. Each client, an agent session
or AI Pair, has a cursor. A read returns the readings since that client last
looked, whether it comes from a hook after an agent action or from the MCP's
check-in. A client can instead wait for the next reading, which a new reading
or a timeout ends. The person can rate a reading the client was shown, 1 to
5, and the rating goes back to facepalm for #35. A client that connects
starts with no backlog, and keeps what it was shown before, so its earlier
ratings stand.

| Property | What it says | Test |
|---|---|---|
| `NoRepeat` | No reading is shown to the same client twice. | Nothing yet |
| `NoSilentGap` | Every reading made while a client was connected, up to its cursor, was shown to it or counted as missed. | Nothing yet |
| `RateOnlyShown` | A client rates only a reading it was shown, so a rating judges the summary the person saw. | Nothing yet |
| `NoWaitPastAReading` | A waiting client is never left waiting while a reading it has not seen exists. | Nothing yet |
| `CanShow` | Something must happen: a connected client with a reading it has not seen can be shown it. | Nothing yet |
| `WaitEnds` | Every wait ends, with a reading or a timeout. | Nothing yet |

Each mutation, and what TLC found:

| Mutation | Broke |
|---|---|
| `GuardAdvanceCursor = FALSE`: a read leaves the cursor where it was | `CanShow`, 4 states; with `CanShow` left out, `NoRepeat`, 6 states |
| `GuardReportMissed = FALSE`: dropped readings are skipped without a word | `NoSilentGap`, 5 states |
| `GuardWakeWaiters = FALSE`: a new reading leaves waiters waiting | `NoWaitPastAReading`, 6 states |
| `GuardRateOnlyShown = FALSE`: any reading can be rated | `RateOnlyShown`, 5 states |
| A read shows nothing | `CanShow`, 3 states |
| A read counts an id outside the type as missed | `TypeOK`, 3 states |
| No fairness on `WaitTimesOut` | `WaitEnds` |
