---- MODULE CalibrationMenu ----
\* The menu's own side of an optional calibration session (#118, item 12 of
\* #61, #36, #35, #135, #137): the wizard tools/menu/main.mm's and
\* main_linux.cc's "Calibrate..." entry drives against the daemon's
\* Service, over the socket client neither menu had before
\* (src/calibration_flow.h's CalibrationFlow). specs/Calibration.tla
\* already models the session the daemon runs; this spec is the client's
\* own small lifecycle on top of it, which that spec does not cover: what
\* the wizard shows while it waits on the person, what a daemon or camera
\* failure mid-flow does to it, what closing the window has to do before
\* the socket goes away, and (#135, #137) that every screen gets its own
\* window move and prompt, and the wizard ends by saying what it did.
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
\* modeled here as NextFails or NextRefused (which also now covers a
\* refusal for #137's weak-step reason, specs/Calibration.tla's Refuse:
\* the client's handling is the same request/reply shape either way, so
\* it needs no action of its own). cameraUp is the daemon's own camera,
\* which this flow cannot see directly, only infer from a "calibrate
\* start" refusal; cameraWaits counts the retries this flow has already
\* spent waiting for it, bounded by MaxCameraWaits the way
\* specs/Calibration.tla's MaxOutages bounds a camera outage. resultShown
\* abstracts whether the wizard has the count #135/#137 ends on ("Calibrated
\* N screens."), earned again for each flow. windowOnScreen abstracts
\* whether the window has been moved onto the screen the step now showing
\* names (#135): a per-step, not a per-flow, fact, since each step can
\* name a different screen.
EXTENDS Naturals
CONSTANTS GuardFailOnDisconnect, GuardCancelOnClose, GuardCancelOnRefusal,
          GuardShowResult, GuardMoveWindow, MaxCameraWaits
VARIABLES state, sessionLive, cancelSent, danglingSession, cameraUp,
          cameraWaits, resultShown, windowOnScreen, unshownResult,
          unmovedWindow
vars == <<state, sessionLive, cancelSent, danglingSession, cameraUp,
          cameraWaits, resultShown, windowOnScreen, unshownResult,
          unmovedWindow>>

States == {"closed", "opening", "prompting", "closing", "done", "failed"}

TypeOK ==
    /\ state \in States /\ sessionLive \in BOOLEAN
    /\ cancelSent \in BOOLEAN /\ danglingSession \in BOOLEAN
    /\ cameraUp \in BOOLEAN /\ cameraWaits \in 0..MaxCameraWaits
    /\ resultShown \in BOOLEAN /\ windowOnScreen \in BOOLEAN
    /\ unshownResult \in BOOLEAN /\ unmovedWindow \in BOOLEAN

Init ==
    /\ state = "closed" /\ sessionLive = FALSE /\ cancelSent = FALSE
    /\ danglingSession = FALSE
    \* The camera starts down, the way a freshly launched daemon's does
    \* (specs/Calibration.tla's own DaemonCalibration::Start note): the
    \* worst case for a cold start, #128's first finding.
    /\ cameraUp = FALSE /\ cameraWaits = 0
    /\ resultShown = FALSE /\ windowOnScreen = FALSE
    /\ unshownResult = FALSE /\ unmovedWindow = FALSE

\* "Calibrate..." is chosen, the socket connects, hello and "calibrate
\* start" are both answered "ok": CalibrationFlow::Start reaches
\* kPrompting, having moved the window onto the first step's screen
\* (ordinarily, GuardMoveWindow; #135). Needs the camera up:
\* calibration.cc's Start refuses otherwise, which is OpenWaitsForCamera's
\* and RetrySucceeds's case instead.
OpenSucceeds ==
    /\ state = "closed" /\ cameraUp
    /\ state' = "prompting" /\ sessionLive' = TRUE
    /\ cancelSent' = FALSE /\ resultShown' = FALSE
    /\ windowOnScreen' = GuardMoveWindow
    /\ unmovedWindow' = (unmovedWindow \/ ~GuardMoveWindow)
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits, unshownResult>>

\* Connecting or hello fails or is refused, or "calibrate start" is refused
\* for a reason other than the camera (such as a session already running):
\* CalibrationFlow::Start reaches kFailed with nothing left running. While
\* the camera is down, "calibrate start" can only ever be refused for the
\* camera, which is OpenWaitsForCamera's case, not this one.
OpenFails ==
    /\ state = "closed"
    /\ state' = "failed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits, resultShown, windowOnScreen, unshownResult,
                  unmovedWindow>>

\* "calibrate start" is refused because the camera is not up yet (#128):
\* rather than failing outright on a cold start, CalibrationFlow::Start
\* retries it, up to MaxCameraWaits times.
OpenWaitsForCamera ==
    /\ state = "closed" /\ ~cameraUp /\ cameraWaits < MaxCameraWaits
    /\ state' = "opening" /\ cameraWaits' = cameraWaits + 1
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  resultShown, windowOnScreen, unshownResult, unmovedWindow>>

\* A retry while waiting for the camera finds it up now: the retried
\* "calibrate start" succeeds, moving the window the same way OpenSucceeds
\* does.
RetrySucceeds ==
    /\ state = "opening" /\ cameraUp
    /\ state' = "prompting" /\ sessionLive' = TRUE /\ cancelSent' = FALSE
    /\ resultShown' = FALSE /\ windowOnScreen' = GuardMoveWindow
    /\ unmovedWindow' = (unmovedWindow \/ ~GuardMoveWindow)
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits, unshownResult>>

\* The camera is still down and there is patience left: retry again.
RetryWaits ==
    /\ state = "opening" /\ ~cameraUp /\ cameraWaits < MaxCameraWaits
    /\ cameraWaits' = cameraWaits + 1
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession, cameraUp,
                  resultShown, windowOnScreen, unshownResult, unmovedWindow>>

\* The camera never came up within MaxCameraWaits retries: give up.
RetryGivesUp ==
    /\ state = "opening" /\ ~cameraUp /\ cameraWaits >= MaxCameraWaits
    /\ state' = "failed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits, resultShown, windowOnScreen, unshownResult,
                  unmovedWindow>>

\* The daemon's camera comes up, on its own schedule (Calibration.tla's
\* CameraBack), unrelated to anything this flow does.
CameraComesUp ==
    /\ ~cameraUp
    /\ cameraUp' = TRUE
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession,
                  cameraWaits, resultShown, windowOnScreen, unshownResult,
                  unmovedWindow>>

\* Next is pressed and the daemon's reply is another step still running,
\* on whatever screen that step names: the window moves onto it the same
\* way OpenSucceeds's first step does (#135).
NextContinues ==
    /\ state = "prompting" /\ sessionLive
    /\ windowOnScreen' = GuardMoveWindow
    /\ unmovedWindow' = (unmovedWindow \/ ~GuardMoveWindow)
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits, resultShown, unshownResult>>

\* Next is pressed and the daemon's reply is the session's last step,
\* saved: CalibrationFlow::Next reaches kDone, showing the count it ends
\* on (ordinarily, GuardShowResult; #135, #137: "Calibrated N screens.").
NextFinishes ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "done" /\ sessionLive' = FALSE
    /\ resultShown' = GuardShowResult
    /\ unshownResult' = (unshownResult \/ ~GuardShowResult)
    /\ UNCHANGED <<cancelSent, danglingSession, cameraUp, cameraWaits,
                  windowOnScreen, unmovedWindow>>

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
    /\ UNCHANGED <<cancelSent, danglingSession, cameraUp, cameraWaits,
                  resultShown, windowOnScreen, unshownResult, unmovedWindow>>

\* Next is pressed and the daemon refuses it while its own session is
\* still running (src/calibration.cc's Next: no sample captured yet for
\* this step, its save failed, or #137's weak-step check, specs/
\* Calibration.tla's Refuse) -- unlike NextFails, nothing outside this
\* flow has ended the session, so somebody has to tell the daemon to give
\* it up (#128). Ordinarily (GuardCancelOnRefusal) CalibrationFlow::Next
\* sends "calibrate cancel" before reporting the failure, in the same call
\* (its ReadLine is synchronous, the same reasoning as ClosingCompletes
\* below), so sessionLive ends FALSE either way the cancel itself turns
\* out; with the guard disabled it reports the failure and leaves the
\* session exactly as it was, which SessionEndsWithFlow below catches.
NextRefused ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "failed"
    /\ sessionLive' = IF GuardCancelOnRefusal THEN FALSE ELSE sessionLive
    /\ cancelSent' = (cancelSent \/ GuardCancelOnRefusal)
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits, resultShown,
                  windowOnScreen, unshownResult, unmovedWindow>>

\* Cancel is pressed: CalibrationFlow::Cancel always sends "calibrate
\* cancel" and waits for its reply before the window goes away.
UserCancel ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "closing"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits, resultShown, windowOnScreen, unshownResult,
                  unmovedWindow>>

\* The window's own close control, not the Cancel button
\* (CalibrationFlow::WindowClosed). Ordinarily (GuardCancelOnClose) this
\* is exactly UserCancel under another name, so the session a person
\* walked away from is still cleaned up; with the guard disabled it tears
\* the window down without ever telling the daemon, leaving sessionLive
\* running with nobody left to finish or cancel it -- an orphaned
\* session, which danglingSession remembers and NoOrphanOnClose catches.
WindowClosed ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = IF GuardCancelOnClose THEN "closing" ELSE "closed"
    /\ UNCHANGED <<sessionLive, cancelSent, cameraUp, cameraWaits,
                  resultShown, windowOnScreen, unshownResult, unmovedWindow>>
    /\ danglingSession' = (danglingSession \/
                           (~GuardCancelOnClose /\ sessionLive /\ ~cancelSent))

\* "calibrate cancel"'s reply arrives, and it is "ok": the window closes
\* having told the daemon.
CancelSucceeds ==
    /\ state = "closing"
    /\ state' = "closed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits, resultShown,
                  windowOnScreen, unshownResult, unmovedWindow>>

\* "calibrate cancel" never gets a reply, or the connection is already
\* gone by the time it is sent: the attempt was still made, so this is
\* not an orphan, only a flow that could not confirm it.
CancelFails ==
    /\ state = "closing"
    /\ state' = "failed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE
    /\ UNCHANGED <<danglingSession, cameraUp, cameraWaits, resultShown,
                  windowOnScreen, unshownResult, unmovedWindow>>

\* The person dismisses a finished or failed flow's window: Settings can
\* be opened to calibrate again later.
Dismiss ==
    /\ state \in {"done", "failed"}
    /\ state' = "closed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession, cameraUp,
                  cameraWaits, resultShown, windowOnScreen, unshownResult,
                  unmovedWindow>>

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

\* A finished flow always shows the count it ended on (#135, #137): the
\* wizard never reaches "done" silently, the way #136 found it did.
ResultShownOnDone == ~unshownResult

\* Every step shown while prompting has had the window moved onto the
\* screen it names (#135): the wizard never leaves the window sitting on
\* the wrong screen for a step that names another one.
WindowMovedWhilePrompting == ~unmovedWindow

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
