---- MODULE SettingsSocket ----------------------------------------------------
EXTENDS Naturals

CONSTANT GuardDeadline, GuardOneShot
VARIABLES state, calls, ticks
vars == <<state, calls, ticks>>

Init == /\ state = "reading" /\ calls = 0 /\ ticks = 0
ReceiveValid ==
    /\ state = "reading" \/ (~GuardOneShot /\ state = "done")
    /\ state' = "done" /\ calls' = calls + 1 /\ UNCHANGED ticks
ReceiveInvalid == /\ state = "reading" /\ state' = "closed"
                  /\ UNCHANGED <<calls, ticks>>
Tick == /\ state = "reading" /\ ticks < 2 /\ ticks' = ticks + 1
        /\ state' = IF ticks' = 2 /\ GuardDeadline THEN "closed" ELSE state
        /\ UNCHANGED calls
Next == ReceiveValid \/ ReceiveInvalid \/ Tick
Spec == Init /\ [][Next]_vars /\ WF_vars(Tick)
TypeOK == /\ state \in {"reading", "done", "closed"} /\ calls \in Nat
          /\ ticks \in 0..2
AtMostOnce == calls <= 1
ReadSettles == state = "reading" ~> state \in {"done", "closed"}
=============================================================================
