---- MODULE CalibrationMenu ----
\* The menu's own side of an optional calibration session (#118, item 12 of
\* #61, #36, #35): the wizard tools/menu/main.mm's and main_linux.cc's
\* "Calibrate..." entry drives against the daemon's Service, over the
\* socket client neither menu had before (src/calibration_flow.h's
\* CalibrationFlow). specs/Calibration.tla already models the session the
\* daemon runs; this spec is the client's own small lifecycle on top of it,
\* which that spec does not cover: what the wizard shows while it waits on
\* the person, what a daemon or camera failure mid-flow does to it, and
\* what closing the window has to do before the socket goes away.
\*
\* state is this one flow's own view: "closed" (no window open, or a
\* previous flow's outcome dismissed), "opening" (hello succeeded but
\* "calibrate start" was refused only because the camera is not up yet
\* (#128): the flow keeps retrying rather than giving up), "prompting" (a
\* step is shown, waiting on Next or Cancel), "closing" (Cancel or the
\* window's close button was pressed, and "calibrate cancel" is on the
\* wire, waiting for its reply), "done" (the last step finished and the
\* profile saved) or "failed" (the connection or the daemon refused).
\* sessionLive abstracts whether the daemon still has a session this flow
\* is responsible for: CalibrationFlow never gets an unsolicited push when
\* the daemon's own session ends on its own (specs/Calibration.tla's
\* CameraLost, say), so it only learns that at its own next request,
\* modeled here as NextFails or NextRefused. cameraUp is the daemon's own
\* camera, which this flow cannot see directly, only infer from a
\* "calibrate start" refusal; cameraWaits counts the retries this flow has
\* already spent waiting for it, bounded by MaxCameraWaits the way
\* specs/Calibration.tla's MaxOutages bounds a camera outage.
EXTENDS Naturals
CONSTANTS GuardFailOnDisconnect, GuardCancelOnClose, GuardCancelOnRefusal,
          MaxCameraWaits
VARIABLES state, sessionLive, cancelSent, danglingSession, cameraUp,
          cameraWaits
vars == <<state, sessionLive, cancelSent, danglingSession, cameraUp,
          cameraWaits>>

States == {"closed", "opening", "prompting", "closing", "done", "failed"}

TypeOK ==
    /\ state \in States /\ sessionLive \in BOOLEAN
    /\ cancelSent \in BOOLEAN /\ danglingSession \in BOOLEAN
    /\ cameraUp \in BOOLEAN /\ cameraWaits \in 0..MaxCameraWaits

