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

## `CameraLifecycle`: when the camera may be on

Models consent, client subscriptions, the camera device, frames in flight,
and the helper process dying and restarting. The OS can refuse or take away
the device. Each of the design's protections is a `Guard` constant, so a
mutation sets one to `FALSE` in a copy of the config.

| Property | What it says | Test |
|---|---|---|
| `OpenOnlyWithConsent` | The camera is opening or open only while consent holds. | Nothing yet: no capture code exists. |
| `OpenOnlyWhenSubscribed` | The camera is opening or open only while a client is subscribed. | Nothing yet. |
| `NoEventWithoutConsent` | No reaction is delivered after consent is revoked. | Nothing yet. |
| `NoEventToStranger` | A reaction reaches only a subscribed client. | Nothing yet. |
| `DeadHelperHoldsNoCamera` | A dead helper holds no camera. | Nothing yet. |
| `CanOpen` | Something must happen: a wanted, closed camera can start opening. | Nothing yet. |
| `RevokingCloses` | Revoking consent leads to the camera being closed. | Nothing yet. |
| `LastLeaveCloses` | No subscribers leads to the camera being closed. | Nothing yet. |
| `WantedOpens` | Consent, a subscriber, the device and the helper, all held, lead to the camera being open. | Nothing yet. |

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
