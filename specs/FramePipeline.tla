---- MODULE FramePipeline -----------------------------------------------------
(***************************************************************************)
(* How a captured frame reaches the face model, refining CameraLifecycle's *)
(* Capture, Emit and Drop. Three serial queues share one frame slot:       *)
(*                                                                         *)
(*   capture:   the camera's callback copies the frame into the slot, as   *)
(*              RGBA, and the frame is gone when the callback returns;     *)
(*   control:   CameraController, which opens and closes the camera and    *)
(*              gives the frame a ticket, or refuses one when not open;    *)
(*   inference: runs its tasks in order: create the model when the camera  *)
(*              opens, detect on a ticketed frame, destroy the model when  *)
(*              the camera closes.                                         *)
(*                                                                         *)
(* The slot is free, copying, copied, ticketed or inferring. Each Guard    *)
(* constant is one of the design's protections; a mutation sets one to     *)
(* FALSE in a copy of the config (specs/README.md lists what each breaks). *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS
    MaxOpens,             \* How many times the camera may open.
    GuardOneSlot,         \* A frame is copied in only when the slot is free.
    GuardFreeOnRefusal,   \* A refused ticket frees the slot.
    GuardFreeOnError,     \* A failed detect frees the slot.
    GuardSkipAbsent,      \* A detect with no model frees the slot instead.
    GuardOneQueue,        \* The model is destroyed on the inference queue.
    GuardDestroyOnClose   \* Closing the camera queues the model's destroy.

VARIABLES
    camera,     \* "closed" or "open", as the control queue sees it.
    opens,      \* How many times the camera has opened.
    model,      \* "absent" or "ready".
    tasks,      \* The inference queue: "create", "detect" or "destroy".
    slot,       \* "free", "copied", "ticketed" or "inferring".
    copying,    \* The capture callback is writing the slot's buffer.
    inFlight,   \* Frames copied in and not yet freed.
    detecting,  \* The inference queue is running a detect.
    blindDetect \* A detect ran without a ready model.

vars == <<camera, opens, model, tasks, slot, copying, inFlight, detecting,
          blindDetect>>

Init ==
    /\ camera = "closed" /\ opens = 0 /\ model = "absent" /\ tasks = <<>>
    /\ slot = "free" /\ copying = FALSE /\ inFlight = 0
    /\ detecting = FALSE /\ blindDetect = FALSE

(* Control queue: the camera opening and closing. *)

Open ==
    /\ camera = "closed" /\ opens < MaxOpens
    /\ camera' = "open" /\ opens' = opens + 1
    /\ tasks' = Append(tasks, "create")
    /\ UNCHANGED <<model, slot, copying, inFlight, detecting, blindDetect>>

Close ==
    /\ camera = "open" /\ camera' = "closed"
    /\ IF GuardOneQueue
         THEN /\ tasks' = IF GuardDestroyOnClose
                             THEN Append(tasks, "destroy") ELSE tasks
              /\ UNCHANGED model
         \* The wrong design: destroyed at once, on the control queue.
         ELSE /\ model' = "absent" /\ UNCHANGED tasks
    /\ UNCHANGED <<opens, slot, copying, inFlight, detecting, blindDetect>>

\* The controller's OnFrameCaptured: a ticket while open, otherwise none.
Ticket ==
    /\ slot = "copied"
    /\ IF camera = "open"
         THEN /\ slot' = "ticketed" /\ tasks' = Append(tasks, "detect")
              /\ UNCHANGED inFlight
         ELSE /\ slot' = IF GuardFreeOnRefusal THEN "free" ELSE slot
              /\ inFlight' = IF GuardFreeOnRefusal THEN inFlight - 1
                                                   ELSE inFlight
              /\ UNCHANGED tasks
    /\ UNCHANGED <<camera, opens, model, copying, detecting, blindDetect>>

(* Capture queue: a frame arrives, at any time, even just after closing. *)

CaptureStart ==
    /\ ~copying
    /\ (slot = "free" \/ ~GuardOneSlot)
    /\ copying' = TRUE
    /\ UNCHANGED <<camera, opens, model, tasks, slot, inFlight, detecting,
                   blindDetect>>

CaptureEnd ==
    /\ copying /\ copying' = FALSE
    /\ slot' = "copied" /\ inFlight' = inFlight + 1
    /\ UNCHANGED <<camera, opens, model, tasks, detecting, blindDetect>>

(* Inference queue: one task at a time, in order. A detect holds the     *)
(* queue until it ends, so nothing else on it runs meanwhile.            *)

Idle == ~detecting

CreateModel ==
    /\ Idle /\ tasks # <<>> /\ Head(tasks) = "create"
    /\ model' \in {"ready", "absent"}  \* Loading the model can fail.
    /\ tasks' = Tail(tasks)
    /\ UNCHANGED <<camera, opens, slot, copying, inFlight, detecting,
                   blindDetect>>

DestroyModel ==
    /\ Idle /\ tasks # <<>> /\ Head(tasks) = "destroy"
    /\ model' = "absent" /\ tasks' = Tail(tasks)
    /\ UNCHANGED <<camera, opens, slot, copying, inFlight, detecting,
                   blindDetect>>

DetectStart ==
    /\ Idle /\ tasks # <<>> /\ Head(tasks) = "detect"
    /\ tasks' = Tail(tasks)
    /\ IF model = "ready" \/ ~GuardSkipAbsent
         THEN /\ slot' = "inferring" /\ detecting' = TRUE
              /\ UNCHANGED inFlight
         ELSE /\ slot' = "free" /\ inFlight' = inFlight - 1
              /\ UNCHANGED detecting
    /\ UNCHANGED <<camera, opens, model, copying, blindDetect>>

\* Detect returns a face or no face; the reaction goes to the controller.
DetectEnd ==
    /\ detecting /\ detecting' = FALSE
    /\ blindDetect' = (blindDetect \/ model # "ready")
    /\ slot' = "free" /\ inFlight' = inFlight - 1
    /\ UNCHANGED <<camera, opens, model, tasks, copying>>

\* Detect fails: the model reports an error. Without the guard, the slot
\* is left as it was, and nothing frees it.
DetectFails ==
    /\ detecting /\ detecting' = FALSE
    /\ blindDetect' = (blindDetect \/ model # "ready")
    /\ slot' = IF GuardFreeOnError THEN "free" ELSE slot
    /\ inFlight' = IF GuardFreeOnError THEN inFlight - 1 ELSE inFlight
    /\ UNCHANGED <<camera, opens, model, tasks, copying>>

Next ==
    \/ Open \/ Close \/ Ticket
    \/ CaptureStart \/ CaptureEnd
    \/ CreateModel \/ DestroyModel \/ DetectStart \/ DetectEnd \/ DetectFails

Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Ticket)
    /\ WF_vars(CreateModel) /\ WF_vars(DestroyModel)
    /\ WF_vars(DetectStart) /\ WF_vars(DetectEnd \/ DetectFails)

\* Bounds the state space when a mutation lets frames pile up.
StateBound == inFlight <= 2

TypeOK ==
    /\ camera \in {"closed", "open"} /\ opens \in 0..MaxOpens
    /\ model \in {"absent", "ready"}
    /\ \A i \in 1..Len(tasks): tasks[i] \in {"create", "detect", "destroy"}
    /\ slot \in {"free", "copied", "ticketed", "inferring"}
    /\ copying \in BOOLEAN /\ inFlight \in 0..2
    /\ detecting \in BOOLEAN /\ blindDetect \in BOOLEAN

\* Safety.
NoDetectWithoutModel == /\ detecting => model = "ready"
                        /\ ~blindDetect
NeverWrittenWhileRead == ~(copying /\ detecting)
AtMostOneInFlight == inFlight <= 1

\* Something must happen: a ticketed frame next in line, with the model
\* ready, can be detected.
CanDetect ==
    (slot = "ticketed" /\ model = "ready" /\ tasks # <<>> /\
     Head(tasks) = "detect") => ENABLED (DetectStart /\ slot' = "inferring")

\* Liveness.
SlotFreed == slot # "free" ~> slot = "free"
ModelGoneAfterClose == camera = "closed" ~> model = "absent"
==============================================================================