Init ==
    /\ state = "closed" /\ sessionLive = FALSE /\ cancelSent = FALSE
    /\ danglingSession = FALSE
    \* The camera starts down, the way a freshly launched daemon's does
    \* (specs/Calibration.tla's own DaemonCalibration::Start note): the
    \* worst case for a cold start, #128's first finding.
    /\ cameraUp = FALSE /\ cameraWaits = 0

\* "Calibrate..." is chosen, the socket connects, hello and "calibrate
\* start" are both answered "ok": CalibrationFlow::Start reaches
\* kPrompting. Needs the camera up: calibration.cc's Start refuses
\* otherwise, which is OpenWaitsForCamera's and RetrySucceeds's case
\* instead.
OpenSucceeds ==
    /\ state = "closed" /\ cameraUp
    /\ state' = "prompting" /\ sessionLive' = TRUE
    /\ cancelSent' = FALSE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits>>

\* Connecting or hello fails or is refused, or "calibrate start" is refused
\* for a reason other than the camera (such as a session already running):
\* CalibrationFlow::Start reaches kFailed with nothing left running. While
\* the camera is down, "calibrate start" can only ever be refused for the
\* camera, which is OpenWaitsForCamera's case, not this one.
OpenFails ==
    /\ state = "closed"
    /\ state' = "failed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits>>

\* "calibrate start" is refused because the camera is not up yet (#128):
\* rather than failing outright on a cold start, CalibrationFlow::Start
\* retries it, up to MaxCameraWaits times.
OpenWaitsForCamera ==
    /\ state = "closed" /\ ~cameraUp /\ cameraWaits < MaxCameraWaits
    /\ state' = "opening" /\ cameraWaits' = cameraWaits + 1
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp>>

\* A retry while waiting for the camera finds it up now: the retried
\* "calibrate start" succeeds.
RetrySucceeds ==
    /\ state = "opening" /\ cameraUp
    /\ state' = "prompting" /\ sessionLive' = TRUE /\ cancelSent' = FALSE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits>>

\* The camera is still down and there is patience left: retry again.
RetryWaits ==
    /\ state = "opening" /\ ~cameraUp /\ cameraWaits < MaxCameraWaits
    /\ cameraWaits' = cameraWaits + 1
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession, cameraUp>>

\* The camera never came up within MaxCameraWaits retries: give up.
RetryGivesUp ==
    /\ state = "opening" /\ ~cameraUp /\ cameraWaits >= MaxCameraWaits
    /\ state' = "failed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits>>

\* The daemon's camera comes up, on its own schedule (Calibration.tla's
\* CameraBack), unrelated to anything this flow does.
CameraComesUp ==
    /\ ~cameraUp
    /\ cameraUp' = TRUE
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession,
                  cameraWaits>>

\* Next is pressed and the daemon's reply is another step still running.
NextContinues ==
    /\ state = "prompting" /\ sessionLive
    /\ UNCHANGED vars

\* Next is pressed and the daemon's reply is the session's last step,
\* saved: CalibrationFlow::Next reaches kDone.
NextFinishes ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "done" /\ sessionLive' = FALSE
    /\ UNCHANGED <<cancelSent, danglingSession, cameraUp, cameraWaits>>

\* Next is pressed and the daemon errors or hangs up because it lost the
\* session mid-flow on its own -- the camera went away, or the process is
\* simply gone -- so sessionLive was already on its way to FALSE with
\* nothing this flow could have cancelled: ordinarily CalibrationFlow::Next
\* notices and reaches kFailed (GuardFailOnDisconnect); with the guard
\* disabled it keeps showing the step that is no longer real, which
\* NoStalePrompt below catches.
NextFails ==
    /\ state = "prompting" /\ sessionLive
    /\ sessionLive' = FALSE
    /\ state' = IF GuardFailOnDisconnect THEN "failed" ELSE state
    /\ UNCHANGED <<cancelSent, danglingSession, cameraUp, cameraWaits>>

\* Next is pressed and the daemon refuses it while its own session is
\* still running (src/calibration.cc's Next: no pose captured yet for
\* this step, or its save failed) -- unlike NextFails, nothing outside
\* this flow has ended the session, so somebody has to tell the daemon to
\* give it up (#128). Ordinarily (GuardCancelOnRefusal)
\* CalibrationFlow::Next sends "calibrate cancel" before reporting the
\* failure, in the same call (its ReadLine is synchronous, the same
\* reasoning as ClosingCompletes below), so sessionLive ends FALSE either
\* way the cancel itself turns out; with the guard disabled it reports the
\* failure and leaves the session exactly as it was, which
\* SessionEndsWithFlow below catches.
NextRefused ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "failed"
    /\ sessionLive' = IF GuardCancelOnRefusal THEN FALSE ELSE sessionLive
    /\ cancelSent' = (cancelSent \/ GuardCancelOnRefusal)
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits>>

\* Cancel is pressed: CalibrationFlow::Cancel always sends "calibrate
\* cancel" and waits for its reply before the window goes away.
UserCancel ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "closing"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits>>

\* The window's own close button, not the Cancel button
\* (CalibrationFlow::WindowClosed). Ordinarily (GuardCancelOnClose) this
\* is exactly UserCancel under another name, so the session a person
\* walked away from is still cleaned up; with the guard disabled it tears
\* the window down without ever telling the daemon, leaving sessionLive
\* running with nobody left to finish or cancel it -- an orphaned
\* session, which danglingSession remembers and NoOrphanOnClose catches.
WindowClosed ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = IF GuardCancelOnClose THEN "closing" ELSE "closed"
    /\ UNCHANGED <<sessionLive, cancelSent, cameraUp, cameraWaits>>
    /\ danglingSession' = (danglingSession \/
                           (~GuardCancelOnClose /\ sessionLive /\ ~cancelSent))

\* "calibrate cancel"'s reply arrives, and it is "ok": the window closes
\* having told the daemon.
CancelSucceeds ==
    /\ state = "closing"
    /\ state' = "closed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits>>

\* "calibrate cancel" never gets a reply, or the connection is already
\* gone by the time it is sent: the attempt was still made, so this is
\* not an orphan, only a flow that could not confirm it.
CancelFails ==
    /\ state = "closing"
    /\ state' = "failed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits>>

\* The person dismisses a finished or failed flow's window: Settings can
\* be opened to calibrate again later.
Dismiss ==
    /\ state \in {"done", "failed"}
    /\ state' = "closed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits>>

Next == OpenSucceeds \/ OpenFails \/ OpenWaitsForCamera \/ RetrySucceeds
           \/ RetryWaits \/ RetryGivesUp \/ CameraComesUp \/ NextContinues
           \/ NextFinishes \/ NextFails \/ NextRefused \/ UserCancel
           \/ WindowClosed \/ CancelSucceeds \/ CancelFails \/ Dismiss

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(CancelSucceeds) /\ WF_vars(CancelFails)
        /\ WF_vars(RetrySucceeds) /\ WF_vars(RetryWaits)
        /\ WF_vars(RetryGivesUp)

\* Whatever the wizard shows as "prompting" is backed by a session the
\* daemon still has: the flow never keeps showing a step once it knows
\* (from a guard above) that the session is gone.
NoStalePrompt == state = "prompting" => sessionLive

\* Closing the window never leaves a session running with nobody left to
\* answer for it.
NoOrphanOnClose == ~danglingSession

\* A flow that has reported done or failed never still has a session the
\* daemon thinks is running: whatever ended this flow's own view also
\* ended, or already found ended, the one it was responsible for (#128).
SessionEndsWithFlow == state \in {"failed", "done"} => ~sessionLive

\* Something must happen: once Cancel or the window's close button has
\* asked the daemon to end the session, that always finishes, one way or
\* another, rather than leaving the window (and the socket write it is
\* waiting on) stuck forever.
ClosingCompletes == state = "closing" ~> state \in {"closed", "failed"}

\* Something must happen: a cold start that keeps the wizard waiting for
\* the camera always resolves, once the camera comes up or the wait is
\* spent, rather than leaving the window showing nothing, waiting on a
\* retry nobody ever makes.
CameraWaitResolves == state = "opening" ~> state \in {"prompting", "failed"}
====
