---- MODULE TurnExcursion ----
\* Abstract pose as near or away; time confirmation is a single settled action.
CONSTANTS GuardReturn, GuardRepeat
VARIABLES latched, near, settled, reported, wasLatched
vars == <<latched, near, settled, reported, wasLatched>>
Init == /\ latched = FALSE /\ near = TRUE /\ settled = FALSE
        /\ reported = FALSE /\ wasLatched = FALSE
Turn == /\ (~latched \/ ~GuardRepeat)
        /\ latched' = TRUE /\ near' = FALSE /\ settled' = FALSE
        /\ reported' = TRUE /\ wasLatched' = latched
Away == /\ latched
        /\ near' = FALSE /\ settled' = FALSE /\ reported' = FALSE
        /\ wasLatched' = latched /\ UNCHANGED latched
Return == /\ latched /\ ~near
          /\ near' = TRUE /\ settled' = FALSE /\ reported' = FALSE
          /\ wasLatched' = latched /\ UNCHANGED latched
Settle == /\ latched /\ (near \/ ~GuardReturn)
          /\ latched' = FALSE /\ settled' = TRUE /\ reported' = FALSE
          /\ wasLatched' = latched /\ UNCHANGED near
Lost == /\ latched
        /\ latched' = FALSE /\ near' = TRUE /\ settled' = FALSE
        /\ reported' = FALSE /\ wasLatched' = latched
Next == Turn \/ Away \/ Return \/ Settle \/ Lost
Spec == Init /\ [][Next]_vars
NoRearmAway == settled => near
NoRepeat == reported => ~wasLatched
====
