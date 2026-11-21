---- MODULE FeelingFeedback ---------------------------------------------------
(***************************************************************************)
(* How the person's feeling for a beat is taken and kept (#35, item 4 of  *)
(* #61). A beat's reply carries an interpretation (specs/FeelingBeat.tla).*)
(* The person picks how they really felt, and the client sends it back:   *)
(* `feel <id> <feeling>`. facepalm takes it only for an interpretation it *)
(* gave that client, only once, and only within a time limit, then counts *)
(* it in the person's own file, which the daemon alone writes. A client   *)
(* can also ask facepalm to forget every count: `reset feelings`.         *)
(*                                                                         *)
(* The counts are modelled as the set of corrections they hold, each an   *)
(* <<interpretation, feeling>> pair: what matters here is which ones the  *)
(* file and the reads hold, not how they tune a read (src/tuning.cc, by   *)
(* its tests). A save writes the new counts to a temporary file, syncs    *)
(* it, renames it over the file, and syncs the directory; one save runs   *)
(* at a time. Every step can fail, and the daemon can stop at any moment  *)
(* (Restart): the file must then still be whole, and hold every           *)
(* correction it answered ok.                                              *)
(*                                                                         *)
(* `acked`, `taken`, `wrongClient`, `late` and `forgotten` are ghost      *)
(* variables, kept apart from what the store computes, so the invariants  *)
(* can hold it to the truth.                                               *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, FiniteSets

CONSTANTS
    Clients,               \* The clients that may give feelings.
    MaxInterpretations,    \* How many interpretations the model gives.
    Feelings,              \* The feelings a person may pick.
    MaxResets,             \* How many resets the model asks for.
    MaxRestarts,           \* How many times the daemon stops and restarts.
    GuardGivenOnly,        \* Only for an interpretation given to that client.
    GuardOnce,             \* Only once per interpretation.
    GuardRecent,           \* Only within the time limit.
    GuardOneWriter,        \* One save at a time.
    GuardTempFile,         \* A save writes a temporary file, then renames it.
    GuardSyncBeforeRename, \* The temporary file is synced before the rename.
    GuardAckDurable,       \* ok only once the directory is synced.
    GuardCommitOnRename,   \* The reads' counts change only at the rename.
    GuardResetEmpties,     \* A reset saves empty counts.
    None,                  \* A model value: no request, or no file.
    Torn,                  \* A model value: a file only part written.
    Clear                  \* A model value: a reset request.

Ids == 1..MaxInterpretations
Corrections == Ids \X Feelings
Stages == {"idle", "queued", "begun", "written", "synced", "renamed",
           "durable", "failed"}
\* The stages of a save between its start and its answer.
Saving == Stages \ {"idle", "queued"}

VARIABLES
    next,        \* The id the next interpretation gets.
    given,       \* given[c]: the ids given to c in this run of the daemon.
    expired,     \* The ids whose time limit has passed.
    used,        \* The ids a feeling was taken for.
    request,     \* request[c]: None, or the feel or reset c is waiting on.
    stage,       \* stage[c]: how far c's save has got.
    saving,      \* saving[c]: the counts c's save writes.
    disk,        \* The counts in the file, or Torn.
    diskSynced,  \* The file's data is on the disk, not only in the cache.
    before,      \* The file before a rename the directory sync has not kept.
    temp,        \* The temporary file's counts, None, or Torn.
    tempSynced,  \* The temporary file's data is on the disk.
    memory,      \* The counts the reads use.
    acked,       \* Corrections answered ok since a reset was renamed in.
    taken,       \* taken[i]: how many feelings were taken for i.
    wrongClient, \* A feeling was taken for an id not given to the client.
    late,        \* A feeling was taken past the time limit.
    forgotten,   \* forgotten[c]: what c's reset must clear.
    resets,      \* How many resets were asked for.
    restarts     \* How many times the daemon restarted.

ghosts == <<acked, taken, wrongClient, late, forgotten>>
vars == <<next, given, expired, used, request, stage, saving, disk,
          diskSynced, before, temp, tempSynced, memory, resets, restarts,
          ghosts>>

IsFeel(c) == request[c] # None /\ request[c] # Clear

\* No other client's save is under way.
Alone(c) == \A d \in Clients \ {c} : stage[d] \notin Saving

\* What a feel for interpretation i needs, with every protection on.
Fresh(c, i) == i \in given[c] /\ i \notin used /\ i \notin expired

Init ==
    /\ next = 1 /\ given = [c \in Clients |-> {}]
    /\ expired = {} /\ used = {}
    /\ request = [c \in Clients |-> None]
    /\ stage = [c \in Clients |-> "idle"]
    /\ saving = [c \in Clients |-> {}]
    /\ disk = {} /\ diskSynced = TRUE /\ before = None
    /\ temp = None /\ tempSynced = TRUE
    /\ memory = {}
    /\ acked = {} /\ taken = [i \in Ids |-> 0]
    /\ wrongClient = FALSE /\ late = FALSE
    /\ forgotten = [c \in Clients |-> {}]
    /\ resets = 0 /\ restarts = 0

\* A beat ends, and its interpretation is given to client c.
Give(c) ==
    /\ next <= MaxInterpretations
    /\ given' = [given EXCEPT ![c] = @ \cup {next}]
    /\ next' = next + 1
    /\ UNCHANGED <<expired, used, request, stage, saving, disk, diskSynced,
                   before, temp, tempSynced, memory, resets, restarts,
                   ghosts>>

\* Time passes: interpretation i can no longer be given a feeling.
Expire(i) ==
    /\ i < next /\ i \notin expired
    /\ expired' = expired \cup {i}
    /\ UNCHANGED <<next, given, used, request, stage, saving, disk,
                   diskSynced, before, temp, tempSynced, memory, resets,
                   restarts, ghosts>>

\* Client c sends `feel i f`, and facepalm takes it. A feel it refuses is
\* answered at once and changes nothing, so it is not modelled.
Feel(c, i, f) ==
    /\ request[c] = None /\ i < next
    /\ i \in given[c] \/ ~GuardGivenOnly
    /\ i \notin used \/ ~GuardOnce
    /\ i \notin expired \/ ~GuardRecent
    /\ used' = used \cup {i}
    /\ taken' = [taken EXCEPT ![i] = @ + 1]
    /\ wrongClient' = (wrongClient \/ i \notin given[c])
    /\ late' = (late \/ i \in expired)
    /\ request' = [request EXCEPT ![c] = <<i, f>>]
    /\ stage' = [stage EXCEPT ![c] = "queued"]
    /\ UNCHANGED <<next, given, expired, saving, disk, diskSynced, before,
                   temp, tempSynced, memory, resets, restarts, acked,
                   forgotten>>

\* Client c sends `reset feelings`.
Reset(c) ==
    /\ request[c] = None /\ resets < MaxResets
    /\ resets' = resets + 1
    /\ request' = [request EXCEPT ![c] = Clear]
    /\ stage' = [stage EXCEPT ![c] = "queued"]
    /\ forgotten' = [forgotten EXCEPT ![c] = memory]
    /\ UNCHANGED <<next, given, expired, used, saving, disk, diskSynced,
                   before, temp, tempSynced, memory, restarts, acked, taken,
                   wrongClient, late>>

\* c's save starts from the counts the reads use.
Begin(c) ==
    /\ stage[c] = "queued"
    /\ Alone(c) \/ ~GuardOneWriter
    /\ LET counts == IF IsFeel(c) THEN memory \cup {request[c]}
                     ELSE IF GuardResetEmpties THEN {} ELSE memory
       IN /\ saving' = [saving EXCEPT ![c] = counts]
          /\ memory' = IF GuardCommitOnRename THEN memory ELSE counts
    /\ stage' = [stage EXCEPT ![c] = "begun"]
    /\ UNCHANGED <<next, given, expired, used, request, disk, diskSynced,
                   before, temp, tempSynced, resets, restarts, ghosts>>

\* The counts are written: to the temporary file, or, without
\* GuardTempFile, over the file itself, which is torn until it is done.
Write(c) ==
    /\ stage[c] = "begun"
    /\ IF GuardTempFile
          THEN /\ temp' = saving[c] /\ tempSynced' = FALSE
               /\ UNCHANGED <<disk, diskSynced, before>>
          ELSE /\ disk' = Torn /\ diskSynced' = FALSE /\ before' = None
               /\ UNCHANGED <<temp, tempSynced>>
    /\ stage' = [stage EXCEPT ![c] = "written"]
    /\ UNCHANGED <<next, given, expired, used, request, saving, memory,
                   resets, restarts, ghosts>>

SyncTemp(c) ==
    /\ stage[c] = "written"
    /\ tempSynced' = (GuardTempFile \/ tempSynced)
    /\ stage' = [stage EXCEPT ![c] = "synced"]
    /\ UNCHANGED <<next, given, expired, used, request, saving, disk,
                   diskSynced, before, temp, memory, resets, restarts,
                   ghosts>>

\* The temporary file replaces the file, and the reads take its counts. A
\* reset renamed in ends what was acknowledged before it.
Rename(c) ==
    /\ \/ stage[c] = "synced"
       \/ stage[c] = "written" /\ ~GuardSyncBeforeRename
    /\ IF GuardTempFile
          THEN /\ disk' = temp /\ diskSynced' = tempSynced
               /\ before' = disk /\ temp' = None
          ELSE /\ disk' = saving[c] /\ diskSynced' = (stage[c] = "synced")
               /\ UNCHANGED <<before, temp>>
    /\ memory' = IF GuardCommitOnRename THEN saving[c] ELSE memory
    /\ acked' = IF IsFeel(c) THEN acked ELSE {}
    /\ stage' = [stage EXCEPT ![c] = "renamed"]
    /\ UNCHANGED <<next, given, expired, used, request, saving, tempSynced,
                   resets, restarts, taken, wrongClient, late, forgotten>>

\* The directory is synced: the rename survives a crash.
SyncDirectory(c) ==
    /\ stage[c] = "renamed"
    /\ before' = None
    /\ stage' = [stage EXCEPT ![c] = "durable"]
    /\ UNCHANGED <<next, given, expired, used, request, saving, disk,
                   diskSynced, temp, tempSynced, memory, resets, restarts,
                   ghosts>>

\* A step fails. Before the rename, the temporary file may be left part
\* written; after it, the directory sync has failed.
Fail(c) ==
    /\ stage[c] \in {"begun", "written", "synced", "renamed"}
    /\ temp' = IF stage[c] \in {"written", "synced"} /\ GuardTempFile
               THEN Torn ELSE temp
    /\ stage' = [stage EXCEPT ![c] = "failed"]
    /\ UNCHANGED <<next, given, expired, used, request, saving, disk,
                   diskSynced, before, tempSynced, memory, resets, restarts,
                   ghosts>>

\* c is answered ok, and a feeling counts as acknowledged.
ReplyOk(c) ==
    /\ \/ stage[c] = "durable"
       \/ stage[c] = "renamed" /\ ~GuardAckDurable
    /\ acked' = IF IsFeel(c) THEN acked \cup {request[c]} ELSE acked
    /\ request' = [request EXCEPT ![c] = None]
    /\ stage' = [stage EXCEPT ![c] = "idle"]
    /\ UNCHANGED <<next, given, expired, used, saving, disk, diskSynced,
                   before, temp, tempSynced, memory, resets, restarts, taken,
                   wrongClient, late, forgotten>>

ReplyError(c) ==
    /\ stage[c] = "failed"
    /\ request' = [request EXCEPT ![c] = None]
    /\ stage' = [stage EXCEPT ![c] = "idle"]
    /\ UNCHANGED <<next, given, expired, used, saving, disk, diskSynced,
                   before, temp, tempSynced, memory, resets, restarts,
                   ghosts>>

\* The daemon stops at any moment and starts again. A rename the directory
\* sync did not keep may be lost, and data never synced may be lost from
\* the file. The new run loads the file, and has given no interpretation.
Restart ==
    /\ restarts < MaxRestarts
    /\ restarts' = restarts + 1
    /\ \E kept \in {disk} \cup (IF before = None THEN {} ELSE {before}) :
          LET file == IF kept = disk /\ ~diskSynced THEN Torn ELSE kept
          IN /\ disk' = file
             /\ memory' = IF file = Torn THEN {} ELSE file
    /\ diskSynced' = TRUE /\ before' = None
    /\ given' = [c \in Clients |-> {}]
    /\ request' = [c \in Clients |-> None]
    /\ stage' = [c \in Clients |-> "idle"]
    /\ UNCHANGED <<next, expired, used, saving, temp, tempSynced, resets,
                   ghosts>>

Next ==
    \/ Restart
    \/ \E i \in Ids : Expire(i)
    \/ \E c \in Clients :
          \/ Give(c) \/ Reset(c) \/ Begin(c) \/ Write(c) \/ SyncTemp(c)
          \/ Rename(c) \/ SyncDirectory(c) \/ Fail(c) \/ ReplyOk(c)
          \/ ReplyError(c)
          \/ \E i \in Ids, f \in Feelings : Feel(c, i, f)

Spec ==
    /\ Init /\ [][Next]_vars
    /\ \A c \in Clients :
          /\ WF_vars(Begin(c)) /\ WF_vars(Write(c)) /\ WF_vars(SyncTemp(c))
          /\ WF_vars(Rename(c)) /\ WF_vars(SyncDirectory(c))
          /\ WF_vars(ReplyOk(c)) /\ WF_vars(ReplyError(c))

TypeOK ==
    /\ next \in 1..(MaxInterpretations + 1)
    /\ given \in [Clients -> SUBSET Ids]
    /\ expired \subseteq Ids /\ used \subseteq Ids
    /\ request \in [Clients -> {None, Clear} \cup Corrections]
    /\ stage \in [Clients -> Stages]
    /\ saving \in [Clients -> SUBSET Corrections]
    /\ disk \in (SUBSET Corrections) \cup {Torn}
    /\ before \in (SUBSET Corrections) \cup {None, Torn}
    /\ temp \in (SUBSET Corrections) \cup {None, Torn}
    /\ {diskSynced, tempSynced, wrongClient, late} \subseteq BOOLEAN
    /\ memory \subseteq Corrections /\ acked \subseteq Corrections
    /\ taken \in [Ids -> Nat]
    /\ forgotten \in [Clients -> SUBSET Corrections]
    /\ resets \in 0..MaxResets /\ restarts \in 0..MaxRestarts

\* A feeling is taken only for an interpretation given to that client, so
\* it answers what that client's person was shown.
OnlyGiven == ~wrongClient

\* At most one feeling is taken for each interpretation.
OncePerInterpretation == \A i \in Ids : taken[i] <= 1

\* A feeling comes within the time limit, while the person remembers it.
OnlyRecent == ~late

\* The file is always whole: a crash leaves the old counts or the new.
NoTornFile == disk # Torn

\* The reads use the counts in the file, never ones it failed to save.
ReadsTheFile == memory = disk

\* A feeling answered ok stays in the file, through a crash, until a reset.
AckedKept == disk # Torn => acked \subseteq disk

\* A reset answered ok has cleared every count there was when it was asked.
ResetForgets ==
    \A c \in Clients :
        (request[c] = Clear /\ stage[c] = "durable") =>
            disk \cap forgotten[c] = {}

\* Something must happen: a fresh feeling from a client with nothing under
\* way is taken.
CanLearn ==
    \A c \in Clients, i \in Ids, f \in Feelings :
        (Fresh(c, i) /\ request[c] = None) => ENABLED Feel(c, i, f)

\* Liveness: every feel and reset that is taken is answered, ok or not.
Answered == \A c \in Clients : request[c] # None ~> request[c] = None
==============================================================================
