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
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS
    Clients,              \* The clients that may subscribe.
    MaxRestarts,          \* How many times the helper may be restarted.
    GuardOpenNeedsConsent,  \* Opening the camera requires consent.
    GuardRevokeCloses,      \* Revoking consent starts closing the camera.
    GuardRevokeDiscards,    \* Revoking consent drops a ticketed frame.
    GuardLastLeaveCloses,   \* The last client leaving starts closing it.
    GuardEmitToSubscribers, \* An event goes only to a subscribed client.
    GuardRestartRechecks,   \* A restarted helper starts with it closed.
    GuardStartChecksAttempt, \* A start for an ended attempt is dropped.
    GuardDiscoverAnyCamera,     \* With none selected, any new camera counts.
    GuardTicketChecksCapture, \* A frame from an ended capture is dropped.
    GuardReopens            \* The camera may open more than once.

VARIABLES
    consent,      \* Whether the user has granted consent.
    subscribers,  \* The clients subscribed to reactions.
    camera,       \* The controller's belief: "closed", "opening", "open"
                  \* or "closing".
    capturing,    \* Whether the capture device is running.
    starts,       \* Start tasks not yet run: "current" for this attempt,
                  \* "stale" for one closing has ended.
    device,       \* Whether a camera is attached and usable.
    usable,       \* Whether the controller believes one is.
    selected,     \* Whether the session has picked the camera.
    helper,       \* "running" or "dead".
    restarts,     \* How many times the helper has been restarted.
    wasOpen,      \* Whether the camera was open when the helper died.
    everOpened,   \* Whether the camera has ever opened.
    frame,        \* "none", "copied" or "ticketed".
    frameEnded,   \* The frame's capture has ended since it was copied.
    frameOld,     \* Consent was revoked since the frame was copied.
    strayEvent,   \* An event reached a client that is not subscribed.
    unconsented,  \* An event was delivered without consent.
    stale         \* An event came from a frame copied before a revoke.

vars == <<consent, subscribers, camera, capturing, starts, device, usable,
          selected, helper, restarts, wasOpen, everOpened, frame, frameEnded,
          frameOld, strayEvent, unconsented, stale>>

Live == {"opening", "open"}

\* The camera is wanted, and nothing stops it from opening.
Wanted ==
    /\ consent /\ subscribers # {} /\ device /\ helper = "running"

Init ==
    /\ consent = FALSE /\ subscribers = {}
    /\ camera = "closed" /\ capturing = FALSE /\ starts = {}
    /\ device \in BOOLEAN /\ usable = TRUE /\ selected = FALSE
    /\ helper = "running" /\ restarts = 0 /\ wasOpen = FALSE
    /\ everOpened = FALSE
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
    /\ UNCHANGED <<subscribers, camera, capturing, starts, device, usable,
                   selected, helper, restarts, wasOpen, everOpened, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

RevokeConsent ==
    /\ consent /\ consent' = FALSE
    /\ IF GuardRevokeCloses /\ camera \in Live
          THEN Closing ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ frame' = IF GuardRevokeDiscards /\ frame = "ticketed"
                   THEN "none" ELSE frame
    /\ frameOld' = (frameOld \/ frame # "none")
    /\ UNCHANGED <<subscribers, capturing, device, usable, selected, helper,
                   restarts, wasOpen, everOpened, strayEvent, unconsented,
                   stale>>

Subscribe(c) ==
    /\ helper = "running" /\ c \notin subscribers
    /\ subscribers' = subscribers \cup {c}
    /\ UNCHANGED <<consent, camera, capturing, starts, device, usable,
                   selected, helper, restarts, wasOpen, everOpened, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

Leave(c) ==
    /\ c \in subscribers
    /\ subscribers' = subscribers \ {c}
    /\ IF GuardLastLeaveCloses /\ subscribers' = {} /\ camera \in Live
          THEN Closing ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ UNCHANGED <<consent, capturing, device, usable, selected, helper,
                   restarts, wasOpen, everOpened, frame, frameOld,
                   strayEvent, unconsented, stale>>

\* The controller asks the session to open: a new attempt, whose start task
\* runs later, after a permission prompt if there is one.
Open ==
    /\ camera = "closed" /\ helper = "running" /\ subscribers # {}
    /\ usable /\ (consent \/ ~GuardOpenNeedsConsent)
    /\ (~everOpened \/ GuardReopens)
    /\ camera' = "opening"
    /\ starts' = starts \cup {"current"}
    /\ UNCHANGED <<consent, subscribers, capturing, device, usable,
                   selected, helper, restarts, wasOpen, everOpened, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* A start task runs on the session queue. A stale one is dropped.
\* Otherwise it starts capture if there is a camera, and reports Opened, or
\* OpenFailed. The controller acts on the report only while opening, so a
\* late Opened is ignored.
Start(t) ==
    /\ t \in starts /\ helper = "running"
    /\ starts' = starts \ {t}
    /\ IF GuardStartChecksAttempt /\ t = "stale"
          THEN UNCHANGED <<camera, capturing, usable, selected, everOpened>>
          ELSE IF device
             THEN /\ capturing' = TRUE /\ selected' = TRUE
                  /\ camera' = IF camera = "opening" THEN "open" ELSE camera
                  /\ everOpened' = (everOpened \/ camera = "opening")
                  /\ UNCHANGED usable
             ELSE /\ camera' = IF camera = "opening" THEN "closed"
                               ELSE camera
                  /\ usable' = IF camera = "opening" THEN FALSE ELSE usable
                  /\ UNCHANGED <<capturing, selected, everOpened>>
    /\ UNCHANGED <<consent, subscribers, device, helper, restarts, wasOpen,
                   frame, frameEnded, frameOld, strayEvent, unconsented,
                   stale>>

