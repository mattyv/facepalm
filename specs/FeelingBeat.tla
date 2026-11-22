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
(* Where the person looked (#142, #144): a beat's reply also carries the   *)
(* majority gaze over its window, as context, never as a rule's input --   *)
(* Rule and Applied below never take gaze, so the generic feeling is       *)
(* exactly what it was before this. `gaze` is the ambient zone the person  *)
(* is looking in right now (Look changes it, standing for a camera frame's *)
(* own composed-gaze read, src/screen_context.h's Gaze); `gazeDuring[c]`   *)
(* is the ghost tracking what c's reply will report: reset to `gaze` when  *)
(* c marks a beat, and overwritten by every Look while that beat stays     *)
(* open, so it ends up holding the zone the beat *ends* with -- the        *)
(* model's own stand-in for "the majority over the window" (the real code  *)
(* tallies one; a short window's gaze rarely flips, #137's own median      *)
(* filter and dwell already damp that, so the last sample is a reasonable  *)
(* proxy for the model to check the two properties below against). One     *)
(* exception does read gaze: GuardAvertedUnknown's thinking guard. A       *)
(* question beat that ends with gaze averted is read unknown however the  *)
(* cues went, "eyes never decide a feeling by rule" (#137's own comment) - *)
(* this is that one guarded exception, not the rule changing. kind[c] is   *)
(* the kind c marked its current beat with (Mark now takes one). Kinds is  *)
(* a CONSTANT, like Cues, so FeelingBeat.cfg can hold it to one element:   *)
(* the thinking guard needs both kinds reachable to bite, and nothing else *)
(* here cares which kind a beat is, so FeelingBeat_Gaze.cfg is the one     *)
(* config that spends the extra branching both kinds would otherwise cost  *)
(* every config.                                                          *)
(*                                                                         *)
(* #144's content rectangle -- whether gaze was mostly inside one a client *)
(* gives alongside a beat -- carries no lifecycle of its own (a client     *)
(* gives it once, with the beat, and the answer is a snapshot read against *)
(* it the way CoarseGazeFrom's own screen zones already are): it is not    *)
(* modeled here, the way screen_context.h's own comment already excuses    *)
(* CoarseGazeFrom from a spec entry, and is tested directly instead        *)
(* (test/screen_context_test.cc's ContentHitFromTest).                     *)
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
    Kinds,                \* What a client may mark a beat with (#142).
    MaxLooks,              \* How many Look steps the model makes (#142).
    GuardWindowStart,     \* The beat reads only readings made after it began.
    GuardDroppedUnknown,  \* A window with a dropped reading reads as unknown.
    GuardConflictUnknown, \* Cues of different feelings read as unknown.
    GuardCloseBeat,       \* A beat ends once, closing its window.
    GuardCancelOnLeave,   \* Disconnecting ends the client's beat unanswered.
    GuardKeepBacklog,     \* Marking a beat leaves the client's cursor alone.
    GuardAvertedUnknown   \* A question beat ending averted reads as unknown.

\* The six feelings; src/feelings.h is the code's only list of them.
\* engagement and annoyance are labels a correction can name (#35); no
\* generic rule gives them yet.
Feelings == {"agreement", "disagreement", "engagement", "annoyance",
             "confused", "unknown"}

\* The four coarse gaze zones a beat's reply can report (#142,
\* src/screen_context.h's Gaze, worded for a beat's own reply):
\* on_content (this screen), averted (another screen, or simply not the
\* content), away, or unknown. Fixed, like Kinds above: every client's
\* reply names one of these four.
Zones == {"on_content", "averted", "away", "unknown"}

\* The feeling one cue gives, or "none".
FeelingOf(cue) ==
    CASE cue = "nod" -> "agreement"
      [] cue = "shake" -> "disagreement"
      [] cue = "puzzled" -> "confused"
      [] OTHER -> "none"

Felt(cues) == {FeelingOf(cue) : cue \in cues} \ {"none"}

\* The generic rule, as designed: one kind of feeling among the cues, or
\* unknown. Gaze is never one of its inputs (#142): "eyes never decide a
\* feeling by rule".
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
    kind,          \* kind[c]: what c marked its current/last beat with.
    gaze,          \* The ambient gaze zone right now (#142).
    looks,         \* How many Look steps have run so far.
    gazeDuring,    \* gazeDuring[c]: the zone c's reply will report.
    wrongFeeling,  \* A clear, whole beat was read as another feeling.
    wrongUnclear,  \* An unclear, whole beat was read as a feeling.
    wrongLost,     \* A beat missing a reading was read as a feeling.
    wrongThinking, \* A question beat ending averted did not read unknown.
    wrongContext,  \* Gaze changed a feeling outside that one exception.
    ended,         \* ended[c]: c's beats up to this one have ended.
    left,          \* left[c]: c left during its open beat.
    twice          \* Some beat was answered twice, or after its client left.

answers == <<wrongFeeling, wrongUnclear, wrongLost, wrongThinking,
            wrongContext, twice>>
vars == <<next, oldest, cueOf, connected, since, cursor, shown, missed,
          repeated, start, beats, during, lost, kind, gaze, looks,
          gazeDuring, ended, left, answers>>

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
    /\ kind = [c \in Clients |-> "question"]
    /\ gaze = "unknown" /\ looks = 0
    /\ gazeDuring = [c \in Clients |-> "unknown"]
    /\ wrongFeeling = FALSE /\ wrongUnclear = FALSE /\ wrongLost = FALSE
    /\ wrongThinking = FALSE /\ wrongContext = FALSE
    /\ ended = [c \in Clients |-> 0] /\ left = [c \in Clients |-> FALSE]
    /\ twice = FALSE

Connect(c) ==
    /\ c \notin connected
    /\ connected' = connected \cup {c}
    /\ since' = [since EXCEPT ![c] = next]
    /\ cursor' = [cursor EXCEPT ![c] = next]
    /\ UNCHANGED <<next, oldest, cueOf, shown, missed, repeated, start,
                   beats, during, lost, kind, gaze, looks, gazeDuring,
                   ended, left, answers>>

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
                   repeated, beats, during, lost, kind, gaze, looks,
                   gazeDuring, answers>>

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
                   beats, kind, gaze, looks, gazeDuring, ended, left,
                   answers>>

\* The camera's own gaze read changes (#142): stands for one frame's
\* composed gaze (src/screen_context.h's CoarseGazeFrom), worded as a
\* beat's own reply would word it. Every open beat's own gazeDuring is
\* overwritten with it, the model's stand-in for "the majority over the
\* window" (see the header comment).
Look(zone) ==
    /\ looks < MaxLooks
    /\ looks' = looks + 1
    /\ gaze' = zone
    /\ gazeDuring' = [c \in Clients |->
                        IF start[c] # 0 THEN zone ELSE gazeDuring[c]]
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, cursor, shown,
                   missed, repeated, start, beats, during, lost, kind,
                   ended, left, answers>>

Read(c) ==
    /\ c \in connected /\ start[c] = 0
    /\ Tell(c)
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, start, beats,
                   during, lost, kind, gaze, looks, gazeDuring, ended, left,
                   answers>>

\* The client marks a beat, with the kind it is answering for (#142's
\* kind[c]): the window opens at the next reading, and gazeDuring starts
\* from gaze right now.
Mark(c, k) ==
    /\ c \in connected /\ start[c] = 0 /\ beats[c] < MaxBeats
    /\ start' = [start EXCEPT ![c] = next]
    /\ beats' = [beats EXCEPT ![c] = @ + 1]
    /\ during' = [during EXCEPT ![c] = {}]
    /\ lost' = [lost EXCEPT ![c] = FALSE]
    /\ left' = [left EXCEPT ![c] = FALSE]
    /\ kind' = [kind EXCEPT ![c] = k]
    /\ gazeDuring' = [gazeDuring EXCEPT ![c] = gaze]
    /\ cursor' = IF GuardKeepBacklog THEN cursor
                 ELSE [cursor EXCEPT ![c] = next]
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, shown, missed,
                   repeated, gaze, looks, ended, answers>>

\* The feeling the feed gives c's beat as it ends now, from the cues
\* alone (#142: gaze is never one of Applied's inputs).
Answer(c) ==
    LET window == IF GuardWindowStart THEN start[c]..(next - 1)
                  ELSE cursor[c]..(next - 1)
        complete == \A i \in window : i >= oldest
        kept == {cueOf[i] : i \in {j \in window : j >= oldest}}
    IN IF complete \/ ~GuardDroppedUnknown THEN Applied(kept) ELSE "unknown"

\* The thinking guard's own trigger (#142): a question beat whose reply
\* will report gaze averted. A module-level operator, not a LET inside
\* EndBeat, so ContextNeverChangesFeeling and ThinkingGuardFires below
\* check the real `feeling` against this one fixed truth, never against
\* whatever EndBeat's own formula happens to compute it with -- the same
\* reason during/lost are ghosts kept apart from the feed's own cueOf. A
\* mutation that moves EndBeat's own gate away from this (rather than
\* just toggling GuardAvertedUnknown) is what proves ContextNeverChanges
\* Feeling bites (specs/README.md's own mutation table).
Averted(c) == kind[c] = "question" /\ gazeDuring[c] = "averted"

\* The window ends: the client is told what came since it last looked, and
\* given one interpretation of the readings made in the window, which is
\* checked against what really happened during it. `generic` is Answer(c),
\* the cue-only feeling; `feeling` is what the client is actually told,
\* `generic` unless the thinking guard (#142) applies.
EndBeat(c) ==
    /\ start[c] # 0
    /\ LET generic == Answer(c)
           truth == Rule(during[c])
           feeling == IF GuardAvertedUnknown /\ Averted(c) THEN "unknown"
                      ELSE generic
       IN /\ wrongFeeling' = (wrongFeeling \/
                (~lost[c] /\ truth # "unknown" /\ generic # truth))
          /\ wrongUnclear' = (wrongUnclear \/
                (~lost[c] /\ truth = "unknown" /\ generic # "unknown"))
          /\ wrongLost' = (wrongLost \/ (lost[c] /\ generic # "unknown"))
          /\ wrongThinking' = (wrongThinking \/
                (Averted(c) /\ feeling # "unknown"))
          /\ wrongContext' = (wrongContext \/
                (~Averted(c) /\ feeling # generic))
    /\ twice' = (twice \/ ended[c] = beats[c])
    /\ ended' = [ended EXCEPT ![c] = beats[c]]
    /\ start' = IF GuardCloseBeat THEN [start EXCEPT ![c] = 0] ELSE start
    /\ Tell(c)
    /\ UNCHANGED <<next, oldest, cueOf, connected, since, beats, during,
                   lost, kind, gaze, looks, gazeDuring, left>>

Next ==
    \/ \E cue \in Cues : React(cue)
    \/ \E zone \in Zones : Look(zone)
    \/ \E c \in Clients : Connect(c) \/ Disconnect(c) \/ Read(c) \/ EndBeat(c)
    \/ \E c \in Clients, k \in Kinds : Mark(c, k)

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
    /\ kind \in [Clients -> Kinds]
    /\ gaze \in Zones /\ looks \in 0..MaxLooks
    /\ gazeDuring \in [Clients -> Zones]
    /\ {wrongFeeling, wrongUnclear, wrongLost, wrongThinking, wrongContext,
       twice} \subseteq BOOLEAN
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

\* Gaze never changes the feeling a beat reports, except through the one
\* guarded exception (#142): "eyes never decide a feeling by rule".
ContextNeverChangesFeeling == ~wrongContext

\* The thinking guard fires exactly when it should (#142): a question
\* beat ending averted, with the guard on, always reads unknown.
ThinkingGuardFires == ~wrongThinking

\* Liveness: every beat ends, answered or abandoned.
BeatEnds ==
    \A c \in Clients, n \in 1..MaxBeats :
        (start[c] # 0 /\ beats[c] = n) ~> ended[c] >= n
==============================================================================
