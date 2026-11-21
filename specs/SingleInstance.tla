---- MODULE SingleInstance -----------------------------------------------------
(***************************************************************************)
(* The Linux tray's single-instance guard (#98): launching facepalm-menu   *)
(* while one already runs must not open a second tray icon. tools/menu/   *)
(* main_linux.cc claims an exclusive lock (a QLockFile under the user's    *)
(* state directory, src/user_paths.h) before it creates a QApplication. A  *)
(* launch that finds the lock already held talks to the running instance  *)
(* over a QLocalServer instead: if it was asked to open Settings           *)
(* (--settings), it sends that request and waits for it to be shown;       *)
(* otherwise it exits without starting a redundant tray.                   *)
(*                                                                         *)
(* QLockFile::tryLock() is one atomic call: it sees the lock free, or left *)
(* behind by a process that has since died, and claims it in the same      *)
(* step, so no other launch can observe "free" in between (GuardAtomic    *)
(* Claim). It also tells a live owner from a dead one by checking the      *)
(* owning PID, so a crash's lock is later reclaimed rather than wedging    *)
(* every launch after it (GuardStaleReclaimed). The running instance only  *)
(* forwards a Settings request when one was asked for (GuardForward       *)
(* OnSettings); a launch with no flag never talks to it at all.            *)
(*                                                                         *)
(* Each launcher models one run of facepalm-menu, from idle through its    *)
(* decision to either become the tray (`"primary"`) or hand off to one     *)
(* (`"forwarding"`, then `"exited"`). A launcher that loses its running    *)
(* instance mid-handoff (the primary quit or crashed first) retries, with  *)
(* the same --settings choice it started with, rather than exiting        *)
(* empty-handed.                                                          *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Launchers,             \* The launches of facepalm-menu to model.
    GuardAtomicClaim,      \* tryLock() sees "free" and claims it as one step.
    GuardStaleReclaimed,   \* tryLock() reclaims a dead owner's lock.
    GuardForwardOnSettings \* A launch that wants Settings asks the owner.

VARIABLES
    lock,          \* "free", or the launcher currently holding the lock.
    alive,         \* alive[l]: whether l's process (once primary) still runs.
    state,         \* state[l]: "idle", "trying", "claiming" (the race
                   \* window), "forwarding", "primary" or "exited".
    shown,         \* shown[l]: Settings was shown because l asked for it.
    wantsSettings  \* wantsSettings[l]: l was run with --settings, chosen
                   \* when it launches and kept through any retry.

vars == <<lock, alive, state, shown, wantsSettings>>

States == {"idle", "trying", "claiming", "forwarding", "primary", "exited"}

Init ==
    /\ lock = "free"
    /\ alive = [l \in Launchers |-> FALSE]
    /\ state = [l \in Launchers |-> "idle"]
    /\ shown = [l \in Launchers |-> FALSE]
    /\ wantsSettings = [l \in Launchers |-> FALSE]

\* Whether the lock is held by a live owner. Written with IF so that
\* `alive[lock]` is never applied to "free", which is not in its domain:
\* TLA+'s \/ and /\ are not guaranteed to short-circuit the way a
\* language's && and || do, so a bare `lock # "free" /\ alive[lock]` can
\* still evaluate `alive[lock]` when `lock = "free"` and fail.
Held ==
    IF lock \in Launchers THEN alive[lock] ELSE FALSE

\* Whether `l` may claim the lock: it is free, or its owner has died and
\* reclaiming a dead owner's lock is allowed.
Claimable(l) ==
    \/ lock = "free"
    \/ GuardStaleReclaimed /\ ~Held

\* The person runs facepalm-menu, with or without --settings.
Launch(l) ==
    /\ state[l] = "idle"
    /\ state' = [state EXCEPT ![l] = "trying"]
    /\ \E flag \in BOOLEAN : wantsSettings' = [wantsSettings EXCEPT ![l] = flag]
    /\ UNCHANGED <<lock, alive, shown>>

\* SingleInstance.tla: TryLockAtomic. QLockFile::tryLock() claims an
\* unowned or stale lock in the one call that also checked it, becoming the
\* tray.
TryLockAtomic(l) ==
    /\ GuardAtomicClaim
    /\ state[l] = "trying" /\ Claimable(l)
    /\ lock' = l /\ alive' = [alive EXCEPT ![l] = TRUE]
    /\ state' = [state EXCEPT ![l] = "primary"]
    /\ shown' = [shown EXCEPT ![l] = IF wantsSettings[l] THEN TRUE ELSE @]
    /\ UNCHANGED wantsSettings

\* The mutation for GuardAtomicClaim = FALSE: a non-atomic probe splits the
\* same decision across two steps, so another launch's ObserveFree can land
\* in the window between this one seeing the lock free and claiming it --
\* the TOCTOU race a bare connect-then-listen would have, which is why the
\* design uses QLockFile's one atomic call instead.
ObserveFree(l) ==
    /\ ~GuardAtomicClaim
    /\ state[l] = "trying" /\ Claimable(l)
    /\ state' = [state EXCEPT ![l] = "claiming"]
    /\ UNCHANGED <<lock, alive, shown, wantsSettings>>

\* Claims without looking again: exactly the bug. Another launch's own
\* ClaimAfterObserve may already have taken the lock since this one looked.
ClaimAfterObserve(l) ==
    /\ ~GuardAtomicClaim
    /\ state[l] = "claiming"
    /\ lock' = l /\ alive' = [alive EXCEPT ![l] = TRUE]
    /\ state' = [state EXCEPT ![l] = "primary"]
    /\ shown' = [shown EXCEPT ![l] = IF wantsSettings[l] THEN TRUE ELSE @]
    /\ UNCHANGED wantsSettings

\* SingleInstance.tla: TryLockHeld. The lock is held by a live instance: l
\* asks it for Settings only if it was asked for one itself, otherwise it
\* just exits rather than starting a redundant tray.
TryLockHeld(l) ==
    /\ state[l] = "trying"
    /\ Held
    /\ state' = [state EXCEPT ![l] =
                     IF wantsSettings[l] /\ GuardForwardOnSettings
                        THEN "forwarding" ELSE "exited"]
    /\ UNCHANGED <<lock, alive, shown, wantsSettings>>

\* SingleInstance.tla: Forward. The owner is still listening: it raises
\* its Settings window on l's behalf.
Forward(l) ==
    /\ state[l] = "forwarding"
    /\ Held
    /\ state' = [state EXCEPT ![l] = "exited"]
    /\ shown' = [shown EXCEPT ![l] = TRUE]
    /\ UNCHANGED <<lock, alive, wantsSettings>>

\* The owner quit or crashed between TryLockHeld and Forward: the request
\* never reached anyone, so l tries again, with the same --settings choice,
\* instead of exiting as though it had been served.
ForwardLost(l) ==
    /\ state[l] = "forwarding"
    /\ ~Held
    /\ state' = [state EXCEPT ![l] = "trying"]
    /\ UNCHANGED <<lock, alive, shown, wantsSettings>>

\* SingleInstance.tla: Quit. The running instance's person chooses Quit: it
\* releases the lock cleanly.
Quit(l) ==
    /\ state[l] = "primary" /\ lock = l /\ alive[l]
    /\ lock' = "free" /\ alive' = [alive EXCEPT ![l] = FALSE]
    /\ state' = [state EXCEPT ![l] = "exited"]
    /\ UNCHANGED <<shown, wantsSettings>>

\* The running instance's process dies without releasing the lock (a crash,
\* or a kill -9): the lock file is left behind, stale, for the next launch
\* to find.
Crash(l) ==
    /\ state[l] = "primary" /\ lock = l /\ alive[l]
    /\ alive' = [alive EXCEPT ![l] = FALSE]
    /\ state' = [state EXCEPT ![l] = "exited"]
    /\ UNCHANGED <<lock, shown, wantsSettings>>

Next ==
    \E l \in Launchers :
        \/ Launch(l) \/ TryLockAtomic(l) \/ ObserveFree(l)
        \/ ClaimAfterObserve(l) \/ TryLockHeld(l) \/ Forward(l)
        \/ ForwardLost(l) \/ Quit(l) \/ Crash(l)

\* The code's own steps, once enabled, run promptly; the person launching,
\* quitting, and a crash are not fair.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ \A l \in Launchers :
           /\ WF_vars(TryLockAtomic(l)) /\ WF_vars(ObserveFree(l))
           /\ WF_vars(ClaimAfterObserve(l)) /\ WF_vars(TryLockHeld(l))
           /\ WF_vars(Forward(l)) /\ WF_vars(ForwardLost(l))

TypeOK ==
    /\ lock \in Launchers \cup {"free"}
    /\ alive \in [Launchers -> BOOLEAN]
    /\ state \in [Launchers -> States]
    /\ shown \in [Launchers -> BOOLEAN]
    /\ wantsSettings \in [Launchers -> BOOLEAN]

\* At most one launch ever holds the tray's claim at a time.
AtMostOnePrimary ==
    Cardinality({l \in Launchers : state[l] = "primary" /\ alive[l]}) <= 1

\* The lock names the live primary, and only it.
LockMatchesPrimary ==
    \A l \in Launchers : (state[l] = "primary" /\ alive[l]) => lock = l

\* Liveness: every launch settles, either as the tray or handed off to one.
LaunchSettles ==
    \A l \in Launchers : state[l] = "trying" ~> state[l] \in {"primary", "exited"}

\* Liveness, and something must happen: a launch that asked for Settings
\* eventually gets it shown, whether as the tray itself or through the one
\* already running.
RequestServed ==
    \A l \in Launchers :
        (state[l] = "trying" /\ wantsSettings[l]) ~> shown[l]
==============================================================================
