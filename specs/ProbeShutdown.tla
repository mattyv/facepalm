---- MODULE ProbeShutdown ----
\* After camera closure, the probe requests model retirement on inference.
\* Only inference completion allows control to exit the process.
CONSTANT WaitForRetirement
VARIABLES model, exited
vars == <<model, exited>>

Init == /\ model = "queued"
        /\ exited = FALSE

BeginRetirement == /\ ~exited /\ model = "queued"
                   /\ model' = "closing"
                   /\ UNCHANGED exited
FinishRetirement == /\ ~exited /\ model = "closing"
                    /\ model' = "retired"
                    /\ UNCHANGED exited
Exit == /\ ~exited
        /\ (~WaitForRetirement \/ model = "retired")
        /\ exited' = TRUE
        /\ UNCHANGED model

Next == BeginRetirement \/ FinishRetirement \/ Exit
Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(BeginRetirement)
        /\ WF_vars(FinishRetirement)
        /\ WF_vars(Exit)

ExitAfterRetirement == exited => model = "retired"
EventuallyExits == <>exited
====
