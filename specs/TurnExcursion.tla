---- MODULE TurnExcursion ----
\* One reported turn per head excursion, and a bound on how long it
\* suppresses the next (#34, #62). Pose is abstracted as near or away from
\* the turn's origin. Time is abstracted as how long the head has held still
\* where it is: held = 0 moving, 1 still for StillMs (750 ms), 2 still for
\* SettledAwayMs (2,000 ms). A head that moves at most MaxMoves times between
\* turns is this model's form of "the head eventually holds still".
EXTENDS Naturals
CONSTANTS GuardReturn, GuardRepeat, GuardHoldAway, GuardSettleAway, MaxMoves
VARIABLES latched, near, held, rearm, moves, reported, wasLatched
vars == <<latched, near, held, rearm, moves, reported, wasLatched>>

Rearms == {"none", "return", "away", "lost"}
TypeOK == /\ latched \in BOOLEAN /\ near \in BOOLEAN /\ held \in 0..2
          /\ rearm \in Rearms /\ moves \in 0..MaxMoves
          /\ reported \in BOOLEAN /\ wasLatched \in BOOLEAN

Init == /\ latched = FALSE /\ near = TRUE /\ held = 0 /\ rearm = "none"
        /\ moves = 0 /\ reported = FALSE /\ wasLatched = FALSE

\* A turn confirms and is reported, unless one is already latched.
Turn == /\ (~latched \/ ~GuardRepeat)
        /\ latched' = TRUE /\ near' = FALSE /\ held' = 0 /\ rearm' = "none"
        /\ moves' = 0 /\ reported' = TRUE /\ wasLatched' = latched
\* The head moves on, away from the origin, or the model reads a gesture.
Away == /\ latched /\ moves < MaxMoves
        /\ near' = FALSE /\ held' = 0 /\ moves' = moves + 1
        /\ reported' = FALSE /\ wasLatched' = latched
        /\ UNCHANGED <<latched, rearm>>
\* The head comes back within ten degrees of the origin.
Return == /\ latched /\ ~near /\ moves < MaxMoves
          /\ near' = TRUE /\ held' = 0 /\ moves' = moves + 1
          /\ reported' = FALSE /\ wasLatched' = latched
          /\ UNCHANGED <<latched, rearm>>
\* Time passes with the model reading none and yaw within ten degrees of
\* where the head settled.
Hold == /\ latched /\ held < 2
        /\ held' = held + 1 /\ reported' = FALSE /\ wasLatched' = latched
        /\ UNCHANGED <<latched, near, rearm, moves>>
\* Back near the origin and still for StillMs: the excursion is over.
Settle == /\ latched /\ held >= 1 /\ (near \/ ~GuardReturn)
          /\ latched' = FALSE /\ rearm' = "return"
          /\ reported' = FALSE /\ wasLatched' = latched
          /\ UNCHANGED <<near, held, moves>>
\* Still away for SettledAwayMs: the pose it settled in is the new origin.
SettleAway == /\ GuardSettleAway /\ latched /\ ~near
              /\ (held = 2 \/ ~GuardHoldAway)
              /\ latched' = FALSE /\ near' = TRUE /\ rearm' = "away"
              /\ reported' = FALSE /\ wasLatched' = latched
              /\ UNCHANGED <<held, moves>>
\* Tracking is lost: GestureReportGate::Reset.
Lost == /\ latched
        /\ latched' = FALSE /\ near' = TRUE /\ held' = 0 /\ rearm' = "lost"
        /\ reported' = FALSE /\ wasLatched' = latched /\ UNCHANGED moves

Next == Turn \/ Away \/ Return \/ Hold \/ Settle \/ SettleAway \/ Lost
\* Time passes, and a head that has stopped moving is judged settled.
Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Hold) /\ WF_vars(Settle \/ SettleAway)

\* A turn is released only once the head has held still for StillMs.
ReleaseNeedsStill == rearm \in {"return", "away"} => held >= 1
\* A release by return happens back near the origin.
NoRearmAway == rearm = "return" => near
\* A release away from the origin needs the long hold, so a glance and its
\* return stay one excursion.
NoRearmOnGlance == rearm = "away" => held = 2
\* No turn is reported while one is latched.
NoRepeat == reported => ~wasLatched
\* Something must happen: every latched turn is released, wherever the head
\* settles, so a later turn can be reported (#62).
TurnRearms == latched ~> ~latched
====
