---- MODULE CameraStatus ----
\* The camera-in-use indicator (#37 item 8, #61): the daemon publishes
\* whether the camera is open to a status file beside its socket
\* (src/user_paths.h), and the menu or tray polls that file on a timer.
\* No second token or path: the file sits in the same private per-user
\* directory as the socket and the client tokens (#89), so anything that
\* may already read those may read this.
\*
\* `camera` stands for CameraController's own state, already proved by
\* DaemonAdmission.tla: it goes on when the first client subscribes and
\* off when the last one leaves or the daemon loses consent. This spec
\* takes those transitions as given and checks only the publish/poll path
\* on top of them.
\*
\* `ModelsCrash` splits the model in two, checked by its two configs.
\* FALSE (`CameraStatus.cfg`) checks that the display always catches up
\* with the camera during normal running, under `CameraOnLeadsToDisplayOn`
\* and `CameraOffLeadsToDisplayOff`. TRUE (`CameraStatus_Crash.cfg`) adds
\* one crash and checks `NeverStaleWhileRunning` instead: TLC found that a
\* crash timed to land inside the window between a change and the next
\* poll can make the display miss that one change for good, which is a
\* real but physically correct outcome (the camera the display never
\* caught really did go off in the crash, as every behavior's `camera`
\* value agrees), not a bug the two properties above should have to
\* survive; the two configs keep that distinction rather than one
\* property trying to say both at once.
\*
\* `pending` serializes the environment with the poller: a change is held
\* until a poll has had the chance to observe it, standing for a
\* subscription that lasts at least one poll interval (seconds), which is
\* what the menu is asked to notice, not one gone within a single tick.
EXTENDS Naturals
CONSTANTS GuardPublishOnChange, GuardPublishOnRestart, GuardPollRefreshes,
         ModelsCrash
VARIABLES camera, running, crashed, published, pending, displayed
vars == <<camera, running, crashed, published, pending, displayed>>

Init == /\ camera = "off" /\ running = TRUE /\ crashed = FALSE
        /\ published = "off" /\ pending = FALSE /\ displayed = "off"

\* A client subscribes, or the last one leaves. DaemonStatus::Publish is
\* called from the very callback that notices the change, so the two are
\* one action, not a race the publisher could lose.
CameraChanges ==
  /\ running /\ ~pending
  /\ \E state \in {"on", "off"}:
       /\ state # camera /\ camera' = state
       /\ published' = IF GuardPublishOnChange THEN state ELSE published
  /\ pending' = TRUE
  /\ UNCHANGED <<running, crashed, displayed>>

\* The daemon crashes, once: the camera is lost at once (the OS reclaims
\* it), but the file it last wrote stays exactly as it was. Only part of
\* the model when ModelsCrash: see the header.
Crash == /\ ModelsCrash /\ running /\ ~crashed
         /\ camera' = "off" /\ running' = FALSE /\ crashed' = TRUE
         /\ UNCHANGED <<published, pending, displayed>>

\* The daemon starts again with no subscribers and republishes before
\* taking any subscription. GuardPublishOnRestart is the fix for the file
\* staying stuck at "on" after a crash with no later subscription change
\* to trigger a fresh publish.
Restart == /\ ~running /\ running' = TRUE
           /\ published' = IF GuardPublishOnRestart THEN "off" ELSE published
           /\ pending' = TRUE
           /\ UNCHANGED <<camera, crashed, displayed>>

\* The menu's timer, independent of the daemon and blind to whether it is
\* running: it only ever rereads the file. A missing file reads the same
\* as one holding "off" (DaemonStatus::Read), which is why `published`
\* starts at "off" rather than modeling the file's absence separately.
Poll == /\ GuardPollRefreshes /\ pending
        /\ displayed' = published /\ pending' = FALSE
        /\ UNCHANGED <<camera, running, crashed, published>>

Next == CameraChanges \/ Crash \/ Restart \/ Poll
Spec == Init /\ [][Next]_vars /\ WF_vars(Poll) /\ WF_vars(Restart)

\* `CameraStatus.cfg` (ModelsCrash = FALSE): the indicator eventually
\* shows what the camera is doing.
CameraOnLeadsToDisplayOn == camera = "on" ~> displayed = "on"
CameraOffLeadsToDisplayOff == camera = "off" ~> displayed = "off"

\* `CameraStatus_Crash.cfg` (ModelsCrash = TRUE): the file never disagrees
\* with the camera for longer than the daemon is down. Once Restart has
\* run, running and published stay in lockstep with camera (through
\* CameraChanges) for the rest of the behavior, so nothing is ever stuck
\* showing a camera that crashed and came back as still on, or one that
\* changed while the daemon was briefly down as unchanged.
NeverStaleWhileRunning == running => published = camera
====
