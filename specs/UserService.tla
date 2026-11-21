---- MODULE UserService -------------------------------------------------------
(***************************************************************************)
(* The menu turns facepalm on and off as a user service (#37): a launchd   *)
(* LaunchAgent on macOS, a systemd --user unit on Linux, following         *)
(* src/user_service.cc. Turning it on is the person's consent: it writes   *)
(* the service's file, which runs the daemon with --consent, and loads it. *)
(* Turning it off unloads it, which stops the daemon, and removes the      *)
(* file, so the next login does not start it again. The service manager    *)
(* restarts a daemon that failed, and not one that stopped cleanly.        *)
(*                                                                         *)
(* The daemon's own stop is specs/DaemonAdmission.tla's: a stopping daemon *)
(* exits in time (its StopEnds), with the camera closed (its ExitClean).   *)
(* Here it is one fair step.                                               *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)

CONSTANTS
    GuardUnloadOnTurnOff,  \* Turning off unloads the service.
    GuardRemoveOnTurnOff,  \* Turning off removes the service's file.
    GuardRestartOnFailure, \* A failed daemon is started again.
    GuardNoRestartOnStop,  \* A daemon that stopped cleanly stays stopped.
    GuardOffWaits          \* Turning off returns once the daemon is down.

VARIABLES
    on,        \* The person's choice in the menu.
    file,      \* Whether the service's file is installed.
    loaded,    \* Whether the service manager has the service loaded.
    daemon,    \* "down", "running" or "stopping".
    lastExit,  \* How the daemon last exited: "none", "failed" or "stopped".
    loggedIn   \* Whether the person's session, and its manager, run.

vars == <<on, file, loaded, daemon, lastExit, loggedIn>>

Init ==
    /\ on = FALSE /\ file = FALSE /\ loaded = FALSE
    /\ daemon = "down" /\ lastExit = "none" /\ loggedIn = TRUE

\* Turn on: write the file and load it. The menu runs one command at a
\* time, and turning off waits for the daemon to exit (launchctl bootout and
\* systemctl --user disable --now both do), so the previous daemon is down
\* (GuardOffWaits).
TurnOn ==
    /\ loggedIn /\ ~on
    /\ (daemon = "down" \/ ~GuardOffWaits)
    /\ on' = TRUE /\ file' = TRUE /\ loaded' = TRUE /\ lastExit' = "none"
    /\ UNCHANGED <<daemon, loggedIn>>

\* Turn off: unload, which asks a running daemon to stop, and remove the
\* file.
TurnOff ==
    /\ loggedIn /\ on
    /\ on' = FALSE
    /\ file' = (file /\ ~GuardRemoveOnTurnOff)
    /\ IF GuardUnloadOnTurnOff
          THEN /\ loaded' = FALSE
               /\ daemon' = IF daemon = "running" THEN "stopping" ELSE daemon
          ELSE UNCHANGED <<loaded, daemon>>
    /\ UNCHANGED <<lastExit, loggedIn>>

\* The service manager starts a loaded service when it loads (RunAtLoad,
\* WantedBy) and again after a failure (KeepAlive's SuccessfulExit = false,
\* Restart=on-failure).
Start ==
    /\ loggedIn /\ loaded /\ daemon = "down"
    /\ \/ lastExit = "none"
       \/ lastExit = "failed" /\ GuardRestartOnFailure
       \/ lastExit = "stopped" /\ ~GuardNoRestartOnStop
    /\ daemon' = "running" /\ lastExit' = "none"
    /\ UNCHANGED <<on, file, loaded, loggedIn>>

\* The daemon fails: it crashes, or its camera or model fails.
Fail ==
    /\ daemon = "running"
    /\ daemon' = "down" /\ lastExit' = "failed"
    /\ UNCHANGED <<on, file, loaded, loggedIn>>

\* A stopping daemon exits cleanly (DaemonAdmission's StopEnds).
Exit ==
    /\ daemon = "stopping"
    /\ daemon' = "down" /\ lastExit' = "stopped"
    /\ UNCHANGED <<on, file, loaded, loggedIn>>

\* The person logs out, which stops their services, or logs in, which loads
\* each installed file afresh.
LogOut ==
    /\ loggedIn
    /\ loggedIn' = FALSE /\ loaded' = FALSE
    /\ daemon' = IF daemon = "running" THEN "stopping" ELSE daemon
    /\ UNCHANGED <<on, file, lastExit>>

LogIn ==
    /\ ~loggedIn /\ daemon = "down"
    /\ loggedIn' = TRUE /\ loaded' = file /\ lastExit' = "none"
    /\ UNCHANGED <<on, file, daemon>>

Next == TurnOn \/ TurnOff \/ Start \/ Fail \/ Exit \/ LogOut \/ LogIn

\* The service manager and a stopping daemon are fair; the person and the
\* failures are not.
Spec == Init /\ [][Next]_vars /\ WF_vars(Start) /\ WF_vars(Exit)

TypeOK ==
    /\ on \in BOOLEAN /\ file \in BOOLEAN /\ loaded \in BOOLEAN
    /\ daemon \in {"down", "running", "stopping"}
    /\ lastExit \in {"none", "failed", "stopped"}
    /\ loggedIn \in BOOLEAN

\* The daemon, which holds the person's consent, runs only while they have
\* it turned on.
RunsOnlyWhenOn == daemon = "running" => on

\* The service's file is installed exactly while it is turned on.
FileMatchesMenu == file <=> on

\* Liveness: turning off stops the daemon.
TurnOffStops == ~on ~> daemon = "down"

\* Liveness, and something must happen: turned on, in a session, the daemon
\* runs, and runs again after it fails, unless it is turned off or the
\* session ends.
OnRuns == (on /\ loggedIn) ~> (daemon = "running" \/ ~on \/ ~loggedIn)
==============================================================================
