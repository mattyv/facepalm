---- MODULE LookAway ----
\* When facepalm reports that the person looked away (#35): once, when the
\* head has been away from its resting pose for a held time, and not for a
\* nod or a shake, which leave the resting pose only briefly.
\*
\* The head's pose is abstracted against the resting pose ExpressionTracker
\* learns: "rest" within half the threshold, "between" from half the
\* threshold to the threshold, "away" past it. held is whether the head has
\* stayed away for hold_ms. The resting pose learns throughout, so a pose
\* held long enough becomes the resting pose (Absorb): quickly while it is
\* first learned, and over about twenty seconds after, far slower than
\* hold_ms, so a held look away is reported before it is absorbed.
\* MaxMoves and MaxGaps bound how often the head moves and tracking restarts,
\* this model's form of "the head eventually holds a pose".
EXTENDS Naturals
CONSTANTS GuardLearned, GuardHold, GuardOnce, GuardHysteresis, GuardAbsorb,
          MaxMoves, MaxGaps
VARIABLES resting, pose, held, shown, rearmedAt, moves, gaps,
          reported, wasShown
vars == <<resting, pose, held, shown, rearmedAt, moves, gaps, reported,
          wasShown>>

Poses == {"rest", "between", "away"}
TypeOK == /\ resting \in {"learning", "learned"} /\ pose \in Poses
          /\ held \in BOOLEAN /\ shown \in BOOLEAN
          /\ rearmedAt \in {"none"} \cup Poses
          /\ moves \in 0..MaxMoves /\ gaps \in 0..MaxGaps
          /\ reported \in BOOLEAN /\ wasShown \in BOOLEAN

Init == /\ resting = "learning" /\ pose = "rest" /\ held = FALSE
        /\ shown = FALSE /\ rearmedAt = "none" /\ moves = 0 /\ gaps = 0
        /\ reported = FALSE /\ wasShown = FALSE

\* Every action but Report says nothing was reported on this step.
Quiet == reported' = FALSE /\ wasShown' = shown

\* The head moves to another band around the resting pose; any move restarts
\* the hold.
Move(p) == /\ moves < MaxMoves /\ p # pose
           /\ pose' = p /\ held' = FALSE /\ moves' = moves + 1 /\ Quiet
           /\ UNCHANGED <<resting, shown, rearmedAt, gaps>>
\* hold_ms passes with the head away. While the resting pose is being
\* learned there is none to be away from, so no hold runs.
Hold == /\ pose = "away" /\ ~held /\ (resting = "learned" \/ ~GuardLearned)
        /\ held' = TRUE /\ Quiet
        /\ UNCHANGED <<resting, pose, shown, rearmedAt, moves, gaps>>
\* The first three seconds of a track only learn the resting pose.
Learn == /\ resting = "learning"
         /\ resting' = "learned" /\ held' = FALSE /\ Quiet
         /\ UNCHANGED <<pose, shown, rearmedAt, moves, gaps>>
\* A gap of more than two seconds: the resting pose is learned afresh.
Gap == /\ gaps < MaxGaps
       /\ resting' = "learning" /\ held' = FALSE /\ gaps' = gaps + 1 /\ Quiet
       /\ UNCHANGED <<pose, shown, rearmedAt, moves>>
\* The resting pose follows the pose the head holds. While learned, it is too
\* slow to absorb a look away before hold_ms, so only one already reported.
Absorb == /\ GuardAbsorb /\ pose # "rest"
          /\ (pose = "between" \/ shown \/ resting = "learning")
          /\ pose' = "rest" /\ held' = FALSE /\ Quiet
          /\ UNCHANGED <<resting, shown, rearmedAt, moves, gaps>>
\* Looking away is reported, once per look.
Report == /\ pose = "away" /\ (held \/ ~GuardHold) /\ (~shown \/ ~GuardOnce)
          /\ shown' = TRUE /\ reported' = TRUE /\ wasShown' = shown
          /\ UNCHANGED <<resting, pose, held, rearmedAt, moves, gaps>>
\* Back within half the threshold, the next look away can be reported.
Rearm == /\ shown /\ (pose = "rest" \/ (~GuardHysteresis /\ pose # "away"))
         /\ shown' = FALSE /\ rearmedAt' = pose /\ Quiet
         /\ UNCHANGED <<resting, pose, held, moves, gaps>>

Next == \/ \E p \in Poses : Move(p)
        \/ Hold \/ Learn \/ Gap \/ Absorb \/ Report \/ Rearm
Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Hold) /\ WF_vars(Absorb)
        /\ WF_vars(Report) /\ WF_vars(Rearm)

\* Nothing is reported while the resting pose is being learned.
NoReportWhileLearning == reported => resting = "learned"
\* A nod or a shake, away for less than hold_ms, is not reported.
NoReportBeforeHold == reported => held
\* One report per look away.
OncePerLook == reported => ~wasShown
\* The head must come back within half the threshold, so a head hovering
\* at the threshold is not reported over and over.
RearmOnlyAtRest == rearmedAt \in {"none", "rest"}
\* Something must happen: a learned head held away is reported.
LookReported == (resting = "learned" /\ pose = "away")
                    ~> (shown \/ pose # "away" \/ resting = "learning")
\* The suppression after a report is bounded: wherever the head settles, the
\* next look away can be reported (as #62 found for turns).
LookRearms == shown ~> ~shown
====
