---- MODULE CalibrationRetirement ----
CONSTANT GuardClose
VARIABLES state, connected
vars == <<state, connected>>
Init == /\ state = "idle" /\ connected = FALSE
Start == /\ state = "idle" /\ state' = "prompting" /\ connected' = TRUE
Finish == /\ state = "prompting" /\ state' \in {"done", "failed", "idle"}
          /\ connected' = ~GuardClose
Close == /\ state \in {"done", "failed"} /\ state' = "idle"
         /\ connected' = FALSE
Next == Start \/ Finish \/ Close
Spec == Init /\ [][Next]_vars
TerminalReleases == state \in {"done", "failed", "idle"} => ~connected
====
