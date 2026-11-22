---- MODULE Service -----------------------------------------------------------
(***************************************************************************)
(* facepalm's background service and its local connections (#37). The     *)
(* service accepts connections on a socket only its user can open. Each    *)
(* connection has its own thread: it names its client and shows a token,  *)
(* then reads, waits for a reading, or gives a feeling, through            *)
(* ReactionFeed (specs/ReactionFeed.tla). Only a token the person approved *)
(* is let in, and several connections may show the same one: each names   *)
(* itself, so two sessions of one approved app are two clients (#37).      *)
(* The peer can hang up at any time. A thread blocked in a wait or a beat  *)
(* is not reading its socket, so a watcher notices the hang-up and         *)
(* disconnects the client, which ends the wait at once: a client gone does *)
(* not hold its session, and the camera, until the wait times out.         *)
(* Stopping the service closes every connection and must not hang on a     *)
(* waiting one.                                                            *)
(*                                                                         *)
(* calibOwner is the one connection, if any, Service::CalibrateReply has   *)
(* seen most recently start the daemon's single calibration session       *)
(* (src/calibration.h, specs/Calibration.tla), tying it to that connection *)
(* (#128): a session any connected client may otherwise go on driving     *)
(* (CalibrateEnds does not need to come from calibOwner itself) is still   *)
(* someone's responsibility to clean up if nobody ever does, which         *)
(* Closed(k) does once that connection itself is the one going away.       *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Conns,                 \* The connections that may be made.
    Names,                 \* The client names a connection may give.
    None,                  \* No name yet, and no calibration owner yet.
    Tokens,                \* The tokens a connection may show.
    ApprovedTokens,        \* The tokens the person approved.
    GuardApproval,         \* A token not approved is refused.
    GuardUniqueNames,      \* A name already connected is refused.
    GuardLeaveOnClose,     \* A closed connection's client leaves the feed.
    GuardStopWakes,        \* Stopping ends every wait at once.
    GuardRefuseWhenStopping, \* No connection is accepted once stopping.
    GuardHangUpWakes,      \* A hang-up during a wait ends the wait.
    GuardCalibCancelOnClose \* A closed connection's own session is cancelled.

VARIABLES
    server,     \* "running", "stopping" or "stopped".
    conn,       \* conn[k]: "free", "open", "named", "waiting" or "closing".
    name,       \* name[k]: the client name connection k gave, or None.
    token,      \* token[k]: the token connection k showed, or None.
    hung,       \* hung[k]: whether connection k's peer has hung up.
    feed,       \* The client names connected to the feed.
    calibOwner  \* The connection that owns the one live calibration
                \* session, or None (#128).

vars == <<server, conn, name, token, hung, feed, calibOwner>>

Live == {"open", "named", "waiting", "closing"}

Init ==
    /\ server = "running" /\ conn = [k \in Conns |-> "free"]
    /\ name = [k \in Conns |-> None] /\ token = [k \in Conns |-> None]
    /\ hung = [k \in Conns |-> FALSE]
    /\ feed = {} /\ calibOwner = None

Accept(k) ==
    /\ conn[k] = "free"
    /\ (server = "running" \/ (~GuardRefuseWhenStopping /\ server = "stopping"))
    /\ conn' = [conn EXCEPT ![k] = "open"]
    /\ UNCHANGED <<server, name, token, hung, feed, calibOwner>>

\* Whether a hello naming n with token t is refused.
Refused(n, t) ==
    \/ (t \notin ApprovedTokens /\ GuardApproval)
    \/ (n \in feed /\ GuardUniqueNames)

\* The first line a client sends names it and shows its token, and, unless
\* refused, connects it to the feed.
Hello(k, n, t) ==
    /\ conn[k] = "open"
    /\ IF Refused(n, t)
          THEN /\ conn' = [conn EXCEPT ![k] = "closing"]
               /\ UNCHANGED <<name, token, hung, feed>>
          ELSE /\ conn' = [conn EXCEPT ![k] = "named"]
               /\ name' = [name EXCEPT ![k] = n]
               /\ token' = [token EXCEPT ![k] = t]
               /\ feed' = feed \cup {n}
    /\ UNCHANGED <<server, hung, calibOwner>>

\* The thread blocks in ReactionFeed's Wait.
Wait(k) ==
    /\ conn[k] = "named" /\ server = "running"
    /\ conn' = [conn EXCEPT ![k] = "waiting"]
    /\ UNCHANGED <<server, name, token, hung, feed, calibOwner>>

\* A reading, or the wait's timeout, ends it; the thread then reads its
\* socket, and finds a hang-up there.
Answered(k) ==
    /\ conn[k] = "waiting"
    /\ conn' = [conn EXCEPT ![k] = IF hung[k] THEN "closing" ELSE "named"]
    /\ UNCHANGED <<server, name, token, hung, feed, calibOwner>>

\* The peer hangs up. A thread reading its socket ends; a waiting one is
\* not reading, so only the watcher can notice.
HangUp(k) ==
    /\ conn[k] \in {"open", "named", "waiting"} /\ ~hung[k]
    /\ hung' = [hung EXCEPT ![k] = TRUE]
    /\ conn' = [conn EXCEPT ![k] = IF @ = "waiting" THEN @ ELSE "closing"]
    /\ UNCHANGED <<server, name, token, feed, calibOwner>>

\* The watcher sees the hang-up during the wait and disconnects the client,
\* which ends the wait (GuardHangUpWakes).
Noticed(k) ==
    /\ GuardHangUpWakes /\ conn[k] = "waiting" /\ hung[k]
    /\ conn' = [conn EXCEPT ![k] = "closing"]
    /\ UNCHANGED <<server, name, token, hung, feed, calibOwner>>

\* A named connection asks the daemon to start the one calibration session
\* (specs/Calibration.tla's Start): refused, like the real
\* CalibrationController, while another is already running, which is
\* calibOwner's own job to remember, not this model's job to re-derive
\* from specs/Calibration.tla.
CalibrateStart(k) ==
    /\ conn[k] = "named" /\ calibOwner = None
    /\ calibOwner' = k
    /\ UNCHANGED <<server, conn, name, token, hung, feed>>

\* A named connection -- any one, not only calibOwner, since "a calibrate
\* request needs no client identity" (src/service.cc's own comment) --
\* finishes or cancels the one session running.
CalibrateEnds(k) ==
    /\ conn[k] = "named" /\ calibOwner # None
    /\ calibOwner' = None
    /\ UNCHANGED <<server, conn, name, token, hung, feed>>

\* The connection's thread ends: its client leaves the feed, and its slot is
\* free again. Its own session, if it still owns one nobody else has
\* finished or cancelled, is cancelled too (GuardCalibCancelOnClose),
\* since closing this connection is the daemon's only remaining chance to
\* learn the session has nobody left to answer for it (#128); with the
\* guard disabled calibOwner is left naming a slot about to go "free",
\* which NoDanglingCalibOwner below catches.
Closed(k) ==
    /\ conn[k] = "closing"
    /\ conn' = [conn EXCEPT ![k] = "free"]
    /\ name' = [name EXCEPT ![k] = None]
    /\ token' = [token EXCEPT ![k] = None]
    /\ hung' = [hung EXCEPT ![k] = FALSE]
    /\ feed' = IF GuardLeaveOnClose /\ name[k] # None
                  THEN feed \ {name[k]} ELSE feed
    /\ calibOwner' = IF calibOwner = k /\ GuardCalibCancelOnClose
                        THEN None ELSE calibOwner
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
    /\ UNCHANGED <<name, token, hung, feed, calibOwner>>

Stopped ==
    /\ server = "stopping" /\ \A k \in Conns : conn[k] = "free"
    /\ server' = "stopped"
    /\ UNCHANGED <<conn, name, token, hung, feed, calibOwner>>

Next ==
    \/ Stop \/ Stopped
    \/ \E k \in Conns :
          \/ Accept(k) \/ Wait(k) \/ Answered(k) \/ HangUp(k) \/ Noticed(k)
          \/ Closed(k) \/ CalibrateStart(k) \/ CalibrateEnds(k)
          \/ \E n \in Names, t \in Tokens : Hello(k, n, t)

\* Only the shutdown's own steps, the watchers and the threads' closing are
\* fair. Answered is not: a wait may last its whole timeout, so neither
\* stopping nor a hang-up can rely on it.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Stopped)
    /\ \A k \in Conns : WF_vars(Closed(k)) /\ WF_vars(Noticed(k))

TypeOK ==
    /\ server \in {"running", "stopping", "stopped"}
    /\ conn \in [Conns -> {"free"} \cup Live]
    /\ name \in [Conns -> Names \cup {None}]
    /\ token \in [Conns -> Tokens \cup {None}]
    /\ hung \in [Conns -> BOOLEAN]
    /\ feed \subseteq Names
    /\ calibOwner \in Conns \cup {None}

\* No two connections speak for the same client, so they cannot share one
\* cursor or take each other's readings.
UniqueNames ==
    \A j, k \in Conns :
        (j # k /\ name[j] # None /\ name[j] = name[k]) => FALSE

\* The feed's clients are exactly the named connections still open: no
\* client lingers once its connection is gone.
FeedMatchesConns == feed = {name[k] : k \in {j \in Conns : name[j] # None}}

\* Every named connection showed an approved token.
ApprovedOnly ==
    \A k \in Conns : name[k] # None => token[k] \in ApprovedTokens

\* Something must happen: an approved token with a new name is let in, even
\* while another connection shows the same token.
ApprovedAdmitted ==
    \A k \in Conns, n \in Names, t \in ApprovedTokens :
        (conn[k] = "open" /\ n \notin feed) => ~Refused(n, t)

\* Once stopping, no thread is left blocked in a wait.
StopWakesWaiters ==
    server # "running" => \A k \in Conns : conn[k] # "waiting"

\* Nothing is accepted once stopped.
NothingAfterStop == server = "stopped" => \A k \in Conns : conn[k] = "free"

\* The calibration session's owner, when it has one, is never a slot that
\* has already gone "free": whoever started it is still, at least,
\* mid-close, so its own disconnect is still the one that will be noticed
\* (#128). A session no connection anywhere owns any more (calibOwner =
\* None) is not this invariant's concern: specs/Calibration.tla already
\* covers the session's own correctness once nothing ties it to a
\* connection either way.
NoDanglingCalibOwner ==
    \A k \in Conns : calibOwner = k => conn[k] \in Live

\* Something must happen: a running service can accept a connection into a
\* free slot.
CanAccept ==
    \A k \in Conns :
        (server = "running" /\ conn[k] = "free") => ENABLED Accept(k)

\* Liveness: stopping always finishes.
StopEnds == server = "stopping" ~> server = "stopped"
\* Liveness: a connection whose peer hung up is closed, and its client
\* gone from the feed, even in the middle of a wait.
HangUpEnds == \A k \in Conns : hung[k] ~> conn[k] = "free"
==============================================================================
