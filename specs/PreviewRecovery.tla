---- MODULE PreviewRecovery ----
\* Menu preview demand survives a live daemon's opening/retry; close releases it.
CONSTANT GuardRetain, GuardClear, GuardAttribution
VARIABLES open, phase, requested, staleFrame, identity, currentReason, dropped
vars == <<open, phase, requested, staleFrame, identity, currentReason, dropped>>
Init == /\ open = FALSE /\ phase = "idle" /\ requested = FALSE
        /\ staleFrame = FALSE /\ identity = FALSE /\ currentReason = FALSE
        /\ dropped = FALSE
Start == /\ ~open /\ open' = TRUE /\ requested' = TRUE
         /\ UNCHANGED <<phase, staleFrame, identity, currentReason, dropped>>
Retry == /\ open /\ phase # "retrying"
         /\ phase' = "retrying" /\ requested' = GuardRetain
         /\ staleFrame' = ~GuardClear /\ identity' \in BOOLEAN
         /\ currentReason' = (identity' \/ ~GuardAttribution)
         /\ dropped' = (dropped \/ ~GuardRetain)
         /\ UNCHANGED open
Recover == /\ phase = "retrying" /\ phase' = "active"
           /\ staleFrame' = FALSE /\ currentReason' = FALSE
           /\ UNCHANGED <<open, requested, identity, dropped>>
Close == /\ open /\ open' = FALSE /\ requested' = FALSE
         /\ currentReason' = FALSE /\ UNCHANGED <<phase, staleFrame, identity, dropped>>
Next == Start \/ Retry \/ Recover \/ Close
Spec == Init /\ [][Next]_vars
RetryRetains == ~dropped
NoStaleDuringRetry == phase = "retrying" /\ open => ~staleFrame
ReasonNeedsIdentity == currentReason => identity
====
