---- MODULE Calibration ----
\* facepalm's optional calibration session (item 12 of #61, #36, #35, #137):
\* a short guided session that records the resting pose, a gains step
\* (#137's fixate-and-turn, which measures how much the person's eyes move
\* per degree the head turns) and, for each screen, the gaze -- head pose
\* composed with eye-in-head signal, not head pose alone -- while the
\* person looks at its centre and four corners, then saves them to a
\* private profile. It never runs instead of the generic, uncalibrated
\* reading (#35); coarse gaze (#36) uses the saved profile only when one is
\* present, and a cancelled session leaves the old profile alone.
\*
\* step counts targets from 0 to Steps - 1 (the resting pose, the gains
\* step, then each screen's five points); captured is whether the running
\* step has a sample. weak abstracts a captured step whose sample is too
\* thin to use -- the gains step with too little head travel, or a
\* screen's point too close to its own centre (#137's spread check,
\* #136's original finding) -- read nondeterministically on every Capture,
\* since this model does not need the real head and eye numbers to prove
\* what happens once a step is weak. camera abstracts the frame pipeline
\* that feeds samples in (#38): while it is down, no sample can be
\* captured, a session cannot start, and the real code's reader simply
\* gets no frames, which this model turns into the camera interplay below.
\* MaxOutages bounds how many times the camera can be lost, this model's
\* form of "the camera eventually comes back". applied abstracts whether
\* the daemon's own live reader (ScreenContextReporter) is already reading
\* a just-finished profile, or still reading the one from before (#136:
\* a profile used to take effect only after Turn Off and On); Restart
\* abstracts that manual step, always available, so a stale applied value
\* is never stuck forever even with GuardApplyImmediately off.
\*
\* Every action below but Restart is one indivisible step, which is only
\* true of the code (#128) because CalibrationSession (src/calibration.h)
\* guards its own state -- step, captured, weak, the running sample, the
\* saved profile's in-progress points, camera_up_ -- with one mutex, taken
\* for the whole body of Start, Next, Cancel, Update, OnCameraLost and
\* OnCameraUp alike: whichever of those a connection's own thread (Start,
\* Next, Cancel), the inference queue (Update) or the daemon's control
\* queue (OnCameraLost, OnCameraUp) calls next waits for the one already in
\* progress to finish first, the same serialization this spec's own
\* single next-action-at-a-time semantics already assumes. Restart is the
\* daemon's own composition root (tools/daemon/main.cc), swapping in
\* whatever CalibrationSession's on_finished callback last handed it --
\* unrelated to that mutex, since nothing of CalibrationSession's own state
\* changes.
EXTENDS Naturals
CONSTANTS GuardCapture, GuardStartCamera, GuardCameraLost, GuardCancelDiscards,
          GuardWeakStep, GuardApplyImmediately, Steps, MaxOutages
VARIABLES state, step, captured, weak, profile, applied, camera,
          unsafeAdvance, unsafeWeakAdvance, cancelWrote, outages, staleApply
vars == <<state, step, captured, weak, profile, applied, camera,
          unsafeAdvance, unsafeWeakAdvance, cancelWrote, outages, staleApply>>

States == {"idle", "running"}
Profiles == {"old", "new"}

TypeOK == /\ state \in States /\ step \in 0..Steps
          /\ captured \in BOOLEAN /\ weak \in BOOLEAN
          /\ profile \in Profiles /\ applied \in Profiles
          /\ camera \in {"up", "down"} /\ unsafeAdvance \in BOOLEAN
          /\ unsafeWeakAdvance \in BOOLEAN
          /\ cancelWrote \in BOOLEAN /\ outages \in 0..MaxOutages
          /\ staleApply \in BOOLEAN

Init == /\ state = "idle" /\ step = 0 /\ captured = FALSE /\ weak = FALSE
        /\ profile = "old" /\ applied = "old"
        /\ camera = "up" /\ unsafeAdvance = FALSE /\ unsafeWeakAdvance = FALSE
        /\ cancelWrote = FALSE /\ outages = 0 /\ staleApply = FALSE

\* Starting needs an idle session and, ordinarily, a camera: a session with
\* nothing to capture would only wait forever.
Start == /\ state = "idle" /\ (camera = "up" \/ ~GuardStartCamera)
         /\ state' = "running" /\ step' = 0 /\ captured' = FALSE
         /\ weak' = FALSE
         /\ UNCHANGED <<profile, applied, camera, unsafeAdvance,
                       unsafeWeakAdvance, cancelWrote, outages, staleApply>>

\* A frame arrives for the step now prompted, finalizing whether its sample
\* is weak (#137's gains travel and spread checks) exactly once, the way
\* the real code's own average is settled once at "calibrate next" (not
\* re-read frame by frame): ~captured keeps either from firing again until
\* Refuse, Advance, Finish or Start resets it for the next attempt.
\* SF_vars(CaptureGood) below needs strong, not weak, fairness: CaptureGood
\* is only ever enabled for the instant captured is FALSE, so a scheduler
\* that always answers with CaptureWeak the moment a step opens would
\* starve a merely weak requirement (CaptureGood is not continuously
\* enabled, only infinitely often), even though a real person who keeps
\* retrying a refused step keeps getting another chance to give a good one.
CaptureGood == /\ state = "running" /\ camera = "up" /\ ~captured
               /\ captured' = TRUE /\ weak' = FALSE
               /\ UNCHANGED <<state, step, profile, applied, camera,
                             unsafeAdvance, unsafeWeakAdvance, cancelWrote,
                             outages, staleApply>>
CaptureWeak == /\ state = "running" /\ camera = "up" /\ ~captured
               /\ captured' = TRUE /\ weak' = TRUE
               /\ UNCHANGED <<state, step, profile, applied, camera,
                             unsafeAdvance, unsafeWeakAdvance, cancelWrote,
                             outages, staleApply>>

\* A weak step's sample is refused rather than kept (#137's NoSaveWithWeakStep,
\* ordinarily, under GuardWeakStep): the step stays where it is, waiting on
\* a fresh capture, exactly as if none had been taken yet.
Refuse == /\ state = "running" /\ weak /\ GuardWeakStep
          /\ captured' = FALSE /\ weak' = FALSE
          /\ UNCHANGED <<state, step, profile, applied, camera,
                        unsafeAdvance, unsafeWeakAdvance, cancelWrote,
                        outages, staleApply>>

\* Moves on to the next target. Ordinarily this needs a captured sample for
\* the one just finished (GuardCapture) that is not weak (GuardWeakStep);
\* unsafeAdvance and unsafeWeakAdvance remember whenever it was missing or
\* weak anyway, which can only happen with the matching guard disabled,
\* since otherwise the action is not even enabled.
Advance == /\ state = "running" /\ step < Steps - 1
           /\ (captured \/ ~GuardCapture) /\ (~weak \/ ~GuardWeakStep)
           /\ step' = step + 1 /\ captured' = FALSE /\ weak' = FALSE
           /\ unsafeAdvance' = (unsafeAdvance \/ ~captured)
           /\ unsafeWeakAdvance' = (unsafeWeakAdvance \/ weak)
           /\ UNCHANGED <<state, profile, applied, camera, cancelWrote,
                         outages, staleApply>>

\* The last target's sample finishes the session: the profile is saved
\* atomically, applied at once (ordinarily, GuardApplyImmediately) and the
\* session returns to idle. staleApply remembers whenever a finish left
\* applied behind profile, which can only happen with GuardApplyImmediately
\* off (#136's "applied only after Turn Off and On").
Finish == /\ state = "running" /\ step = Steps - 1
          /\ (captured \/ ~GuardCapture) /\ (~weak \/ ~GuardWeakStep)
          /\ state' = "idle" /\ step' = 0 /\ captured' = FALSE
          /\ weak' = FALSE /\ profile' = "new"
          /\ applied' = IF GuardApplyImmediately THEN "new" ELSE applied
          /\ unsafeAdvance' = (unsafeAdvance \/ ~captured)
          /\ unsafeWeakAdvance' = (unsafeWeakAdvance \/ weak)
          /\ staleApply' = (staleApply \/ ~GuardApplyImmediately)
          /\ UNCHANGED <<camera, cancelWrote, outages>>

\* Cancelling discards the session in progress. Ordinarily the saved
\* profile is untouched (GuardCancelDiscards); cancelWrote remembers
\* whenever cancelling saved anyway.
Cancel == /\ state = "running"
          /\ state' = "idle" /\ step' = 0 /\ captured' = FALSE
          /\ weak' = FALSE
          /\ profile' = IF GuardCancelDiscards THEN profile ELSE "new"
          /\ cancelWrote' = (cancelWrote \/ ~GuardCancelDiscards)
          /\ UNCHANGED <<applied, camera, unsafeAdvance, unsafeWeakAdvance,
                        outages, staleApply>>

\* The camera is lost. Ordinarily that cancels a running session the same
\* way (GuardCameraLost), since there is nothing left to capture; with the
\* guard disabled the session is left running with the camera down, which
\* NoRunningWithoutCamera below catches. Bounded by MaxOutages, the model's
\* form of "the camera eventually stops being lost".
CameraLost == /\ camera = "up" /\ outages < MaxOutages
              /\ camera' = "down" /\ outages' = outages + 1
              /\ IF GuardCameraLost
                   THEN state' = "idle" /\ step' = 0 /\ captured' = FALSE
                        /\ weak' = FALSE
                   ELSE UNCHANGED <<state, step, captured, weak>>
              /\ UNCHANGED <<profile, applied, unsafeAdvance,
                            unsafeWeakAdvance, cancelWrote, staleApply>>

\* The camera comes back.
CameraBack == /\ camera = "down" /\ camera' = "up"
              /\ UNCHANGED <<state, step, captured, weak, profile, applied,
                            unsafeAdvance, unsafeWeakAdvance, cancelWrote,
                            outages, staleApply>>

\* The daemon's composition root swaps in whatever profile
\* CalibrationSession's on_finished callback last handed it (#136, #137):
\* with GuardApplyImmediately already keeping applied current, this is a
\* no-op; with it off, this is the restart that used to be the only way,
\* so a stale applied value is never stuck forever (ProfileEventuallyApplied
\* below).
Restart == /\ applied' = profile
           /\ UNCHANGED <<state, step, captured, weak, profile, camera,
                         unsafeAdvance, unsafeWeakAdvance, cancelWrote,
                         outages, staleApply>>

Next == Start \/ CaptureGood \/ CaptureWeak \/ Refuse \/ Advance \/ Finish
           \/ Cancel \/ CameraLost \/ CameraBack \/ Restart

Spec == /\ Init /\ [][Next]_vars
        /\ SF_vars(CaptureGood) /\ WF_vars(Refuse) /\ WF_vars(Advance)
        /\ WF_vars(Finish) /\ WF_vars(CameraBack) /\ WF_vars(Restart)

\* Every target needs its own sample before the session moves past it.
NoAdvanceWithoutCapture == ~unsafeAdvance
\* A weak sample -- too little head travel in the gains step, or too little
\* spread from a screen's own centre -- is never advanced past or saved
\* over (#137, #136).
NoSaveWithWeakStep == ~unsafeWeakAdvance
\* Cancelling never saves the session in progress.
NoSaveOnCancel == ~cancelWrote
\* A session only runs while the camera is up: starting needs it, and
\* losing it ends the session.
NoRunningWithoutCamera == state = "running" => camera = "up"
\* Something must happen: a running session always finishes or is
\* cancelled, so it can never be left running forever.
SessionEnds == state = "running" ~> state = "idle"
\* Something must happen: a freshly saved profile is always eventually
\* read by the live reader, with or without GuardApplyImmediately (#136,
\* #137) -- promptly is NoStaleApply's job, below, not this one's.
ProfileEventuallyApplied == profile = "new" ~> applied = "new"
\* A freshly saved profile is read by the live reader at once, never
\* waiting for Restart (#136's "applied only after Turn Off and On",
\* fixed by #137's ScreenContextReporter swap on finish).
NoStaleApply == ~staleApply
====
