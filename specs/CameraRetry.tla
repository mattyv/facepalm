---- MODULE CameraRetry ------------------------------------------------------
EXTENDS Naturals, FiniteSets
CONSTANTS MaxEpoch, MaxDelay, GuardIdentity, GuardCap
VARIABLES epoch, wanted, pending, delay, opens, staleOpen
vars == <<epoch, wanted, pending, delay, opens, staleOpen>>
Init == /\ epoch = 0 /\ wanted = TRUE /\ pending = {} /\ delay = 1
        /\ opens = 0 /\ staleOpen = FALSE
Fail == /\ wanted /\ pending' = pending \cup {[id |-> epoch, wait |-> delay]}
        /\ delay' = IF GuardCap THEN IF delay * 2 <= MaxDelay THEN delay * 2 ELSE MaxDelay ELSE delay * 2
        /\ UNCHANGED <<epoch, wanted, opens, staleOpen>>
Cancel == /\ wanted /\ epoch < MaxEpoch /\ epoch' = epoch + 1
          /\ wanted' = FALSE /\ delay' = 1
          /\ UNCHANGED <<pending, opens, staleOpen>>
Demand == /\ ~wanted /\ wanted' = TRUE
          /\ UNCHANGED <<epoch, pending, delay, opens, staleOpen>>
Fire(t) == /\ t \in pending /\ pending' = pending \ {t}
           /\ IF wanted /\ (t.id = epoch \/ ~GuardIdentity)
                 THEN /\ opens' = 1
                      /\ staleOpen' = (staleOpen \/ (t.id # epoch))
                 ELSE UNCHANGED <<opens, staleOpen>>
           /\ UNCHANGED <<epoch, wanted, delay>>
Next == Fail \/ Cancel \/ Demand \/ \E t \in pending: Fire(t)
Spec == Init /\ [][Next]_vars
NoStaleOpen == ~staleOpen
BackoffBounded == delay <= MaxDelay
=============================================================================
