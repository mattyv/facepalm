---- MODULE ProbeShutdown ----
\* When the camera probe may exit, and what its exit code may claim.
\*
\* After the camera closes, the probe retires the face model on the inference
\* queue; only once that is done may control exit the process (#71). Given a
\* directory, the probe also records every frame: the capture queue copies
\* a frame onto the recorder's serial write queue, which saves it, and the
\* backlog it may hold is bounded (#41). Once the model is retired, control
\* finishes the recorder: a task queued behind every write. Control exits
\* when that task has run, with 0 only when every frame captured was saved.
\* No frame arrives once the camera has closed: AvFoundationSession::Stop
\* waits for its capture queue.
\*
\* Each Guard constant is TRUE in ProbeShutdown.cfg; setting one FALSE is a
\* mutation that one property must catch (specs/README.md).
EXTENDS Naturals

CONSTANTS
    Capacity,           \* Frames the write queue may hold.
    MaxFrames,          \* Frames the camera captures, to bound the model.
    WaitForRetirement,  \* Exit waits for the model to be retired.
    GuardBound,         \* A frame past Capacity is dropped, not queued.
    GuardDrain,         \* Exit waits for the finish task, behind the writes.
    GuardReportLoss     \* Exit says failure when a frame was lost.

VARIABLES
    capturing,  \* The camera is still delivering frames.
    captured,   \* Frames the camera delivered.
    pending,    \* Frames on the write queue.
    saved,      \* Frames written with their capture time.
    lost,       \* Frames dropped at a full queue, or whose write failed.
    model,      \* "queued", "closing" or "retired".
    finishing,  \* Control has queued the recorder's finish task.
    finished,   \* That task has run.
    exited,
    code        \* The exit code: 0 or 1, meaningful once exited.

vars == <<capturing, captured, pending, saved, lost, model, finishing,
          finished, exited, code>>

TypeOK ==
    /\ capturing \in BOOLEAN
    /\ captured \in 0..MaxFrames
    /\ pending \in 0..MaxFrames
    /\ saved \in 0..MaxFrames
    /\ lost \in 0..MaxFrames
    /\ model \in {"queued", "closing", "retired"}
    /\ finishing \in BOOLEAN
    /\ finished \in BOOLEAN
    /\ exited \in BOOLEAN
    /\ code \in {0, 1}

Init ==
    /\ capturing = TRUE
    /\ captured = 0
    /\ pending = 0
    /\ saved = 0
    /\ lost = 0
    /\ model = "queued"
    /\ finishing = FALSE
    /\ finished = FALSE
    /\ exited = FALSE
    /\ code = 0

\* The capture queue's Record: queue a copy, or drop the frame at a full
\* queue.
Capture ==
    /\ capturing
    /\ captured < MaxFrames
    /\ captured' = captured + 1
    /\ IF pending < Capacity \/ ~GuardBound
         THEN /\ pending' = pending + 1
              /\ UNCHANGED lost
         ELSE /\ lost' = lost + 1
              /\ UNCHANGED pending
    /\ UNCHANGED <<capturing, saved, model, finishing, finished, exited, code>>

\* The camera closes: SessionCamera reports OnClosed.
StopCapture ==
    /\ capturing
    /\ capturing' = FALSE
    /\ UNCHANGED <<captured, pending, saved, lost, model, finishing, finished,
                   exited, code>>

\* The write queue saves the frame at its head, image and capture time.
WriteSaves ==
    /\ pending > 0
    /\ pending' = pending - 1
    /\ saved' = saved + 1
    /\ UNCHANGED <<capturing, captured, lost, model, finishing, finished,
                   exited, code>>

\* Or fails to: the image or its time could not be written.
WriteFails ==
    /\ pending > 0
    /\ pending' = pending - 1
    /\ lost' = lost + 1
    /\ UNCHANGED <<capturing, captured, saved, model, finishing, finished,
                   exited, code>>

\* OnClosed queues the model's retirement on the inference queue.
BeginRetirement ==
    /\ ~exited
    /\ ~capturing
    /\ model = "queued"
    /\ model' = "closing"
    /\ UNCHANGED <<capturing, captured, pending, saved, lost, finishing,
                   finished, exited, code>>

FinishRetirement ==
    /\ ~exited
    /\ model = "closing"
    /\ model' = "retired"
    /\ UNCHANGED <<capturing, captured, pending, saved, lost, finishing,
                   finished, exited, code>>

\* Control, told the model is retired, queues the recorder's finish task.
Finish ==
    /\ ~exited
    /\ ~finishing
    /\ model = "retired" \/ ~WaitForRetirement
    /\ ~capturing
    /\ finishing' = TRUE
    /\ UNCHANGED <<capturing, captured, pending, saved, lost, model, finished,
                   exited, code>>

\* The finish task runs on the serial write queue, behind every write queued
\* before it.
FinishRuns ==
    /\ finishing
    /\ ~finished
    /\ pending = 0 \/ ~GuardDrain
    /\ finished' = TRUE
    /\ UNCHANGED <<capturing, captured, pending, saved, lost, model, finishing,
                   exited, code>>

\* Control exits, with 0 only when nothing was lost.
Exit ==
    /\ ~exited
    /\ finished
    /\ exited' = TRUE
    /\ code' = IF lost = 0 \/ ~GuardReportLoss THEN 0 ELSE 1
    /\ UNCHANGED <<capturing, captured, pending, saved, lost, model, finishing,
                   finished>>

Next ==
    \/ Capture \/ StopCapture \/ WriteSaves \/ WriteFails
    \/ BeginRetirement \/ FinishRetirement \/ Finish \/ FinishRuns \/ Exit

\* The probe's timer always closes the camera, and everything queued runs. A
\* write may save or fail, so its two outcomes share one fairness condition.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(StopCapture)
    /\ WF_vars(WriteSaves \/ WriteFails)
    /\ WF_vars(BeginRetirement)
    /\ WF_vars(FinishRetirement)
    /\ WF_vars(Finish)
    /\ WF_vars(FinishRuns)
    /\ WF_vars(Exit)

\* The process exits only once the model is retired (#71).
ExitAfterRetirement == exited => model = "retired"

\* The backlog of copies never passes its bound (#41).
BacklogBounded == pending <= Capacity

\* The process exits only once every accepted frame has been written or has
\* failed (#41).
ExitAfterDrain == exited => pending = 0

\* An exit code of 0 means the recording holds every frame the camera
\* captured (#41).
SuccessMeansComplete ==
    (exited /\ code = 0) => saved = captured

\* Something must happen: the probe exits.
EventuallyExits == <>exited
====
