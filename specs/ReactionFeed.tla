---- MODULE ReactionFeed -----------------------------------------------------
(***************************************************************************)
(* How reactions reach each client (#36). facepalm keeps one timeline of   *)
(* readings, the interpreted reactions with their summary lines, and drops *)
(* the oldest once it holds Capacity. Each client, an agent session or AI   *)
(* Pair, has a cursor: a read, from a hook after an agent action or from   *)
(* the MCP's check-in, returns the readings since that client last looked. *)
(* A client can instead wait for the next reading, which a new reading or  *)
(* a timeout ends. The person's feedback is on a feeling, not a reading, *)
(* and specs/FeelingFeedback.tla models it.                                *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Clients,             \* The clients that may connect.
    MaxReadings,         \* How many readings the model makes.
    Capacity,            \* How many readings the timeline keeps.
    GuardAdvanceCursor,  \* A read moves the client's cursor past what it got.
    GuardReportMissed,   \* A read counts the readings dropped before it.
    GuardWakeWaiters     \* A new reading ends every wait with it.

VARIABLES
    next,       \* The id the next reading gets; readings are 1..next-1.
    oldest,     \* The oldest id still kept; older ones were dropped.
    connected,  \* The clients connected.
    since,      \* since[c]: the first id c could have been told about.
    cursor,     \* cursor[c]: the first id c has not yet been told about.
    shown,      \* shown[c]: the ids c was shown.
    missed,     \* missed[c]: the ids c was told it missed.
    waiting,    \* The clients waiting for a reading.
    repeated    \* Some reading was shown to the same client twice.

vars == <<next, oldest, connected, since, cursor, shown, missed, waiting,
          repeated>>

Ids == 1..MaxReadings

\* What client c is told on a read: the kept ids since its cursor are shown,
\* and the dropped ones counted as missed.
NewIds(c) == {i \in cursor[c]..(next - 1) : i >= oldest}
DroppedIds(c) == {i \in cursor[c]..(next - 1) : i < oldest}

\* Client c reads: its new readings, and the count of those it missed.
Tell(c) ==
    /\ repeated' = (repeated \/ NewIds(c) \cap shown[c] # {})
    /\ shown' = [shown EXCEPT ![c] = @ \cup NewIds(c)]
    /\ missed' = [missed EXCEPT ![c] =
                     IF GuardReportMissed THEN @ \cup DroppedIds(c) ELSE @]
    /\ cursor' = [cursor EXCEPT ![c] =
                     IF GuardAdvanceCursor THEN next ELSE @]

Init ==
    /\ next = 1 /\ oldest = 1 /\ connected = {}
    /\ since = [c \in Clients |-> 1] /\ cursor = [c \in Clients |-> 1]
    /\ shown = [c \in Clients |-> {}] /\ missed = [c \in Clients |-> {}]
    /\ waiting = {} /\ repeated = FALSE

\* A client starts with what happens after it connects: no backlog. What it
\* was shown before stays shown.
Connect(c) ==
    /\ c \notin connected
    /\ connected' = connected \cup {c}
    /\ since' = [since EXCEPT ![c] = next]
    /\ cursor' = [cursor EXCEPT ![c] = next]
    /\ UNCHANGED <<next, oldest, shown, missed, waiting, repeated>>

Disconnect(c) ==
    /\ c \in connected
    /\ connected' = connected \ {c}
    /\ waiting' = waiting \ {c}
    /\ UNCHANGED <<next, oldest, since, cursor, shown, missed, repeated>>

\* The model interprets a reaction. Past Capacity, the oldest is dropped.
\* Every waiting client is told at once.
React ==
    /\ next <= MaxReadings
    /\ next' = next + 1
    /\ oldest' = IF next' - oldest > Capacity THEN oldest + 1 ELSE oldest
    /\ IF GuardWakeWaiters
          THEN /\ waiting' = {}
               /\ repeated' = (repeated \/
                     \E c \in waiting : next \in shown[c])
               /\ shown' = [c \in Clients |->
                     IF c \in waiting THEN shown[c] \cup {next}
                     ELSE shown[c]]
               /\ cursor' = [c \in Clients |->
                     IF c \in waiting /\ GuardAdvanceCursor THEN next + 1
                     ELSE cursor[c]]
          ELSE UNCHANGED <<waiting, repeated, shown, cursor>>
    /\ UNCHANGED <<connected, since, missed>>

\* A hook after an agent action, or the MCP's check-in.
Read(c) ==
    /\ c \in connected /\ c \notin waiting
    /\ Tell(c)
    /\ UNCHANGED <<next, oldest, connected, since, waiting>>

\* The MCP's wait_for_reaction. With a reading already there, it returns at
\* once, as a read.
Wait(c) ==
    /\ c \in connected /\ c \notin waiting
    /\ IF NewIds(c) \cup DroppedIds(c) # {}
          THEN /\ Tell(c) /\ UNCHANGED waiting
          ELSE /\ waiting' = waiting \cup {c}
               /\ UNCHANGED <<shown, missed, cursor, repeated>>
    /\ UNCHANGED <<next, oldest, connected, since>>

WaitTimesOut(c) ==
    /\ c \in waiting
    /\ waiting' = waiting \ {c}
    /\ UNCHANGED <<next, oldest, connected, since, cursor, shown, missed,
                   repeated>>

Next ==
    \/ React
    \/ \E c \in Clients :
          \/ Connect(c) \/ Disconnect(c) \/ Read(c) \/ Wait(c)
          \/ WaitTimesOut(c)

Spec ==
    /\ Init /\ [][Next]_vars
    /\ \A c \in Clients : WF_vars(WaitTimesOut(c))

TypeOK ==
    /\ next \in 1..(MaxReadings + 1) /\ oldest \in 1..next
    /\ connected \subseteq Clients /\ waiting \subseteq connected
    /\ since \in [Clients -> 1..(MaxReadings + 1)]
    /\ cursor \in [Clients -> 1..(MaxReadings + 1)]
    /\ shown \in [Clients -> SUBSET Ids]
    /\ missed \in [Clients -> SUBSET Ids]
    /\ repeated \in BOOLEAN

\* No reading is shown to the same client twice.
NoRepeat == ~repeated

\* Every reading made while a client was connected, up to its cursor, was
\* shown to it or counted as missed: none is lost without a word.
NoSilentGap ==
    \A c \in connected :
        \A i \in since[c]..(cursor[c] - 1) : i \in shown[c] \cup missed[c]

\* A waiting client is never left waiting while a reading it has not seen
\* exists.
NoWaitPastAReading ==
    \A c \in waiting : NewIds(c) \cup DroppedIds(c) = {}

\* Something must happen: a connected client with a reading it has not seen
\* can be shown it.
CanShow ==
    \A c \in connected :
        (c \notin waiting /\ NewIds(c) # {}) =>
            ENABLED (Read(c) /\ shown'[c] # shown[c])

\* Liveness: every wait ends.
WaitEnds == \A c \in Clients : c \in waiting ~> c \notin waiting
==============================================================================
