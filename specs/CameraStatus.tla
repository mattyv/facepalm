---- MODULE CameraStatus ----
\* The camera-in-use indicator (#37 item 8, #61): the daemon publishes
\* whether the camera is open to a status file beside its socket
\* (src/user_paths.h), and the menu or tray polls that file on a timer.
\* No second token or path: the file sits in the same private per-user
\* directory as the socket and the client tokens (#89), so anything that
\* may already read those may read this.
\*
\* `wanted` is the demand edge (#176's `src/camera_demand.h`, taken as
\* given): an approved client subscribing, or the preview's own live
\* request. `camera` is whether the camera is genuinely open --
\* CameraLifecycle.tla's own state, taken as given here too -- which a
\* real open only reaches some time after `wanted` does (OpenSucceeds
\* below), and may never reach at all (OpenFails below). Before #185,
\* this spec let `wanted` and `camera` be the very same variable, which
\* only held because no failure was modeled; splitting them is this
\* spec's own fix for that, not a change to the two already-proven specs
\* above.
\*
\* `ModelsCrash` and `ModelsOpenFailure` each split the model in two,
\* independently, checked by their own configs; the header's own
\* comments on each explain why.
\*
\* #184 (`ModelsCrash`): `CameraStatus.cfg` (FALSE) checks that the
\* display always catches up with the camera during normal running,
\* under `CameraOnLeadsToDisplayOn` and `CameraOffLeadsToDisplayOff`.
\* `CameraStatus_Crash.cfg` (TRUE) adds one daemon death -- a bare
\* `Crash` (killed, or dying with no chance to run any more code) or a
\* clean `Shutdown` (a signal it has time to react to) -- and checks
\* `NeverStaleWhileRunning` and, now, `CameraOffLeadsToDisplayOff` too,
\* since `Crash` and `Shutdown` both force `camera' = "off"`.
\*
\* `Restart` republishing off at the next run (already modeled below)
\* only fixes a stale file once there is a next run; a `Crash` or
\* `Shutdown` with nothing after it used to leave `displayed` stuck "on"
\* forever, freezing an open preview. The real fix has two parts,
\* matching src/daemon_status.cc: `Shutdown` gets the chance `Crash`
\* never does to publish off before the process ends
\* (`GuardPublishOnShutdown`), and the reader no longer trusts a
\* published "on" by itself -- `GuardReadChecksAlive` is Poll refusing to
\* show "on" while `running` is FALSE, standing for ReadCameraStatus
\* checking that the pid the file names is still alive. Only the second
\* part can ever save a bare `Crash`, which never gets the chance to
\* write anything; specs/README.md's mutation table says why the first
\* part's own mutation still does not bite in this model (pid reuse is
\* the real reason it still matters, which this model does not go as far
\* as simulating) rather than extending the model to cover it.
\*
\* #185 (`ModelsOpenFailure`): `CameraStatus.cfg` and
\* `CameraStatus_Crash.cfg` both set this FALSE, so `camera` flips
\* atomically with `wanted`, exactly as before the split, which is what
\* `NeverStaleWhileRunning` needs -- the early publish's own brief,
\* honest disagreement between `published` and a not-yet-open `camera`
\* is not staleness, and mixing it into either of those configs would
\* only make a property fail for a reason that has nothing to do with
\* what each is actually checking (TLC found exactly this trying one
\* combined config first: `CameraOffLeadsToDisplayOff` broke on the
\* ordinary, bug-free case of `camera = "off"` while an open was merely
\* pending, not failed). `CameraStatus_OpenFailure.cfg` (TRUE) is where
\* `OpenFails` and `OpenSucceeds` actually run: the demand callback
\* publishes "on" before the device is even asked to open (kept exactly
\* as is -- it is what lets PreviewLease.tla's own grace period work),
\* so a denied or failed open must correct that publish itself, rather
\* than waiting for some unrelated demand change to.
\* `GuardPublishOnOpenFailed` is Engine::OpenFailed publishing off at
\* once; `GuardPublishOnOpenSucceeds` is Engine::Opened publishing on
\* again, the same single call whether this is the first open or a retry
\* finally succeeding, so a transient denial does not leave the file
\* stuck off once the camera genuinely opens.
\*
\* `pending` serializes the environment with the poller: a change is held
\* until a poll has had the chance to observe it, standing for a
\* subscription that lasts at least one poll interval (seconds), which is
\* what the menu is asked to notice, not one gone within a single tick.
EXTENDS Naturals
CONSTANTS GuardPublishOnChange, GuardPublishOnRestart, GuardPollRefreshes,
         GuardPublishOnShutdown, GuardReadChecksAlive,
         GuardPublishOnOpenFailed, GuardPublishOnOpenSucceeds,
         ModelsCrash, ModelsOpenFailure
VARIABLES wanted, camera, running, crashed, published, pending, displayed,
         open_failed
vars == <<wanted, camera, running, crashed, published, pending, displayed,
         open_failed>>

Init == /\ wanted = FALSE /\ camera = "off" /\ running = TRUE
        /\ crashed = FALSE /\ published = "off" /\ pending = FALSE
        /\ displayed = "off" /\ open_failed = FALSE

\* An approved client subscribes, or the preview's own request arrives --
\* or either one's last source leaves. DaemonStatus::Publish is called
\* from the very callback that notices the change, so the two are one
\* action, not a race the publisher could lose. Demand ending closes the
\* camera at once (CameraLifecycle.tla's own close, taken as given, with
\* no failure mode of its own); demand starting only opens it at once
\* when `ModelsOpenFailure` is off, reverting to this spec's pre-#185
\* shape (see the header). A fresh demand transition starts with no open
\* failure outstanding either way: consent was just revisited, so any
\* earlier denial no longer applies.
DemandChanges ==
  /\ running /\ ~pending
  /\ \E state \in BOOLEAN:
       /\ state # wanted /\ wanted' = state
       /\ published' = IF GuardPublishOnChange
                        THEN (IF state THEN "on" ELSE "off") ELSE published
       /\ camera' = IF ~state THEN "off"
                    ELSE IF ModelsOpenFailure THEN camera ELSE "on"
  /\ pending' = TRUE /\ open_failed' = FALSE
  /\ UNCHANGED <<running, crashed, displayed>>

\* The daemon dies outright: killed, or dying with no chance to run any
\* more code before the OS reclaims the process. The camera is lost at
\* once either way, but the file it last wrote stays exactly as it was
\* (#184) -- nothing in this action can fix that; see the header for what
\* does.
Crash == /\ ModelsCrash /\ running /\ ~crashed
         /\ camera' = "off" /\ running' = FALSE /\ crashed' = TRUE
         /\ open_failed' = FALSE /\ pending' = TRUE
         /\ UNCHANGED <<wanted, published, displayed>>

\* The daemon stops cleanly instead -- a signal it has time to react to
\* -- and gets the chance `Crash` above never does. `GuardPublishOnShutdown`
\* is #184's own fix on the writer's side: publishing off before the
\* process ends.
Shutdown == /\ ModelsCrash /\ running /\ ~crashed
            /\ published' = IF GuardPublishOnShutdown
                             THEN "off" ELSE published
            /\ camera' = "off" /\ running' = FALSE /\ crashed' = TRUE
            /\ open_failed' = FALSE /\ pending' = TRUE
            /\ UNCHANGED <<wanted, displayed>>

\* The daemon starts again with no subscribers and republishes before
\* taking any subscription. GuardPublishOnRestart is the fix for the file
\* staying stuck at "on" after a crash with no later subscription change
\* to trigger a fresh publish -- a second, independent line of defense
\* behind the two #184 adds above, for whenever there is a next run.
Restart == /\ ~running /\ running' = TRUE
           /\ published' = IF GuardPublishOnRestart THEN "off" ELSE published
           /\ pending' = TRUE
           /\ UNCHANGED <<wanted, camera, crashed, displayed, open_failed>>

\* The menu's timer, independent of the daemon and blind to whether it is
\* running: it only ever rereads the file. A missing file reads the same
\* as one holding "off" (DaemonStatus::Read), which is why `published`
\* starts at "off" rather than modeling the file's absence separately.
\* #184: GuardReadChecksAlive is ReadCameraStatus no longer trusting a
\* published "on" once the daemon that published it is not `running` --
\* standing for the pid the file names no longer being alive -- which is
\* what lets this catch a bare `Crash` that never wrote anything at all.
Poll == /\ GuardPollRefreshes /\ pending
        /\ displayed' = IF GuardReadChecksAlive /\ ~running
                         THEN "off" ELSE published
        /\ pending' = FALSE
        /\ UNCHANGED <<wanted, camera, running, crashed, published,
                      open_failed>>

\* #185: wanted, but the open itself failed -- a denied permission or a
\* failed start, CameraLifecycle.tla's own retry (taken as given), which
\* keeps `wanted` on throughout rather than ever reporting it off.
\* GuardPublishOnOpenFailed is Engine::OpenFailed publishing off at once,
\* rather than leaving the early "on" uncorrected until some unrelated
\* demand change eventually republishes it.
OpenFails == /\ ModelsOpenFailure /\ running /\ ~pending
             /\ wanted /\ camera = "off" /\ ~open_failed
             /\ open_failed' = TRUE
             /\ published' = IF GuardPublishOnOpenFailed
                              THEN "off" ELSE published
             /\ pending' = TRUE
             /\ UNCHANGED <<wanted, camera, running, crashed, displayed>>

\* #185: the camera opens for real while still wanted -- the first
\* attempt succeeding outright, or a retry finally does after
\* `OpenFails`; either way reaches Engine::Opened in the code, so one
\* action and one guard cover both. GuardPublishOnOpenSucceeds is that
\* call publishing on, which the first attempt does not strictly need
\* (the early publish already said "on"), but a recovered retry does, so
\* a transient denial does not leave the file stuck off once the camera
\* genuinely opens.
OpenSucceeds == /\ ModelsOpenFailure /\ running /\ ~pending
                /\ wanted /\ camera = "off"
                /\ camera' = "on" /\ open_failed' = FALSE
                /\ published' = IF GuardPublishOnOpenSucceeds
                                 THEN "on" ELSE published
                /\ pending' = TRUE
                /\ UNCHANGED <<wanted, running, crashed, displayed>>

Next == DemandChanges \/ Crash \/ Shutdown \/ Restart \/ Poll
           \/ OpenFails \/ OpenSucceeds
\* #184: no fairness on Restart -- the daemon may never run again after a
\* clean stop (the person turned Facepalm off and left it off), which is
\* exactly the behavior GuardReadChecksAlive's own mutation needs to stay
\* reachable to prove it bites (specs/README.md).
Spec == Init /\ [][Next]_vars /\ WF_vars(Poll)

\* `CameraStatus_Crash.cfg` only: the file never disagrees with the
\* camera for longer than the daemon is down. `ModelsOpenFailure = FALSE`
\* there (see the header): with the early publish able to say "on"
\* before `camera` genuinely does, for as long as an open is still
\* pending, mixing the two would fail this for a reason that has nothing
\* to do with a crash.
NeverStaleWhileRunning == running => published = camera

\* `CameraStatus.cfg` (`ModelsOpenFailure = FALSE`): the indicator
\* eventually shows what the camera is doing, with no open ever pending
\* long enough to matter -- camera and wanted flip atomically, this
\* spec's shape before #185.
CameraOnLeadsToDisplayOn == camera = "on" ~> displayed = "on"
\* Also checked by `CameraStatus_Crash.cfg` (#184): `Crash` and
\* `Shutdown` both force `camera' = "off"`, so this is the same property
\* that proves a dead daemon's display eventually catches up too, reused
\* rather than restated as a second invariant.
CameraOffLeadsToDisplayOff == camera = "off" ~> displayed = "off"

\* `CameraStatus_OpenFailure.cfg` only (`ModelsOpenFailure = TRUE`):
\* #185's own two properties, keyed to `open_failed` rather than to raw
\* `camera`, since `camera = "off"` while an open is merely pending (not
\* failed) is the early publish working as intended, not something
\* `CameraOffLeadsToDisplayOff` above could hold against once that
\* publish can lead `camera` rather than only ever lag it.
OpenFailLeadsToDisplayOff == open_failed ~> displayed = "off"
\* A later retry succeeding is eventually shown as on again, whether
\* this is the very first open or a recovery from a failed one: either
\* way reaches `camera = "on" /\ ~open_failed` through `OpenSucceeds`,
\* which is the only action that ever sets `camera' = "on"` here.
OpenRecoversLeadsToDisplayOn ==
    (camera = "on" /\ ~open_failed) ~> displayed = "on"
====
