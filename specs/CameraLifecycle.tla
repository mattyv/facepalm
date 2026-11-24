---- MODULE CameraLifecycle --------------------------------------------------
(***************************************************************************)
(* When facepalm's camera may be on. The user grants and revokes consent;  *)
(* clients subscribe and leave; a camera can be unplugged and plugged in;  *)
(* and the helper process can die and be restarted. A captured frame       *)
(* becomes an event for subscribed clients only while consent holds.       *)
(*                                                                         *)
(* Two facts are kept apart, because the code keeps them apart (#38):      *)
(* `camera` is what the controller believes, and `capturing` is whether    *)
(* the capture device is running. Likewise `device` is whether a camera is *)
(* attached, and `usable` is whether the controller believes one is.       *)
(* Opening is a start task on the session queue that can run late: after   *)
(* a permission prompt, the controller may already have closed. Each open  *)
(* attempt's start task is current until closing makes it stale.          *)
(*                                                                         *)
(* A frame is copied when captured and given its consent ticket later, on  *)
(* the control queue (#40), so it records whether its capture has ended.  *)
(*                                                                         *)
(* Item 10 of #61 adds choosing the camera, and the two findings that      *)
(* belong with it (#13):                                                   *)
(*                                                                         *)
(*  - A second device, `otherDevice`, stands for a camera that is not the  *)
(*    person's default, such as a Continuity Camera iPhone. `ChosenOther`  *)
(*    is their choice, fixed for a run: a changed choice takes effect only *)
(*    on the daemon's next start (the config file main() reads once), so   *)
(*    it is a CONSTANT, not a variable the model needs to change mid-run.  *)
(*    `TargetDevice` is whichever device the choice names.                 *)
(*  - `permitted` and `startWorks` stand for the two other ways Start can   *)
(*    fail that are not "no device" (#52): the OS permission, and whatever *)
(*    else Start needs that can fail apart from the device and permission, *)
(*    such as a configuration race. `blocked` latches on any Start failure *)
(*    including no device, and `Retry` clears it, so the design's          *)
(*    retry is its own action rather than folded into `device` recovery.   *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS
    Clients,              \* The clients that may subscribe.
    MaxRestarts,          \* How many times the helper may be restarted.
    ChosenOther,            \* The person chose the other device, not the default.
    GuardOpenNeedsConsent,  \* Opening the camera requires consent.
    GuardRevokeCloses,      \* Revoking consent starts closing the camera.
    GuardRevokeDiscards,    \* Revoking consent drops a ticketed frame.
    GuardLastLeaveDiscards, \* Last leave invalidates the previous client's frame.
    GuardLastLeaveCloses,   \* The last client leaving starts closing it.
    GuardEmitToSubscribers, \* An event goes only to a subscribed client.
    GuardRestartRechecks,   \* A restarted helper starts with it closed.
    GuardStartChecksAttempt, \* A start for an ended attempt is dropped.
    GuardDiscoverAnyCamera,     \* With none selected, any new camera counts.
    GuardTicketChecksCapture, \* A frame from an ended capture is dropped.
    GuardReopens,           \* The camera may open more than once.
    GuardChoosesByIdentity, \* Start opens only the chosen device, not any (#13).
    GuardRetries            \* A denied permission or failed start is retried (#52).

VARIABLES
    consent,      \* Whether the user has granted consent.
    subscribers,  \* The clients subscribed to reactions.
    camera,       \* The controller's belief: "closed", "opening", "open"
                  \* or "closing".
    capturing,    \* Whether the capture device is running.
    starts,       \* Start tasks not yet run: "current" for this attempt,
                  \* "stale" for one closing has ended.
    device,       \* Whether the default (built-in) camera is attached and
                  \* usable.
    otherDevice,  \* Whether a second, non-default camera (such as a
                  \* Continuity Camera) is attached (#13).
    usable,       \* Whether the controller believes a camera is usable.
    selected,     \* Whether the session has picked the camera.
    helper,       \* "running" or "dead".
    restarts,     \* How many times the helper has been restarted.
    wasOpen,      \* Whether the camera was open when the helper died.
    everOpened,   \* Whether the camera has ever opened.
    permitted,    \* Whether the OS currently grants camera permission (#52).
    startWorks,   \* Whether Start would currently succeed apart from the
                  \* device and permission, such as a configuration race
                  \* (#52).
    blocked,      \* Whether the last Start failed for a reason other than
                  \* the device being absent, and no Retry has run since
                  \* (#52).
    frame,        \* "none", "copied" or "ticketed".
    frameEnded,   \* The frame's capture has ended since it was copied.
    frameOld,     \* Consent was revoked since the frame was copied.
    strayEvent,   \* An event reached a client that is not subscribed.
    unconsented,  \* An event was delivered without consent.
    stale         \* An event came from a frame copied before a revoke.

vars == <<consent, subscribers, camera, capturing, starts, device,
          otherDevice, usable, selected, helper, restarts, wasOpen,
          everOpened, permitted, startWorks, blocked, frame, frameEnded,
          frameOld, strayEvent, unconsented, stale>>

Live == {"opening", "open"}

\* Whichever device the person's choice names (#13).
TargetDevice == IF ChosenOther THEN otherDevice ELSE device

\* The camera is wanted, and nothing stops it from opening: the chosen
\* device is attached, the helper runs, and neither the OS permission nor
\* whatever else Start needs is standing in the way (#52).
Wanted ==
    /\ consent /\ subscribers # {} /\ TargetDevice /\ helper = "running"
    /\ permitted /\ startWorks

Init ==
    /\ consent = FALSE /\ subscribers = {}
    /\ camera = "closed" /\ capturing = FALSE /\ starts = {}
    /\ device \in BOOLEAN /\ otherDevice \in BOOLEAN
    /\ usable = TRUE /\ selected = FALSE
    /\ helper = "running" /\ restarts = 0 /\ wasOpen = FALSE
    /\ everOpened = FALSE
    /\ permitted \in BOOLEAN /\ startWorks \in BOOLEAN /\ blocked = FALSE
    /\ frame = "none" /\ frameEnded = FALSE /\ frameOld = FALSE
    /\ strayEvent = FALSE /\ unconsented = FALSE /\ stale = FALSE

\* The attempt ends: its start task goes stale, and so does a frame from its
\* capture.
EndAttempt ==
    /\ starts' = IF "current" \in starts
                    THEN (starts \ {"current"}) \cup {"stale"} ELSE starts
    /\ frameEnded' = (frameEnded \/ frame # "none")

\* Starts closing: the attempt ends, and the session stops capture later.
Closing == camera' = "closing" /\ EndAttempt

GrantConsent ==
    /\ ~consent /\ consent' = TRUE
    /\ UNCHANGED <<subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts, wasOpen,
                   everOpened, permitted, startWorks, blocked, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

RevokeConsent ==
    /\ consent /\ consent' = FALSE
    /\ IF GuardRevokeCloses /\ camera \in Live
          THEN Closing ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ frame' = IF GuardRevokeDiscards /\ frame = "ticketed"
                   THEN "none" ELSE frame
    /\ frameOld' = (frameOld \/ frame # "none")
    /\ UNCHANGED <<subscribers, capturing, device, otherDevice, usable,
                   selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, strayEvent, unconsented,
                   stale>>

Subscribe(c) ==
    /\ helper = "running" /\ c \notin subscribers
    /\ subscribers' = subscribers \cup {c}
    /\ UNCHANGED <<consent, camera, capturing, starts, device, otherDevice,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

Leave(c) ==
    /\ c \in subscribers
    /\ subscribers' = subscribers \ {c}
    /\ IF GuardLastLeaveCloses /\ subscribers' = {} /\ camera \in Live
          THEN Closing ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ frame' = IF GuardLastLeaveDiscards /\ subscribers' = {} /\ frame = "ticketed"
                   THEN "none" ELSE frame
    /\ frameOld' = (frameOld \/ (subscribers' = {} /\ frame # "none"))
    /\ UNCHANGED <<consent, capturing, device, otherDevice, usable, selected,
                   helper, restarts, wasOpen, everOpened, permitted,
                   startWorks, blocked, strayEvent, unconsented, stale>>

\* The controller asks the session to open: a new attempt, whose start task
\* runs later, after a permission prompt if there is one. `blocked` (#52)
\* holds Open back the same way `usable` does, until Retry clears it.
Open ==
    /\ camera = "closed" /\ helper = "running" /\ subscribers # {}
    /\ usable /\ ~blocked /\ (consent \/ ~GuardOpenNeedsConsent)
    /\ (~everOpened \/ GuardReopens)
    /\ camera' = "opening"
    /\ starts' = starts \cup {"current"}
    /\ UNCHANGED <<consent, subscribers, capturing, device, otherDevice,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* A start task runs on the session queue. A stale one is dropped.
\* Otherwise it checks the chosen device (GuardChoosesByIdentity: without
\* it, either device counts, reproducing #13's bug of picking whichever the
\* OS hands back). With the device present, it also needs the OS permission
\* and whatever else Start needs (#52); failing either of those sets
\* `blocked` rather than `usable`, since the device itself was there. The
\* controller acts on the report only while opening, so a late Opened is
\* ignored.
Start(t) ==
    /\ t \in starts /\ helper = "running"
    /\ starts' = starts \ {t}
    /\ IF GuardStartChecksAttempt /\ t = "stale"
          THEN UNCHANGED <<camera, capturing, usable, selected, everOpened,
                           blocked>>
          ELSE IF IF GuardChoosesByIdentity THEN TargetDevice
                  ELSE (device \/ otherDevice)
             THEN IF permitted /\ startWorks
                     THEN /\ capturing' = TRUE /\ selected' = TRUE
                          /\ camera' = IF camera = "opening" THEN "open" ELSE camera
                          /\ everOpened' = (everOpened \/ camera = "opening")
                          /\ UNCHANGED <<usable, blocked>>
                     ELSE /\ camera' = IF camera = "opening" THEN "closed"
                                       ELSE camera
                          /\ blocked' = (blocked \/ camera = "opening")
                          /\ UNCHANGED <<usable, capturing, selected,
                                         everOpened>>
             ELSE /\ camera' = IF camera = "opening" THEN "closed"
                               ELSE camera
                  /\ blocked' = (blocked \/ camera = "opening")
                  /\ UNCHANGED <<capturing, selected, everOpened, usable>>
    /\ UNCHANGED <<consent, subscribers, device, otherDevice, helper,
                   restarts, wasOpen, permitted, startWorks, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* The session stops capture, then reports Closed.
Closed ==
    /\ camera = "closing"
    /\ camera' = "closed" /\ capturing' = FALSE
    /\ UNCHANGED <<consent, subscribers, starts, device, otherDevice,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* Unplugged, permission withdrawn, or taken by another app. The session
\* reports it only for the camera it picked, which ends an open attempt.
\* With GuardChoosesByIdentity, losing the device that is not the chosen
\* one changes nothing (#13): a Continuity Camera's own owner unplugging it
\* must not interrupt a session the built-in camera is carrying, or the
\* reverse.
DeviceLost ==
    /\ device /\ device' = FALSE
    /\ IF GuardChoosesByIdentity /\ ChosenOther
          THEN UNCHANGED <<capturing, usable, camera, starts, frameEnded>>
          ELSE /\ capturing' = FALSE
               /\ usable' = IF selected THEN FALSE ELSE usable
               /\ IF selected /\ camera = "open"
                     THEN camera' = "closing" /\ EndAttempt
                     ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ UNCHANGED <<consent, subscribers, selected, helper, restarts,
                   wasOpen, everOpened, permitted, startWorks, blocked,
                   frame, frameOld, strayEvent, unconsented, stale,
                   otherDevice>>

\* DeviceLost's mirror for the other device (#13).
OtherDeviceLost ==
    /\ otherDevice /\ otherDevice' = FALSE
    /\ IF GuardChoosesByIdentity /\ ~ChosenOther
          THEN UNCHANGED <<capturing, usable, camera, starts, frameEnded>>
          ELSE /\ capturing' = FALSE
               /\ usable' = IF selected THEN FALSE ELSE usable
               /\ IF selected /\ camera = "open"
                     THEN camera' = "closing" /\ EndAttempt
                     ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ UNCHANGED <<consent, subscribers, selected, helper, restarts,
                   wasOpen, everOpened, permitted, startWorks, blocked,
                   frame, frameOld, strayEvent, unconsented, stale, device>>

\* A camera is plugged in. The session reports it for the camera it picked,
\* or, with none picked yet, for any camera (#39). Which device this is
\* does not have to match the person's choice: Start checks that
\* (GuardChoosesByIdentity), so a wrongly "usable" belief here only costs a
\* harmless failed attempt, never an actual open on the wrong device.
DeviceBack ==
    /\ ~device /\ device' = TRUE
    /\ usable' = IF selected \/ GuardDiscoverAnyCamera THEN TRUE ELSE usable
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts,
                   selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale, otherDevice>>

\* DeviceBack's mirror for the other device (#13).
OtherDeviceBack ==
    /\ ~otherDevice /\ otherDevice' = TRUE
    /\ usable' = IF selected \/ GuardDiscoverAnyCamera THEN TRUE ELSE usable
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts,
                   selected, helper, restarts, wasOpen, everOpened,
                   permitted, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale, device>>

\* The OS permission changes, in either direction, at any time (#52): the
\* person can deny it, and grant it later in System Settings.
TogglePermission ==
    /\ permitted' = ~permitted
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts, wasOpen,
                   everOpened, startWorks, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* Whatever else Start needs, apart from the device and permission, starts
\* or stops working (#52): a configuration race is the kind of thing this
\* stands for.
ToggleStartWorks ==
    /\ startWorks' = ~startWorks
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts, wasOpen,
                   everOpened, permitted, blocked, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* Retry clears any open failure so Open can try again. GuardRetries off
\* leaves `blocked` latched forever.
Retry ==
    /\ GuardRetries /\ camera = "closed" /\ consent /\ subscribers # {}
    /\ (blocked \/ ~usable) /\ blocked' = FALSE /\ usable' = TRUE
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, selected, helper, restarts, wasOpen,
                   everOpened, permitted, startWorks, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* The capture callback copies a frame, tagged with its capture: one that
\* arrives while closing is from an attempt that has already ended.
Capture ==
    /\ capturing /\ frame = "none"
    /\ frame' = "copied" /\ frameEnded' = (camera \notin Live)
    /\ frameOld' = FALSE
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts, wasOpen,
                   everOpened, permitted, startWorks, blocked, strayEvent,
                   unconsented, stale>>

\* Later, on the control queue: an open camera's frame gets a ticket, unless
\* its capture has ended since; any other frame is dropped.
Ticket ==
    /\ frame = "copied"
    /\ frame' = IF camera = "open" /\
                   (~frameEnded \/ ~GuardTicketChecksCapture)
                   THEN "ticketed" ELSE "none"
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts, wasOpen,
                   everOpened, permitted, startWorks, blocked, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* The frame's reaction is delivered to client c.
Emit(c) ==
    /\ frame = "ticketed" /\ frame' = "none"
    /\ (c \in subscribers \/ ~GuardEmitToSubscribers)
    /\ strayEvent' = (strayEvent \/ c \notin subscribers)
    /\ unconsented' = (unconsented \/ ~consent)
    /\ stale' = (stale \/ frameOld)
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts,
                   wasOpen, everOpened, permitted, startWorks, blocked,
                   frameEnded, frameOld>>

Drop ==
    /\ frame # "none" /\ frame' = "none"
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   otherDevice, usable, selected, helper, restarts,
                   wasOpen, everOpened, permitted, startWorks, blocked,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* The OS releases the camera and drops every connection and task. A new
\* controller starts unblocked (#52), as it starts with usable = TRUE.
Crash ==
    /\ helper = "running" /\ helper' = "dead"
    /\ wasOpen' = (camera \in Live)
    /\ camera' = "closed" /\ capturing' = FALSE /\ starts' = {}
    /\ subscribers' = {} /\ frame' = "none"
    /\ usable' = TRUE /\ selected' = FALSE /\ blocked' = FALSE
    /\ UNCHANGED <<consent, device, otherDevice, restarts, everOpened,
                   permitted, startWorks, frameEnded, frameOld, strayEvent,
                   unconsented, stale>>

\* A restart that reopened what was open would do so with no start task.
Restart ==
    /\ helper = "dead" /\ restarts < MaxRestarts
    /\ helper' = "running" /\ restarts' = restarts + 1
    /\ camera' = IF ~GuardRestartRechecks /\ wasOpen
                    THEN "opening" ELSE "closed"
    /\ wasOpen' = FALSE
    /\ UNCHANGED <<consent, subscribers, capturing, starts, device,
                   otherDevice, usable, selected, everOpened, permitted,
                   startWorks, blocked, frame, frameEnded, frameOld,
                   strayEvent, unconsented, stale>>

Next ==
    \/ GrantConsent \/ RevokeConsent
    \/ \E c \in Clients: Subscribe(c) \/ Leave(c) \/ Emit(c)
    \/ Open \/ Closed \/ \E t \in starts : Start(t)
    \/ DeviceLost \/ DeviceBack \/ OtherDeviceLost \/ OtherDeviceBack
    \/ TogglePermission \/ ToggleStartWorks \/ Retry
    \/ Capture \/ Ticket \/ Drop \/ Crash \/ Restart

Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Open) /\ WF_vars(Closed)
    /\ WF_vars(\E t \in starts : Start(t))
    /\ WF_vars(Retry)

TypeOK ==
    /\ consent \in BOOLEAN /\ subscribers \subseteq Clients
    /\ camera \in {"closed", "opening", "open", "closing"}
    /\ capturing \in BOOLEAN
    /\ starts \subseteq {"current", "stale"}
    /\ device \in BOOLEAN /\ otherDevice \in BOOLEAN
    /\ usable \in BOOLEAN /\ selected \in BOOLEAN
    /\ helper \in {"running", "dead"}
    /\ restarts \in 0..MaxRestarts /\ wasOpen \in BOOLEAN
    /\ everOpened \in BOOLEAN
    /\ permitted \in BOOLEAN /\ startWorks \in BOOLEAN /\ blocked \in BOOLEAN
    /\ frame \in {"none", "copied", "ticketed"}
    /\ frameEnded \in BOOLEAN /\ frameOld \in BOOLEAN
    /\ strayEvent \in BOOLEAN /\ unconsented \in BOOLEAN
    /\ stale \in BOOLEAN

\* Safety.
OpenOnlyWithConsent == camera \in Live => consent
OpenOnlyWhenSubscribed == camera \in Live => subscribers # {}
NoEventWithoutConsent == ~unconsented
NoEventToStranger == ~strayEvent
DeadHelperHoldsNoCamera == helper = "dead" => camera = "closed"
\* The device never captures while the controller believes it closed (#38).
NoCaptureWhenClosed == camera = "closed" => ~capturing
\* No event comes from a frame copied before consent was last revoked
\* (#40).
NoStaleEvent == ~stale
\* Capture only ever runs on the device the person chose (#13): with
\* GuardChoosesByIdentity off, Start can pick whichever device the OS
\* handed back, which can be the wrong one.
CapturesOnlyChosenDevice == capturing => TargetDevice

\* Something must happen: a wanted camera can always start opening, once
\* nothing (the device cause, `usable`, or the other causes, `blocked`,
\* #52) still stands in the way.
CanOpen == (camera = "closed" /\ Wanted /\ usable /\ ~blocked) => ENABLED Open

\* Liveness. WantedOpens is checked from every point, so one early open
\* cannot satisfy it for the rest of the run (#45). Folding permission and
\* Start's own readiness into Wanted (#52) means this now also proves a
\* denied permission or a failed start is retried once the cause clears,
\* not only a device coming back.
RevokingCloses == ~consent ~> camera = "closed"
LastLeaveCloses == subscribers = {} ~> camera = "closed"
WantedOpens == [](<>[]Wanted => <>(camera = "open"))
==============================================================================
