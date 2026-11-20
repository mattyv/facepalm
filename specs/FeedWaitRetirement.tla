---- MODULE FeedWaitRetirement ----
\* The old waiter retains a cancellation token even if its entry is erased.
CONSTANTS GuardLifetime, GuardGeneration
VARIABLES entry, oldWait, successor, canceled, staleAccess, staleAnswer
vars == <<entry, oldWait, successor, canceled, staleAccess, staleAnswer>>
Init == /\ entry = TRUE /\ oldWait = TRUE /\ successor = FALSE
        /\ canceled = FALSE /\ staleAccess = FALSE /\ staleAnswer = FALSE
Disconnect == /\ oldWait /\ ~successor /\ successor' = TRUE
              /\ canceled' = GuardGeneration
              /\ UNCHANGED <<entry, oldWait, staleAccess, staleAnswer>>
Forget == /\ entry /\ entry' = FALSE
          /\ canceled' = (canceled \/ GuardLifetime)
          /\ UNCHANGED <<oldWait, successor, staleAccess, staleAnswer>>
OldReturns == /\ oldWait /\ oldWait' = FALSE
              /\ staleAccess' = (~entry /\ ~canceled)
              /\ staleAnswer' = (successor /\ ~canceled)
              /\ UNCHANGED <<entry, successor, canceled>>
Next == Disconnect \/ Forget \/ OldReturns
Spec == Init /\ [][Next]_vars
NoStaleAccess == ~staleAccess
NoStaleAnswer == ~staleAnswer
====