\* The session stops capture, then reports Closed.
Closed ==
    /\ camera = "closing"
    /\ camera' = "closed" /\ capturing' = FALSE
    /\ UNCHANGED <<consent, subscribers, starts, device, usable, selected,
                   helper, restarts, wasOpen, everOpened, frame, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* Unplugged, permission withdrawn, or taken by another app. The session
\* reports it only for the camera it picked, which ends an open attempt.
DeviceLost ==
    /\ device /\ device' = FALSE /\ capturing' = FALSE
    /\ usable' = IF selected THEN FALSE ELSE usable
    /\ IF selected /\ camera = "open"
          THEN camera' = "closed" /\ EndAttempt
          ELSE UNCHANGED <<camera, starts, frameEnded>>
    /\ UNCHANGED <<consent, subscribers, selected, helper, restarts,
                   wasOpen, everOpened, frame, frameOld, strayEvent,
                   unconsented, stale>>

\* A camera is plugged in. The session reports it for the camera it picked,
\* or, with none picked yet, for any camera (#39).
DeviceBack ==
    /\ ~device /\ device' = TRUE
    /\ usable' = IF selected \/ GuardDiscoverAnyCamera THEN TRUE ELSE usable
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts,
                   selected, helper, restarts, wasOpen, everOpened, frame,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* The capture callback copies a frame, tagged with its capture: one that
\* arrives while closing is from an attempt that has already ended.
Capture ==
    /\ capturing /\ frame = "none"
    /\ frame' = "copied" /\ frameEnded' = (camera \notin Live)
    /\ frameOld' = FALSE
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   strayEvent, unconsented, stale>>

\* Later, on the control queue: an open camera's frame gets a ticket, unless
\* its capture has ended since; any other frame is dropped.
Ticket ==
    /\ frame = "copied"
    /\ frame' = IF camera = "open" /\
                   (~frameEnded \/ ~GuardTicketChecksCapture)
                   THEN "ticketed" ELSE "none"
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* The frame's reaction is delivered to client c.
Emit(c) ==
    /\ frame = "ticketed" /\ frame' = "none"
    /\ (c \in subscribers \/ ~GuardEmitToSubscribers)
    /\ strayEvent' = (strayEvent \/ c \notin subscribers)
    /\ unconsented' = (unconsented \/ ~consent)
    /\ stale' = (stale \/ frameOld)
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   frameEnded, frameOld>>

Drop ==
    /\ frame # "none" /\ frame' = "none"
    /\ UNCHANGED <<consent, subscribers, camera, capturing, starts, device,
                   usable, selected, helper, restarts, wasOpen, everOpened,
                   frameEnded, frameOld, strayEvent, unconsented, stale>>

\* The OS releases the camera and drops every connection and task.
Crash ==
    /\ helper = "running" /\ helper' = "dead"
    /\ wasOpen' = (camera \in Live)
    /\ camera' = "closed" /\ capturing' = FALSE /\ starts' = {}
    /\ subscribers' = {} /\ frame' = "none"
    /\ usable' = TRUE /\ selected' = FALSE
    /\ UNCHANGED <<consent, device, restarts, everOpened, frameEnded,
                   frameOld, strayEvent, unconsented, stale>>

\* A restart that reopened what was open would do so with no start task.
Restart ==
    /\ helper = "dead" /\ restarts < MaxRestarts
    /\ helper' = "running" /\ restarts' = restarts + 1
    /\ camera' = IF ~GuardRestartRechecks /\ wasOpen
                    THEN "opening" ELSE "closed"
    /\ wasOpen' = FALSE
    /\ UNCHANGED <<consent, subscribers, capturing, starts, device, usable,
                   selected, everOpened, frame, frameEnded, frameOld,
                   strayEvent, unconsented, stale>>

Next ==
    \/ GrantConsent \/ RevokeConsent
    \/ \E c \in Clients: Subscribe(c) \/ Leave(c) \/ Emit(c)
    \/ Open \/ Closed \/ \E t \in starts : Start(t)
    \/ DeviceLost \/ DeviceBack
    \/ Capture \/ Ticket \/ Drop \/ Crash \/ Restart

Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Open) /\ WF_vars(Closed)
    /\ WF_vars(\E t \in starts : Start(t))

TypeOK ==
    /\ consent \in BOOLEAN /\ subscribers \subseteq Clients
    /\ camera \in {"closed", "opening", "open", "closing"}
    /\ capturing \in BOOLEAN
    /\ starts \subseteq {"current", "stale"}
    /\ device \in BOOLEAN /\ usable \in BOOLEAN /\ selected \in BOOLEAN
    /\ helper \in {"running", "dead"}
    /\ restarts \in 0..MaxRestarts /\ wasOpen \in BOOLEAN
    /\ everOpened \in BOOLEAN
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

\* Something must happen: a wanted camera can always start opening.
CanOpen == (camera = "closed" /\ Wanted /\ usable) => ENABLED Open

\* Liveness. WantedOpens is checked from every point, so one early open
\* cannot satisfy it for the rest of the run (#45).
RevokingCloses == ~consent ~> camera = "closed"
LastLeaveCloses == subscribers = {} ~> camera = "closed"
WantedOpens == [](<>[]Wanted => <>(camera = "open"))
==============================================================================
