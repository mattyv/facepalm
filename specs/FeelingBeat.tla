---- MODULE FeelingBeat -------------------------------------------------------
(***************************************************************************)
(* How a reaction is read as a feeling (#70, item 3 of #61). A client that *)
(* has just asked the person something marks a beat: a short window after *)
(* the question or proposal. facepalm watches the readings made in that    *)
(* window and, when it ends, answers with one interpretation: a feeling,   *)
(* possibly unknown. The reply also tells the client every reading since   *)
(* it last looked, as a read does (specs/ReactionFeed.tla), so marking a   *)
(* beat never skips a reading.                                             *)
(*                                                                         *)
(* Each reading carries the cue it gives: a nod, a shake, a puzzled face   *)
(* (brows down or eyes narrowed, held), or nothing that bears on a        *)
(* feeling. The generic rule reads one kind of cue as its feeling, and     *)
(* anything else as unknown: no cue, cues that conflict, or a window some  *)
(* of whose readings were dropped before the beat ended.                   *)
(*                                                                         *)
(* `during` and `lost` are ghost variables: what really happened while the *)
(* beat was open, kept apart from what the feed computes from its ids, so  *)
(* the invariants can hold the computation to the truth. Each answer is    *)
(* checked against them as it is given, and a flag records a wrong one, so *)
(* the model keeps no history of answers.                                  *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Clients,              \* The clients that may connect.
    MaxReadings,          \* How many readings the model makes.
    Capacity,             \* How many readings the timeline keeps.
    MaxBeats,             \* How many beats each client marks.
    Cues,                 \* The cues a reading may give.
    GuardWindowStart,     \* The beat reads only readings made after it began.
    GuardDroppedUnknown,  \* A window with a dropped reading reads as unknown.
    GuardConflictUnknown, \* Cues of different feelings read as unknown.
    GuardCloseBeat,       \* A beat ends once, closing its window.
    GuardCancelOnLeave,   \* Disconnecting ends the client's beat unanswered.
    GuardKeepBacklog      \* Marking a beat leaves the client's cursor alone.

\* The six feelings; src/feelings.h is the code's only list of them.
\* engagement and annoyance are labels a correction can name (#35); no
\* generic rule gives them yet.
Feelings == {"agreement", "disagreement", "engagement", "annoyance",
             "confused", "unknown"}

\* The feeling one cue gives, or "none".
FeelingOf(cue) ==
    CASE cue = "nod" -> "agreement"
      [] cue = "shake" -> "disagreement"
      [] cue = "puzzled" -> "confused"
      [] OTHER -> "none"

Felt(cues) == {FeelingOf(cue) : cue \in cues} \ {"none"}

\* The generic rule, as designed: one kind of feeling among the cues, or
\* unknown.
Rule(cues) ==
    IF Cardinality(Felt(cues)) = 1 THEN CHOOSE f \in Felt(cues) : TRUE
    ELSE "unknown"

\* The rule as the feed applies it; GuardConflictUnknown off lets one of
\* several feelings win.
Applied(cues) ==
    IF GuardConflictUnknown \/ Felt(cues) = {} THEN Rule(cues)
    ELSE CHOOSE f \in Felt(cues) : TRUE

VARIABLES
    next,          \* The id the next reading gets; readings are 1..next-1.
    oldest,        \* The oldest id still kept; older ones were dropped.
    cueOf,         \* cueOf[i]: the cue reading i gives.
    connected,     \* The clients connected.
    since,         \* since[c]: the first id c could have been told about.
    cursor,        \* cursor[c]: the first id c has not yet been told about.
    shown,         \* shown[c]: the ids c was shown.
    missed,        \* missed[c]: the ids c was told it missed.
    repeated,      \* Some reading was shown to the same client twice.
    start,         \* start[c]: the first id of c's open beat; 0 when none.
    beats,         \* beats[c]: how many beats c has marked.
    during,        \* during[c]: the cues made while c's beat was open.
    lost,          \* lost[c]: one of those readings was dropped.
    wrongFeeling,  \* A clear, whole beat was read as another feeling.
    wrongUnclear,  \* An unclear, whole beat was read as a feeling.
    wrongLost,     \* A beat missing a reading was read as a feeling.
    ended,         \* ended[c]: c's beats up to this one have ended.
    left,          \* left[c]: c left during its open beat.
    twice          \* Some beat was answered twice, or after its client left.

answers == <<wrongFeeling, wrongUnclear, wrongLost, twice>>
vars == <<next, oldest, cueOf, connected, since, cursor, shown, missed,
          repeated, start, beats, during, lost, ended, left, answers>>

Ids == 1..MaxReadings

NewIds(c) == {i \in cursor[c]..(next - 1) : i >= oldest}
DroppedIds(c) == {i \in cursor[c]..(next - 1) : i < oldest}

\* What client c is told: the kept ids since its cursor are shown, the
\* dropped ones counted as missed, and the cursor moved past them all.
Tell(c) ==
    /\ repeated' = (repeated \/ NewIds(c) \cap shown[c] # {})
    /\ shown' = [shown EXCEPT ![c] = @ \cup NewIds(c)]
    /\ missed' = [missed EXCEPT ![c] = @ \cup DroppedIds(c)]
    /\ cursor' = [cursor EXCEPT ![c] = next]

Init ==
    /\ next = 1 /\ oldest = 1 /\ cueOf = [i \in Ids |-> "other"]
    /\ connected = {}
    /\ since = [c \in Clients |-> 1] /\ cursor = [c \in Clients |-> 1]
    /\ shown = [c \in Clients |-> {}] /\ missed = [c \in Clients |-> {}]
    /\ repeated = FALSE
    /\ start = [c \in Clients |-> 0] /\ beats = [c \in Clients |-> 0]
    /\ during = [c \in Clients |-> {}] /\ lost = [c \in Clients |-> FALSE]
    /\ wrongFeeling = FALSE /\ wrongUnclear = FALSE /\ wrongLost = FALSE
    /\ ended = [c \in Clients |-> 0] /\ left = [c \in Clients |-> FALSE]
    /\ twice = FALSE

Connect(c) ==
    /\ c \notin connected
    /\ connected' = connected \cup {c}
    /\ since' = [since EXCEPT ![c] = next]
    /\ cursor' = [cursor EXCEPT ![c] = next]
    /\ UNCHANGED <<next, oldest, cueOf, shown, missed, repeated, start,
                   beats, during, lost, ended, left, answers>>

\* Leaving during a beat ends it with no answer: the connection that would
\* carry it is gone.
Disconnect(c) ==
    /\ c \in connected
    /\ connected' = connected \ {c}
    /\ ended' = IF start[c] # 0 THEN [ended EXCEPT ![c] = beats[c]] ELSE ended
    /\ left' = IF start[c] # 0 THEN [left EXCEPT ![c] = TRUE] ELSE left
    /\ start' = IF GuardCancelOnLeave THEN [start EXCEPT ![c] = 0]
                ELSE start
    /\ UNCHANGED <<next, oldest, cueOf, since, cursor, shown, missed,
                   repeated, beats, during, lost, answers>>

\* The model interprets a reaction giving `cue`. Past Capacity, the oldest
\* is dropped. Every open beat sees the cue, and loses one if the dropped
\* reading was made during it.
React(cue) ==
    /\ next <= MaxReadings
    /\ next' = next + 1
    /\ cueOf' = [cueOf EXCEPT ![next] = cue]
    /\ oldest' = IF next' - oldest > Capacity THEN oldest + 1 ELSE oldest
    /\ during' = [c \in Clients |->
                    IF start[c] # 0 THEN during[c] \cup {cue} ELSE during[c]]
    /\ lost' = [c \in Clients |->
                  lost[c] \/ (start[c] # 0 /\ oldest' > oldest
                              /\ oldest >= start[c])]
    /\ UNCHANGED <<connected, since, cursor, shown, missed, repeated, start,
                   beats, ended, left, answers>>

Read(c) ==
    /\ c \in connected /\ start[c] = 0
    /\ Tell(c)
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, start, beats,
                   during, lost, ended, left, answers>>

\* The client marks a beat: the window opens at the next reading.
Mark(c) ==
    /\ c \in connected /\ start[c] = 0 /\ beats[c] < MaxBeats
    /\ start' = [start EXCEPT ![c] = next]
    /\ beats' = [beats EXCEPT ![c] = @ + 1]
    /\ during' = [during EXCEPT ![c] = {}]
    /\ lost' = [lost EXCEPT ![c] = FALSE]
    /\ left' = [left EXCEPT ![c] = FALSE]
    /\ cursor' = IF GuardKeepBacklog THEN cursor
                 ELSE [cursor EXCEPT ![c] = next]
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, shown, missed,
                   repeated, ended, answers>>

\* The feeling the feed gives c's beat as it ends now.
Answer(c) ==
    LET window == IF GuardWindowStart THEN start[c]..(next - 1)
                  ELSE cursor[c]..(next - 1)
        complete == \A i \in window : i >= oldest
        kept == {cueOf[i] : i \in {j \in window : j >= oldest}}
    IN IF complete \/ ~GuardDroppedUnknown THEN Applied(kept) ELSE "unknown"

\* The window ends: the client is told what came since it last looked, and
\* given one interpretation of the readings made in the window, which is
\* checked against what really happened during it.
EndBeat(c) ==
    /\ start[c] # 0
    /\ LET feeling == Answer(c)
           truth == Rule(during[c])
       IN /\ wrongFeeling' = (wrongFeeling \/
                (~lost[c] /\ truth # "unknown" /\ feeling # truth))
          /\ wrongUnclear' = (wrongUnclear \/
                (~lost[c] /\ truth = "unknown" /\ feeling # "unknown"))
          /\ wrongLost' = (wrongLost \/ (lost[c] /\ feeling # "unknown"))
    /\ twice' = (twice \/ ended[c] = beats[c])
    /\ ended' = [ended EXCEPT ![c] = beats[c]]
    /\ start' = IF GuardCloseBeat THEN [start EXCEPT ![c] = 0] ELSE start
    /\ Tell(c)
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, beats, during,
                   lost, left>>

Next ==
    \/ \E cue \in Cues : React(cue)
    \/ \E c \in Clients :
          Connect(c) \/ Disconnect(c) \/ Read(c) \/ Mark(c) \/ EndBeat(c)

Spec ==
    /\ Init /\ [][Next]_vars
    /\ \A c \in Clients : WF_vars(EndBeat(c))

TypeOK ==
    /\ next \in 1..(MaxReadings + 1) /\ oldest \in 1..next
    /\ cueOf \in [Ids -> Cues]
    /\ connected \subseteq Clients
    /\ since \in [Clients -> 1..(MaxReadings + 1)]
    /\ cursor \in [Clients -> 1..(MaxReadings + 1)]
    /\ shown \in [Clients -> SUBSET Ids]
    /\ missed \in [Clients -> SUBSET Ids]
    /\ repeated \in BOOLEAN
    /\ start \in [Clients -> 0..(MaxReadings + 1)]
    /\ beats \in [Clients -> 0..MaxBeats]
    /\ during \in [Clients -> SUBSET Cues]
    /\ lost \in [Clients -> BOOLEAN]
    /\ {wrongFeeling, wrongUnclear, wrongLost, twice} \subseteq BOOLEAN
    /\ ended \in [Clients -> 0..MaxBeats]
    /\ left \in [Clients -> BOOLEAN]
    /\ \A c \in Clients : start[c] # 0 => Answer(c) \in Feelings

\* Each beat gets at most one answer, and none once its client has left.
OneAnswerPerBeat == ~twice

\* With every reading of the window kept, cues of one feeling give that
\* feeling: never one from a reading made before the beat began.
ReadsOnlyTheBeat == ~wrongFeeling

\* No cue of a feeling, or cues of different feelings, read as unknown.
UnknownWhenUnclear == ~wrongUnclear

\* A window whose readings were not all kept reads as unknown: the missing
\* one might have been a shake.
UnknownWhenIncomplete == ~wrongLost

\* Marking a beat skips nothing: every reading made while a client was
\* connected, up to its cursor, was shown to it or counted as missed.
NoSilentGap ==
    \A c \in connected :
        \A i \in since[c]..(cursor[c] - 1) : i \in shown[c] \cup missed[c]

\* No reading is shown to the same client twice.
NoRepeat == ~repeated

\* Something must happen: a beat in which the person only nodded can end
\* read as agreement.
CanAgree ==
    \A c \in Clients :
        (start[c] # 0 /\ ~lost[c] /\ during[c] = {"nod"}) =>
            (ENABLED EndBeat(c)) /\ Answer(c) = "agreement"

\* Liveness: every beat ends, answered or abandoned.
BeatEnds ==
    \A c \in Clients, n \in 1..MaxBeats :
        (start[c] # 0 /\ beats[c] = n) ~> ended[c] >= n
==============================================================================
