# Decisions

Each decision made about facepalm and why, newest first. An entry is short: the
evidence and the discussion are in the issue it names. An entry is never
edited once made; a decision that is reversed gets a new entry, and the old one
a line saying which entry replaced it.

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
