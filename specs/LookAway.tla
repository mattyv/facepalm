---- MODULE LookAway ----
\* When facepalm reports that the person looked away (#35, #141): once per
\* look, when the gaze returns, with its kind and (src/look_away.cc's own
\* duration_ms, not modeled here as a number) how long it lasted.
\*
\* The gaze is abstracted against the resting pose ExpressionTracker
\* learns: "rest" within half of Settings::degrees, "between" up to the
\* full threshold, "away" past it, or "gone" with no face to read at all
\* (#141). held is whether the gaze has been away (or gone) long enough
\* to be reported at all, rather than read as a glance; heldLong is
\* whether it has run long enough past that to read as kAway rather than
\* kAverted; heldFromBetween records whether held started from merely
\* "between", for GuardHysteresis's own check below. otherScreen and
\* sawGone are sticky, set once the gaze is ever read inside another
\* calibrated screen's box, or with no face, this look. Every one of
\* these resets together, in Return, which is #141's own merge of the old
\* model's separate Report (mid-look) and Rearm (once pose next reached
\* "rest"): reporting now happens only once the gaze is truly back, so
\* there is no longer an earlier moment for Report to fire at, nor a
\* later one for Rearm to clear on its own. The resting pose learns
\* throughout (ReturnByAbsorb, this model's Absorb): quickly while it is
\* first learned (Learn), and over about twenty seconds after, so a look
\* is reported through it too, never silently dropped --
\* RestHasNothingPending is what checks that. MaxMoves and MaxGaps bound
\* how often the gaze moves and tracking restarts, this model's form of
\* "the gaze eventually holds a pose".
EXTENDS Naturals
CONSTANTS GuardLearned, GuardHold, GuardHysteresis, GuardAbsorb,
          GuardGoneWins, GuardOtherScreenWins, GuardOtherScreenAway,
          MaxMoves, MaxGaps
VARIABLES resting, pose, held, heldFromBetween, heldLong, otherScreen,
          sawGone, reported, kind, reportedGone, reportedOtherScreen,
          reportedHeld, moves, gaps
vars == <<resting, pose, held, heldFromBetween, heldLong, otherScreen,
          sawGone, reported, kind, reportedGone, reportedOtherScreen,
          reportedHeld, moves, gaps>>

Bands == {"rest", "between", "away", "gone"}
Kinds == {"none", "averted", "other_screen", "away", "gone"}

TypeOK == /\ resting \in {"learning", "learned"} /\ pose \in Bands
          /\ held \in BOOLEAN /\ heldFromBetween \in BOOLEAN
          /\ heldLong \in BOOLEAN
          /\ otherScreen \in BOOLEAN /\ sawGone \in BOOLEAN
          /\ reported \in BOOLEAN /\ kind \in Kinds
          /\ reportedGone \in BOOLEAN /\ reportedOtherScreen \in BOOLEAN
          /\ reportedHeld \in BOOLEAN
          /\ moves \in 0..MaxMoves /\ gaps \in 0..MaxGaps

Init == /\ resting = "learning" /\ pose = "rest"
        /\ held = FALSE /\ heldFromBetween = FALSE /\ heldLong = FALSE
        /\ otherScreen = FALSE /\ sawGone = FALSE
        /\ reported = FALSE /\ kind = "none"
        /\ reportedGone = FALSE /\ reportedOtherScreen = FALSE
        /\ reportedHeld = FALSE
        /\ moves = 0 /\ gaps = 0

\* Every action but a Return says nothing was reported on this step.
Quiet == reported' = FALSE

\* The kind a Return with this look's own sawGone/otherScreen/heldLong
\* reads as, once held (or GuardHold is off) says to report at all:
\* kGone first (leaving beats anything read while still on camera),
\* kOtherScreen next, then kAway or kAverted by heldLong.
KindOf(heldLong_, otherScreen_, sawGone_) ==
    IF sawGone_ /\ GuardGoneWins THEN "gone"
    ELSE IF otherScreen_ /\ GuardOtherScreenWins THEN "other_screen"
    ELSE IF heldLong_ THEN "away"
    ELSE "averted"

\* Return (by ReturnByMove or ReturnByAbsorb, below): the gaze (or the
\* resting pose catching up to it) is read as home. reported' and kind'
\* read this look's own sticky flags before every one of them resets;
\* kind stays whatever it last was when this look is a glance (~reports),
\* the way rearmedAt did in the model this replaces.
ReturnEffect ==
    LET reports == held \/ ~GuardHold
    IN  /\ pose' = "rest"
        /\ reported' = reports
        /\ kind' = IF reports THEN KindOf(heldLong, otherScreen, sawGone)
                   ELSE kind
        /\ reportedGone' = IF reports THEN sawGone ELSE reportedGone
        /\ reportedOtherScreen' =
               IF reports THEN otherScreen ELSE reportedOtherScreen
        /\ reportedHeld' = IF reports THEN held ELSE reportedHeld
        /\ held' = FALSE /\ heldFromBetween' = FALSE /\ heldLong' = FALSE
        /\ otherScreen' = FALSE /\ sawGone' = FALSE

\* The gaze moves to another band short of home: away, between, or gone
\* (no face, #141, from any band -- this is the only way a look ever
\* opens from nothing, so a face lost from the very first frame still
\* counts, LookAwayDetector's own way).
Move(p) == /\ moves < MaxMoves /\ p # pose /\ p \in {"between", "away", "gone"}
           /\ moves' = moves + 1
           /\ pose' = p
           /\ sawGone' = (sawGone \/ (p = "gone"))
           /\ Quiet
           /\ UNCHANGED <<resting, held, heldFromBetween, heldLong,
                          otherScreen, kind, reportedGone,
                          reportedOtherScreen, reportedHeld, gaps>>

\* The gaze returns on its own account: a conscious move all the way home,
\* bounded like any other move.
ReturnByMove == /\ moves < MaxMoves /\ pose # "rest"
                /\ moves' = moves + 1
                /\ ReturnEffect
                /\ UNCHANGED <<resting, gaps>>

\* The resting pose catches up to a gaze that never itself returned
\* (LookAway.tla's old Absorb): safe once the look has been open long
\* enough to plausibly have drifted (held), while it is only ever between
\* (never far enough to need the drift at all), or while it is still being
\* learned afresh. Unlike the old model's own quiet Absorb, this one
\* reports through ReturnEffect exactly as ReturnByMove does: reporting
\* now happens only at Return, so a quiet Absorb here would silently drop
\* a look RestHasNothingPending would otherwise catch (#141's own finding,
\* this issue's "check Absorb still can't swallow an unreported look").
ReturnByAbsorb == /\ GuardAbsorb /\ pose # "rest"
                  /\ (pose = "between" \/ held \/ resting = "learning")
                  /\ ReturnEffect
                  /\ UNCHANGED <<resting, moves, gaps>>

\* hold_ms passes with the gaze away (or gone). While the resting pose is
\* being learned there is none to be away from, so no hold runs.
\* GuardHysteresis's bug: without it, merely "between" counts too, and
\* heldFromBetween records that it did, for NoHoldFromBetween to catch.
Hold == /\ ~held
        /\ pose \in ({"away", "gone"} \cup
                     (IF GuardHysteresis THEN {} ELSE {"between"}))
        /\ (resting = "learned" \/ ~GuardLearned)
        /\ held' = TRUE /\ heldFromBetween' = (pose = "between") /\ Quiet
        /\ UNCHANGED <<resting, pose, heldLong, otherScreen, sawGone, kind,
                       reportedGone, reportedOtherScreen, reportedHeld,
                       moves, gaps>>

\* averted_ms passes too, past hold_ms: kAway rather than kAverted once
\* Return reads it.
HoldLong == /\ held /\ ~heldLong
            /\ heldLong' = TRUE /\ Quiet
            /\ UNCHANGED <<resting, pose, held, heldFromBetween, otherScreen,
                           sawGone, kind, reportedGone, reportedOtherScreen,
                           reportedHeld, moves, gaps>>

\* The gaze is read inside another calibrated screen's box (#141): only
\* while truly away, never merely between or with no face to place it,
\* unless GuardOtherScreenAway is off.
SeeOtherScreen == /\ ~otherScreen /\ (pose = "away" \/ ~GuardOtherScreenAway)
                  /\ otherScreen' = TRUE /\ Quiet
                  /\ UNCHANGED <<resting, pose, held, heldFromBetween,
                                 heldLong, sawGone, kind, reportedGone,
                                 reportedOtherScreen, reportedHeld, moves,
                                 gaps>>

\* The first three seconds of a track only learn the resting pose; a hold
\* that began while learning does not carry over.
Learn == /\ resting = "learning"
         /\ resting' = "learned" /\ held' = FALSE /\ Quiet
         /\ UNCHANGED <<pose, heldFromBetween, heldLong, otherScreen,
                        sawGone, kind, reportedGone, reportedOtherScreen,
                        reportedHeld, moves, gaps>>

\* A gap of more than two seconds: the resting pose is learned afresh, and
\* whatever look was open is cleared with it (LookAwayDetector's own
\* !resting branch): there is no resting pose left to report it against.
Gap == /\ gaps < MaxGaps
       /\ resting' = "learning" /\ held' = FALSE /\ heldFromBetween' = FALSE
       /\ heldLong' = FALSE /\ otherScreen' = FALSE /\ sawGone' = FALSE
       /\ gaps' = gaps + 1 /\ Quiet
       /\ UNCHANGED <<pose, kind, reportedGone, reportedOtherScreen,
                      reportedHeld, moves>>

Next == \/ \E p \in {"between", "away", "gone"} : Move(p)
        \/ Hold \/ HoldLong \/ SeeOtherScreen \/ Learn \/ Gap
        \/ ReturnByMove \/ ReturnByAbsorb
Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Hold) /\ WF_vars(ReturnByAbsorb)

\* Nothing is reported while the resting pose is being learned.
NoReportWhileLearning == reported => resting = "learned"
\* A glance, away for less than hold_ms, is not reported (reportedHeld,
\* not held: Return already resets held' in the very same step as
\* reported', so held itself cannot say what it was read as).
NoReportBeforeHold == reported => reportedHeld
\* Once truly home, nothing from the look that just ended is left running:
\* this is what keeps ReturnByAbsorb (and any other way home) from
\* swallowing a look that was never reported.
RestHasNothingPending == pose = "rest" =>
    (~held /\ ~heldFromBetween /\ ~heldLong /\ ~otherScreen /\ ~sawGone)
\* A hold can only run once the gaze has truly passed the full threshold
\* or gone, never merely from sitting in the gap between the two
\* (GuardHysteresis's own guard).
NoHoldFromBetween == held => ~heldFromBetween
\* Leaving beats anything else read while the look was open.
GoneWinsPriority == (reportedGone /\ reportedHeld) => kind = "gone"
\* Short of leaving, another screen's box beats how long the look ran.
OtherScreenWinsPriority ==
    (reportedOtherScreen /\ reportedHeld /\ ~reportedGone) =>
        kind = "other_screen"
\* Something must happen: a learned gaze held away eventually moves on or
\* needs relearning.
LookReported == (resting = "learned" /\ pose = "away")
                    ~> (pose # "away" \/ resting = "learning")
\* The suppression a look being open carries is bounded: it eventually
\* closes, wherever the gaze (or the resting pose) settles.
LookRearms == held ~> ~held
====