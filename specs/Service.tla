---- MODULE Service -----------------------------------------------------------
(***************************************************************************)
(* facepalm's background service and its local connections (#37). The     *)
(* service accepts connections on a socket only its user can open. Each    *)
(* connection has its own thread: it names its client, then reads, waits   *)
(* for a reading, or rates, through ReactionFeed (specs/ReactionFeed.tla). *)
(* The peer can hang up at any time. A thread blocked in a wait is not     *)
(* reading its socket, so it notices a hang-up only once the wait ends.    *)
(* Stopping the service closes every connection and must not hang on a     *)
(* waiting one.                                                            *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Conns,                 \* The connections that may be made.
    Names,                 \* The client names a connection may give.
    None,                  \* No name yet.
    GuardUniqueNames,      \* A name already connected is refused.
    GuardLeaveOnClose,     \* A closed connection's client leaves the feed.
    GuardStopWakes,        \* Stopping ends every wait at once.
    GuardRefuseWhenStopping \* No connection is accepted once stopping.

VARIABLES
    server,  \* "running", "stopping" or "stopped".
    conn,    \* conn[k]: "free", "open", "named", "waiting" or "closing".
    name,    \* name[k]: the client name connection k gave, or None.
    feed     \* The client names connected to the feed.

vars == <<server, conn, name, feed>>

Live == {"open", "named", "waiting", "closing"}

Init ==
    /\ server = "running" /\ conn = [k \in Conns |-> "free"]
    /\ name = [k \in Conns |-> None] /\ feed = {}

Accept(k) ==
    /\ conn[k] = "free"
    /\ (server = "running" \/ (~GuardRefuseWhenStopping /\ server = "stopping"))
    /\ conn' = [conn EXCEPT ![k] = "open"]
    /\ UNCHANGED <<server, name, feed>>

\* The first line a client sends names it, and connects it to the feed.
Hello(k, n) ==
    /\ conn[k] = "open"
    /\ IF n \in feed /\ GuardUniqueNames
          THEN /\ conn' = [conn EXCEPT ![k] = "closing"]
               /\ UNCHANGED <<name, feed>>
          ELSE /\ conn' = [conn EXCEPT ![k] = "named"]
               /\ name' = [name EXCEPT ![k] = n]
               /\ feed' = feed \cup {n}
    /\ UNCHANGED server

\* The thread blocks in ReactionFeed's Wait.
Wait(k) ==
    /\ conn[k] = "named" /\ server = "running"
    /\ conn' = [conn EXCEPT ![k] = "waiting"]
    /\ UNCHANGED <<server, name, feed>>

\* A reading, or the wait's timeout, ends it.
Answered(k) ==
    /\ conn[k] = "waiting"
    /\ conn' = [conn EXCEPT ![k] = "named"]
    /\ UNCHANGED <<server, name, feed>>

\* The peer hangs up. A waiting thread is not reading, so it is unaffected
\* until its wait ends.
HangUp(k) ==
    /\ conn[k] \in {"open", "named"}
    /\ conn' = [conn EXCEPT ![k] = "closing"]
    /\ UNCHANGED <<server, name, feed>>

\* The connection's thread ends: its client leaves the feed, and its slot is
\* free again.
Closed(k) ==
    /\ conn[k] = "closing"
    /\ conn' = [conn EXCEPT ![k] = "free"]
    /\ name' = [name EXCEPT ![k] = None]
    /\ feed' = IF GuardLeaveOnClose /\ name[k] # None
                  THEN feed \ {name[k]} ELSE feed
    /\ UNCHANGED server

\* Stopping closes every connection: it shuts each socket and disconnects
\* each client from the feed, which ends a wait.
Stop ==
    /\ server = "running" /\ server' = "stopping"
    /\ conn' = [k \in Conns |->
                  IF conn[k] = "free" THEN "free"
                  ELSE IF conn[k] = "waiting" /\ ~GuardStopWakes
                     THEN "waiting"
                  ELSE "closing"]
    /\ UNCHANGED <<name, feed>>

Stopped ==
    /\ server = "stopping" /\ \A k \in Conns : conn[k] = "free"
    /\ server' = "stopped"
    /\ UNCHANGED <<conn, name, feed>>

Next ==
    \/ Stop \/ Stopped
    \/ \E k \in Conns :
          \/ Accept(k) \/ Wait(k) \/ Answered(k) \/ HangUp(k) \/ Closed(k)
          \/ \E n \in Names : Hello(k, n)

\* Only the shutdown's own steps and the threads' closing are fair. Answered
\* is not: a wait may last its whole timeout, so stopping cannot rely on it.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Stopped)
    /\ \A k \in Conns : WF_vars(Closed(k))

TypeOK ==
    /\ server \in {"running", "stopping", "stopped"}
    /\ conn \in [Conns -> {"free"} \cup Live]
    /\ name \in [Conns -> Names \cup {None}]
    /\ feed \subseteq Names

\* No two connections speak for the same client, so they cannot share one
\* cursor or take each other's readings.
UniqueNames ==
    \A j, k \in Conns :
        (j # k /\ name[j] # None /\ name[j] = name[k]) => FALSE

\* The feed's clients are exactly the named connections still open: no
\* client lingers once its connection is gone.
FeedMatchesConns == feed = {name[k] : k \in {j \in Conns : name[j] # None}}

\* Once stopping, no thread is left blocked in a wait.
StopWakesWaiters ==
    server # "running" => \A k \in Conns : conn[k] # "waiting"

\* Nothing is accepted once stopped.
NothingAfterStop == server = "stopped" => \A k \in Conns : conn[k] = "free"

\* Something must happen: a running service can accept a connection into a
\* free slot.
CanAccept ==
    \A k \in Conns :
        (server = "running" /\ conn[k] = "free") => ENABLED Accept(k)

\* Liveness: stopping always finishes.
StopEnds == server = "stopping" ~> server = "stopped"
==============================================================================
