---- MODULE PreviewLease ----
\* The camera preview's lease (#61 item 8, #36): the menu may watch the
\* camera only once it explicitly starts the preview, and only while the
\* camera is open from a subscription DaemonAdmission.tla already
\* governs; losing either ends it. No image ever reaches a client over
\* the approved-token service: this models the daemon's own gate on a
\* second, user-only local channel the menu alone connects to (#89's
\* trust boundary, not a new one), never whether to publish at all.
\*
\* `camera_session` counts each time the camera opens; `started_session`
\* names the session, if any, a StartPreview was last granted for. They
\* must match for streaming to be on, so a camera that closed and reopened
\* needs a fresh StartPreview: the old grant does not carry over
\* (GuardStartNeedsCamera, together with the session check, is what rules
\* out a preview resuming on its own).
EXTENDS Naturals
CONSTANTS MaxSessions, GuardStartNeedsCamera, GuardStreamingTracksLease
VARIABLES camera, camera_session, started_session, streaming
vars == <<camera, camera_session, started_session, streaming>>

Init == /\ camera = "closed" /\ camera_session = 0
        /\ started_session = 0 /\ streaming = FALSE

\* DaemonAdmission.tla's own first-join, taken as given here.
CameraOpens == /\ camera = "closed" /\ camera_session < MaxSessions
               /\ camera' = "open" /\ camera_session' = camera_session + 1
               /\ streaming' = FALSE
               /\ UNCHANGED started_session

\* DaemonAdmission.tla's own last-leave, taken as given here: ends any
\* preview at once, same as the indicator (#37 item 8) stopping with it.
CameraCloses == /\ camera = "open" /\ camera' = "closed"
                /\ streaming' = IF GuardStreamingTracksLease
                                  THEN FALSE ELSE streaming
                /\ UNCHANGED <<camera_session, started_session>>

\* PreviewLease!Start: the person's own action from the menu. Whether
\* streaming follows depends only on GuardStreamingTracksLease: nothing
\* else here checks camera is open again, so a start wrongly let through
\* while it is closed (GuardStartNeedsCamera off) really does start
\* streaming with no camera, rather than being caught twice over.
StartPreview == /\ camera = "open" \/ ~GuardStartNeedsCamera
                /\ started_session # camera_session
                /\ started_session' = camera_session
                /\ streaming' = IF GuardStreamingTracksLease
                                  THEN TRUE ELSE streaming
                /\ UNCHANGED <<camera, camera_session>>

\* PreviewLease!Stop: the person's own action, or the menu's connection to
\* the preview channel closing.
StopPreview == /\ started_session = camera_session /\ started_session # 0
               /\ started_session' = 0
               /\ streaming' = IF GuardStreamingTracksLease
                                 THEN FALSE ELSE streaming
               /\ UNCHANGED <<camera, camera_session>>

Next == CameraOpens \/ CameraCloses \/ StartPreview \/ StopPreview
Spec == Init /\ [][Next]_vars /\ WF_vars(CameraOpens)

\* The camera can always eventually open: something must happen, so a
\* model that can never open it fails this.
EventuallyOpens == <>(camera = "open")

NeverStreamsWithoutCamera == streaming => camera = "open"
NeverStreamsWithoutAFreshStart ==
  streaming => (started_session = camera_session /\ started_session # 0)
\* Checked every state, not just eventually, so GuardStreamingTracksLease
\* cannot hide behind an adversarial schedule the way a liveness property
\* checking the same thing would (TLC found exactly that: a camera that
\* always closes again before a liveness check could catch a preview
\* that should have started, simply because closing was always the other
\* choice available, forever).
StreamingMatchesFormula ==
  streaming = (camera = "open" /\ started_session = camera_session
               /\ started_session # 0)
====
