---- MODULE MenuReadiness ----
CONSTANT GuardReady, GuardFailureTruth, GuardOffTruth, GuardShowReady, GuardShowFailure
VARIABLES installed, socket, failed, starting
vars == <<installed, socket, failed, starting>>
Init == /\ installed = FALSE /\ socket = FALSE /\ failed = FALSE /\ starting = FALSE
TurnOn == /\ ~installed /\ installed' = TRUE /\ starting' = TRUE
          /\ UNCHANGED <<socket, failed>>
TurnOff == /\ installed /\ installed' = FALSE /\ socket' = FALSE
           /\ failed' = FALSE /\ starting' = FALSE
Start == /\ installed /\ starting /\ socket' = TRUE /\ starting' = FALSE
         /\ failed' = FALSE /\ UNCHANGED installed
Fail == /\ installed /\ socket' = FALSE /\ starting' = FALSE /\ failed' = TRUE
        /\ UNCHANGED installed
Unavailable == /\ installed /\ socket' = FALSE /\ starting' = FALSE
               /\ failed' = FALSE /\ UNCHANGED installed
Retry == /\ installed /\ ~socket /\ starting' = TRUE /\ failed' = FALSE
         /\ UNCHANGED <<installed, socket>>
Next == TurnOn \/ TurnOff \/ Start \/ Fail \/ Unavailable \/ Retry
Spec == Init /\ [][Next]_vars
Enabled == installed /\ ((socket /\ GuardShowReady) \/ ~GuardReady)
ShowsFailure == installed /\ ((failed /\ GuardShowFailure) \/ ~GuardFailureTruth)
ShowsOff == ~installed \/ ~GuardOffTruth
ReadyEnabled == installed /\ socket => Enabled
FailureShown == installed /\ failed => ShowsFailure
ReadyOnly == Enabled => socket
FailureOnly == ShowsFailure => failed
OffOnly == ShowsOff => ~installed
====
