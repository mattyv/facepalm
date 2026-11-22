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
\* previous flow's outcome dismissed), "prompting" (a step is shown,
\* waiting on Next or Cancel), "closing" (Cancel or the window's close
\* button was pressed, and "calibrate cancel" is on the wire, waiting for
\* its reply), "done" (the last step finished and the profile saved) or
\* "failed" (the connection or the daemon refused). sessionLive abstracts
\* whether the daemon still has a session this flow is responsible for:
\* CalibrationFlow never gets an unsolicited push when the daemon's own
\* session ends on its own (specs/Calibration.tla's CameraLost, say), so it
\* only learns that at its own next request, modeled here as NextFails.
EXTENDS Naturals
CONSTANTS GuardFailOnDisconnect, GuardCancelOnClose
VARIABLES state, sessionLive, cancelSent, danglingSession
vars == <<state, sessionLive, cancelSent, danglingSession>>

States == {"closed", "prompting", "closing", "done", "failed"}

TypeOK ==
    /\ state \in States /\ sessionLive \in BOOLEAN
    /\ cancelSent \in BOOLEAN /\ danglingSession \in BOOLEAN

Init ==
    /\ state = "closed" /\ sessionLive = FALSE /\ cancelSent = FALSE
    /\ danglingSession = FALSE

\* "Calibrate..." is chosen, the socket connects, hello and "calibrate
\* start" are both answered "ok": CalibrationFlow::Start reaches
\* kPrompting.
OpenSucceeds ==
    /\ state = "closed"
    /\ state' = "prompting" /\ sessionLive' = TRUE
    /\ cancelSent' = FALSE /\ UNCHANGED danglingSession

\* Connecting, hello or the first "calibrate start" fails or is refused:
\* CalibrationFlow::Start reaches kFailed with nothing left running.
OpenFails ==
    /\ state = "closed"
    /\ state' = "failed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession>>

\* Next is pressed and the daemon's reply is another step still running.
NextContinues ==
    /\ state = "prompting" /\ sessionLive
    /\ UNCHANGED <<state, sessionLive, cancelSent, danglingSession>>

\* Next is pressed and the daemon's reply is the session's last step,
\* saved: CalibrationFlow::Next reaches kDone.
NextFinishes ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "done" /\ sessionLive' = FALSE
    /\ UNCHANGED <<cancelSent, danglingSession>>

\* Next is pressed and the daemon errors or hangs up (it lost the session
\* mid-flow -- the camera went away, or the process is simply gone):
\* ordinarily CalibrationFlow::Next notices and reaches kFailed
\* (GuardFailOnDisconnect); with the guard disabled it keeps showing the
\* step that is no longer real, which NoStalePrompt below catches.
NextFails ==
    /\ state = "prompting" /\ sessionLive
    /\ sessionLive' = FALSE
    /\ state' = IF GuardFailOnDisconnect THEN "failed" ELSE state
    /\ UNCHANGED <<cancelSent, danglingSession>>

\* Cancel is pressed: CalibrationFlow::Cancel always sends "calibrate
\* cancel" and waits for its reply before the window goes away.
UserCancel ==
    /\ state = "prompting" /\ sessionLive
    /\ state' = "closing"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession>>

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
    /\ UNCHANGED <<sessionLive, cancelSent>>
    /\ danglingSession' = (danglingSession \/
                           (~GuardCancelOnClose /\ sessionLive /\ ~cancelSent))

\* "calibrate cancel"'s reply arrives, and it is "ok": the window closes
\* having told the daemon.
CancelSucceeds ==
    /\ state = "closing"
    /\ state' = "closed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE /\ UNCHANGED danglingSession

\* "calibrate cancel" never gets a reply, or the connection is already
\* gone by the time it is sent: the attempt was still made, so this is
\* not an orphan, only a flow that could not confirm it.
CancelFails ==
    /\ state = "closing"
    /\ state' = "failed" /\ sessionLive' = FALSE
    /\ cancelSent' = TRUE /\ UNCHANGED danglingSession

\* The person dismisses a finished or failed flow's window: Settings can
\* be opened to calibrate again later.
Dismiss ==
    /\ state \in {"done", "failed"}
    /\ state' = "closed"
    /\ UNCHANGED <<sessionLive, cancelSent, danglingSession>>

Next == OpenSucceeds \/ OpenFails \/ NextContinues \/ NextFinishes
           \/ NextFails \/ UserCancel \/ WindowClosed \/ CancelSucceeds
           \/ CancelFails \/ Dismiss

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(CancelSucceeds) /\ WF_vars(CancelFails)

\* Whatever the wizard shows as "prompting" is backed by a session the
\* daemon still has: the flow never keeps showing a step once it knows
\* (from the guard above) that the session is gone.
NoStalePrompt == state = "prompting" => sessionLive

\* Closing the window never leaves a session running with nobody left to
\* answer for it.
NoOrphanOnClose == ~danglingSession

\* Something must happen: once Cancel or the window's close button has
\* asked the daemon to end the session, that always finishes, one way or
\* another, rather than leaving the window (and the socket write it is
\* waiting on) stuck forever.
ClosingCompletes == state = "closing" ~> state \in {"closed", "failed"}
====
