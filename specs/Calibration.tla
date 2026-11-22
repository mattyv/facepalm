---- MODULE Calibration ----
\* facepalm's optional calibration session (item 12 of #61, #36, #35): a
\* short guided session that records the resting pose and, for each screen,
\* the head pose while the person looks at its centre and four corners, then
\* saves them to a private profile. It never runs instead of the generic,
\* uncalibrated reading (#35); coarse gaze (#36) uses the saved profile only
\* when one is present, and a cancelled session leaves the old profile alone.
\*
\* step counts targets from 0 to Steps - 1 (the resting pose, then each
\* screen's five points); captured is whether the running step has a pose.
\* camera abstracts the frame pipeline that feeds poses in (#38): while it is
\* down, no pose can be captured, a session cannot start, and the real code's
\* reader simply gets no frames, which this model turns into the camera
\* interplay below. MaxOutages bounds how many times the camera can be lost,
\* this model's form of "the camera eventually comes back".
\*
\* Every action below is one indivisible step, which is only true of the
\* code (#128) because CalibrationSession (src/calibration.h) guards its
\* own state -- step, captured, the running sample, the saved profile's
\* in-progress points, camera_up_ -- with one mutex, taken for the whole
\* body of Start, Next, Cancel, Update, OnCameraLost and OnCameraUp alike:
\* whichever of those a connection's own thread (Start, Next, Cancel),
\* the inference queue (Update) or the daemon's control queue
\* (OnCameraLost, OnCameraUp) calls next waits for the one already in
\* progress to finish first, the same serialization this spec's own
\* single next-action-at-a-time semantics already assumes.
EXTENDS Naturals
CONSTANTS GuardCapture, GuardStartCamera, GuardCameraLost, GuardCancelDiscards,
          Steps, MaxOutages
VARIABLES state, step, captured, profile, camera, unsafeAdvance, cancelWrote,
          outages
vars == <<state, step, captured, profile, camera, unsafeAdvance, cancelWrote,
          outages>>

States == {"idle", "running"}
Profiles == {"old", "new"}

TypeOK == /\ state \in States /\ step \in 0..Steps
          /\ captured \in BOOLEAN /\ profile \in Profiles
          /\ camera \in {"up", "down"} /\ unsafeAdvance \in BOOLEAN
          /\ cancelWrote \in BOOLEAN /\ outages \in 0..MaxOutages

Init == /\ state = "idle" /\ step = 0 /\ captured = FALSE /\ profile = "old"
        /\ camera = "up" /\ unsafeAdvance = FALSE /\ cancelWrote = FALSE
        /\ outages = 0

\* Starting needs an idle session and, ordinarily, a camera: a session with
\* nothing to capture would only wait forever.
Start == /\ state = "idle" /\ (camera = "up" \/ ~GuardStartCamera)
         /\ state' = "running" /\ step' = 0 /\ captured' = FALSE
         /\ UNCHANGED <<profile, camera, unsafeAdvance, cancelWrote, outages>>

\* A frame arrives for the step now prompted.
Capture == /\ state = "running" /\ camera = "up"
           /\ captured' = TRUE
           /\ UNCHANGED <<state, step, profile, camera, unsafeAdvance,
                         cancelWrote, outages>>

\* Moves on to the next target. Ordinarily this needs a captured pose for
\* the one just finished (GuardCapture); unsafeAdvance remembers whenever it
\* did not, which can only happen with the guard disabled, since otherwise
\* the action is not even enabled.
Advance == /\ state = "running" /\ step < Steps - 1
           /\ (captured \/ ~GuardCapture)
           /\ step' = step + 1 /\ captured' = FALSE
           /\ unsafeAdvance' = (unsafeAdvance \/ ~captured)
           /\ UNCHANGED <<state, profile, camera, cancelWrote, outages>>

\* The last target's pose finishes the session: the profile is saved
\* atomically and the session returns to idle.
Finish == /\ state = "running" /\ step = Steps - 1
          /\ (captured \/ ~GuardCapture)
          /\ state' = "idle" /\ step' = 0 /\ captured' = FALSE
          /\ profile' = "new"
          /\ unsafeAdvance' = (unsafeAdvance \/ ~captured)
          /\ UNCHANGED <<camera, cancelWrote, outages>>

\* Cancelling discards the session in progress. Ordinarily the saved
\* profile is untouched (GuardCancelDiscards); cancelWrote remembers
\* whenever cancelling saved anyway.
Cancel == /\ state = "running"
          /\ state' = "idle" /\ step' = 0 /\ captured' = FALSE
          /\ profile' = IF GuardCancelDiscards THEN profile ELSE "new"
          /\ cancelWrote' = (cancelWrote \/ ~GuardCancelDiscards)
          /\ UNCHANGED <<camera, unsafeAdvance, outages>>

\* The camera is lost. Ordinarily that cancels a running session the same
\* way (GuardCameraLost), since there is nothing left to capture; with the
\* guard disabled the session is left running with the camera down, which
\* NoRunningWithoutCamera below catches. Bounded by MaxOutages, the model's
\* form of "the camera eventually stops being lost".
CameraLost == /\ camera = "up" /\ outages < MaxOutages
              /\ camera' = "down" /\ outages' = outages + 1
              /\ IF GuardCameraLost
                   THEN state' = "idle" /\ step' = 0 /\ captured' = FALSE
                   ELSE UNCHANGED <<state, step, captured>>
              /\ UNCHANGED <<profile, unsafeAdvance, cancelWrote>>

\* The camera comes back.
CameraBack == /\ camera = "down" /\ camera' = "up"
              /\ UNCHANGED <<state, step, captured, profile, unsafeAdvance,
                            cancelWrote, outages>>

Next == Start \/ Capture \/ Advance \/ Finish \/ Cancel \/ CameraLost
           \/ CameraBack

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Capture) /\ WF_vars(Advance) /\ WF_vars(Finish)
        /\ WF_vars(CameraBack)

\* Every target needs its own pose before the session moves past it.
NoAdvanceWithoutCapture == ~unsafeAdvance
\* Cancelling never saves the session in progress.
NoSaveOnCancel == ~cancelWrote
\* A session only runs while the camera is up: starting needs it, and
\* losing it ends the session.
NoRunningWithoutCamera == state = "running" => camera = "up"
\* Something must happen: a running session always finishes or is
\* cancelled, so it can never be left running forever.
SessionEnds == state = "running" ~> state = "idle"
====
