---- MODULE CameraLifecycle --------------------------------------------------
(***************************************************************************)
(* When facepalm's camera may be on. The user grants and revokes consent;  *)
(* clients subscribe and leave; the OS can take the device away; and the   *)
(* helper process can die and be restarted. A captured frame becomes an    *)
(* event for subscribed clients only while consent holds.                  *)
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
    GuardRevokeDiscards,    \* Revoking consent drops a frame in flight.
    GuardLastLeaveCloses,   \* The last client leaving starts closing it.
    GuardEmitToSubscribers, \* An event goes only to a subscribed client.
    GuardRestartRechecks    \* A restarted helper starts with it closed.

VARIABLES
    consent,      \* Whether the user has granted consent.
    subscribers,  \* The clients subscribed to reactions.
    camera,       \* "closed", "opening", "open" or "closing".
    device,       \* Whether the OS lets the camera be used.
    helper,       \* "running" or "dead".
    restarts,     \* How many times the helper has been restarted.
    wasOpen,      \* Whether the camera was open when the helper died.
    inFlight,     \* Whether a captured frame is being processed.
    strayEvent,   \* An event reached a client that is not subscribed.
    unconsented   \* An event was delivered without consent.

vars == <<consent, subscribers, camera, device, helper, restarts, wasOpen,
          inFlight, strayEvent, unconsented>>

Live == {"opening", "open"}

\* The camera is wanted, and nothing stops it from opening.
Wanted ==
    /\ consent /\ subscribers # {} /\ device /\ helper = "running"

Init ==
    /\ consent = FALSE /\ subscribers = {} /\ camera = "closed"
    /\ device = TRUE /\ helper = "running" /\ restarts = 0
    /\ wasOpen = FALSE /\ inFlight = FALSE
    /\ strayEvent = FALSE /\ unconsented = FALSE

GrantConsent ==
    /\ ~consent /\ consent' = TRUE
    /\ UNCHANGED <<subscribers, camera, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

RevokeConsent ==
    /\ consent /\ consent' = FALSE
    /\ camera' = IF GuardRevokeCloses /\ camera \in Live
                    THEN "closing" ELSE camera
    /\ inFlight' = IF GuardRevokeDiscards THEN FALSE ELSE inFlight
    /\ UNCHANGED <<subscribers, device, helper, restarts, wasOpen,
                   strayEvent, unconsented>>

Subscribe(c) ==
    /\ helper = "running" /\ c \notin subscribers
    /\ subscribers' = subscribers \cup {c}
    /\ UNCHANGED <<consent, camera, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

Leave(c) ==
    /\ c \in subscribers
    /\ subscribers' = subscribers \ {c}
    /\ camera' = IF GuardLastLeaveCloses /\ subscribers' = {} /\
                    camera \in Live
                    THEN "closing" ELSE camera
    /\ UNCHANGED <<consent, device, helper, restarts, wasOpen, inFlight,
                   strayEvent, unconsented>>

Open ==
    /\ camera = "closed" /\ helper = "running" /\ subscribers # {}
    /\ (consent \/ ~GuardOpenNeedsConsent)
    /\ camera' = "opening"
    /\ UNCHANGED <<consent, subscribers, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

Opened ==
    /\ camera = "opening" /\ device /\ camera' = "open"
    /\ UNCHANGED <<consent, subscribers, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

\* The OS refuses the device: permission denied, or another app holds it.
OpenFailed ==
    /\ camera = "opening" /\ ~device /\ camera' = "closed"
    /\ UNCHANGED <<consent, subscribers, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

Closed ==
    /\ camera = "closing" /\ camera' = "closed"
    /\ UNCHANGED <<consent, subscribers, device, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

\* Unplugged, permission withdrawn, or taken by another app.
DeviceLost ==
    /\ device /\ device' = FALSE
    /\ camera' = IF camera = "open" THEN "closed" ELSE camera
    /\ UNCHANGED <<consent, subscribers, helper, restarts, wasOpen, inFlight,
                   strayEvent, unconsented>>

DeviceBack ==
    /\ ~device /\ device' = TRUE
    /\ UNCHANGED <<consent, subscribers, camera, helper, restarts, wasOpen,
                   inFlight, strayEvent, unconsented>>

Capture ==
    /\ camera = "open" /\ ~inFlight /\ inFlight' = TRUE
    /\ UNCHANGED <<consent, subscribers, camera, device, helper, restarts,
                   wasOpen, strayEvent, unconsented>>

\* The frame's reaction is delivered to client c, or dropped.
Emit(c) ==
    /\ inFlight /\ inFlight' = FALSE
    /\ (c \in subscribers \/ ~GuardEmitToSubscribers)
    /\ strayEvent' = (strayEvent \/ c \notin subscribers)
    /\ unconsented' = (unconsented \/ ~consent)
    /\ UNCHANGED <<consent, subscribers, camera, device, helper, restarts,
                   wasOpen>>

Drop ==
    /\ inFlight /\ inFlight' = FALSE
    /\ UNCHANGED <<consent, subscribers, camera, device, helper, restarts,
                   wasOpen, strayEvent, unconsented>>

\* The OS releases the camera and drops every connection.
Crash ==
    /\ helper = "running" /\ helper' = "dead"
    /\ wasOpen' = (camera \in Live)
    /\ camera' = "closed" /\ subscribers' = {} /\ inFlight' = FALSE
    /\ UNCHANGED <<consent, device, restarts, strayEvent, unconsented>>

Restart ==
    /\ helper = "dead" /\ restarts < MaxRestarts
    /\ helper' = "running" /\ restarts' = restarts + 1
    /\ camera' = IF ~GuardRestartRechecks /\ wasOpen
                    THEN "opening" ELSE "closed"
    /\ wasOpen' = FALSE
    /\ UNCHANGED <<consent, subscribers, device, inFlight, strayEvent,
                   unconsented>>

Next ==
    \/ GrantConsent \/ RevokeConsent
    \/ \E c \in Clients: Subscribe(c) \/ Leave(c) \/ Emit(c)
    \/ Open \/ Opened \/ OpenFailed \/ Closed
    \/ DeviceLost \/ DeviceBack
    \/ Capture \/ Drop \/ Crash \/ Restart

Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Open) /\ WF_vars(Opened) /\ WF_vars(Closed)

TypeOK ==
    /\ consent \in BOOLEAN /\ subscribers \subseteq Clients
    /\ camera \in {"closed", "opening", "open", "closing"}
    /\ device \in BOOLEAN /\ helper \in {"running", "dead"}
    /\ restarts \in 0..MaxRestarts /\ wasOpen \in BOOLEAN
    /\ inFlight \in BOOLEAN /\ strayEvent \in BOOLEAN
    /\ unconsented \in BOOLEAN

\* Safety.
OpenOnlyWithConsent == camera \in Live => consent
OpenOnlyWhenSubscribed == camera \in Live => subscribers # {}
NoEventWithoutConsent == ~unconsented
NoEventToStranger == ~strayEvent
DeadHelperHoldsNoCamera == helper = "dead" => camera = "closed"

\* Something must happen: a wanted camera can always start opening.
CanOpen == (camera = "closed" /\ Wanted) => ENABLED Open

\* Liveness.
RevokingCloses == ~consent ~> camera = "closed"
LastLeaveCloses == subscribers = {} ~> camera = "closed"
WantedOpens == <>[]Wanted => <>(camera = "open")
==============================================================================
