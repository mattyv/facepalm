---- MODULE RatingPersistence ----
\* One append at a time; a failed or partial write poisons the sink.
CONSTANTS GuardSync, GuardPoison
VARIABLES state, synced, acknowledged, failed
vars == <<state, synced, acknowledged, failed>>
Init == /\ state = "ready" /\ synced = FALSE
        /\ acknowledged = FALSE /\ failed = FALSE
Begin == /\ state = "ready" /\ ( ~failed \/ ~GuardPoison )
         /\ state' = "writing" /\ synced' = FALSE /\ acknowledged' = FALSE
         /\ UNCHANGED failed
Write == /\ state = "writing" /\ state' = "written"
         /\ UNCHANGED <<synced, acknowledged, failed>>
Sync == /\ state = "written" /\ state' = "synced" /\ synced' = TRUE
        /\ UNCHANGED <<acknowledged, failed>>
Ack == /\ state \in {"written", "synced"} /\ (synced \/ ~GuardSync)
       /\ state' = "ready" /\ acknowledged' = TRUE
       /\ UNCHANGED <<synced, failed>>
Fail == /\ state \in {"writing", "written"}
        /\ state' = "ready" /\ failed' = TRUE /\ acknowledged' = FALSE
        /\ UNCHANGED synced
Next == Begin \/ Write \/ Sync \/ Ack \/ Fail
Spec == Init /\ [][Next]_vars
OnlyAckSynced == acknowledged => synced
NoWriteAfterFailure == failed => state = "ready"
====
