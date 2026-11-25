---- MODULE CalibrationPresentation ----
\* One existing CalibrationFlow and its logical presentation intent. A map
\* request is not evidence of compositor placement or physical retirement.
\* The target order is frozen for one attempt. A topology change cancels
\* that attempt and tells the user to restart; indices are never reinterpreted.
EXTENDS Naturals
CONSTANTS GuardRetire, GuardPreserveFlow, GuardTarget, GuardSame,
          GuardIdentity, GuardTopology, GuardGuidance, MaxSteps
VARIABLES phase, step, target, requested, maps, flow, cancel, restart,
          badOverlap, badSame, badLate, badTopology, generation, pending, guidance
vars == <<phase, step, target, requested, maps, flow, cancel, restart,
          badOverlap, badSame, badLate, badTopology, generation, pending, guidance>>

Init ==
  /\ phase = "closed" /\ step = 0 /\ target = 0 /\ requested = 0
  /\ maps = 0 /\ flow = FALSE /\ cancel = FALSE /\ restart = FALSE
  /\ badOverlap = FALSE /\ badSame = FALSE /\ badLate = FALSE
  /\ badTopology = FALSE /\ generation = 0 /\ pending = 0 /\ guidance = "none"

Open ==
  /\ phase = "closed" /\ step = 0
  /\ phase' = "shown" /\ step' = 1 /\ target' = 1 /\ requested' = 1
  /\ maps' = 1 /\ flow' = TRUE /\ generation' = 1
  /\ UNCHANGED <<cancel, restart, badOverlap, badSame, badLate, badTopology, pending, guidance>>

\* A different target retires logical mapping intent before another request.
Advance ==
  /\ phase = "shown" /\ step < MaxSteps
  /\ phase' = "switching" /\ step' = step + 1
  /\ target' = IF target = 1 THEN 2 ELSE 1
  /\ maps' = IF GuardRetire THEN 0 ELSE maps
  /\ flow' = IF GuardPreserveFlow THEN flow ELSE FALSE
  /\ cancel' = IF GuardPreserveFlow THEN cancel ELSE TRUE
  /\ pending' = generation
  /\ UNCHANGED <<requested, restart, badOverlap, badSame, badLate, badTopology, generation, guidance>>

\* Another corner on the same target, or a retry, leaves the mapping alone.
SameTarget ==
  /\ phase = "shown" /\ step < MaxSteps
  /\ step' = step + 1 /\ badSame' = (badSame \/ ~GuardSame)
  /\ phase' = IF GuardSame THEN phase ELSE "switching"
  /\ maps' = IF GuardSame THEN maps ELSE 0
  /\ pending' = IF GuardSame THEN pending ELSE generation
  /\ UNCHANGED <<target, requested, flow, cancel, restart,
                 badOverlap, badLate, badTopology, generation, guidance>>
Retry ==
  /\ phase = "shown" /\ badSame' = (badSame \/ ~GuardSame)
  /\ phase' = IF GuardSame THEN phase ELSE "switching"
  /\ maps' = IF GuardSame THEN maps ELSE 0
  /\ pending' = IF GuardSame THEN pending ELSE generation
  /\ UNCHANGED <<step, target, requested, flow, cancel, restart,
                 badOverlap, badLate, badTopology, generation, guidance>>

Request ==
  /\ pending # 0
  /\ (phase = "switching" \/ (~GuardIdentity /\ phase = "closed"))
  /\ (pending = generation \/ ~GuardIdentity)
  /\ phase' = "shown" /\ requested' = IF GuardTarget THEN target ELSE requested
  /\ maps' = maps + 1 /\ pending' = 0
  /\ badOverlap' = (badOverlap \/ maps > 0)
  /\ badLate' = (badLate \/ phase = "closed")
  /\ UNCHANGED <<step, target, flow, cancel, restart, badSame, badTopology, generation, guidance>>

\* Closing during a switch invalidates its queued presentation request.
Close ==
  /\ phase \in {"shown", "switching"}
  /\ phase' = "closed" /\ maps' = 0 /\ flow' = FALSE /\ cancel' = TRUE
  /\ generation' = 2
  /\ UNCHANGED <<step, target, requested, restart, badOverlap, badSame,
                 badLate, badTopology, pending, guidance>>

\* Guard topology before both hide and setScreen. The flow is canceled; a
\* new attempt must establish a new screen ordering outside this model.
TargetLost ==
  /\ phase \in {"shown", "switching"}
  /\ phase' = IF GuardTopology THEN "lost" ELSE phase
  /\ maps' = IF GuardTopology THEN 0 ELSE maps
  /\ flow' = IF GuardTopology THEN FALSE ELSE flow
  /\ cancel' = IF GuardTopology THEN TRUE ELSE cancel
  /\ restart' = GuardTopology /\ badTopology' = (badTopology \/ ~GuardTopology)
  /\ generation' = IF GuardTopology THEN 2 ELSE generation
  /\ guidance' = IF GuardTopology /\ GuardGuidance THEN "queued" ELSE "none"
  /\ UNCHANGED <<step, target, requested, badOverlap, badSame, badLate, pending>>

ShowGuidance ==
  /\ phase = "lost" /\ guidance = "queued"
  /\ guidance' = "shown"
  /\ UNCHANGED <<phase, step, target, requested, maps, flow, cancel, restart,
                 badOverlap, badSame, badLate, badTopology, generation, pending>>

Next == Open \/ Advance \/ SameTarget \/ Retry \/ Request \/ Close \/ TargetLost \/ ShowGuidance
Spec == Init /\ [][Next]_vars
TypeOK == /\ phase \in {"closed", "shown", "switching", "lost"}
          /\ step \in 0..MaxSteps /\ target \in 0..2 /\ requested \in 0..2
          /\ maps \in 0..MaxSteps /\ flow \in BOOLEAN /\ cancel \in BOOLEAN
          /\ restart \in BOOLEAN /\ generation \in 0..2 /\ pending \in 0..1
          /\ badOverlap \in BOOLEAN /\ badSame \in BOOLEAN /\ badLate \in BOOLEAN
          /\ badTopology \in BOOLEAN /\ guidance \in {"none", "queued", "shown"}
OneMappingIntent == ~badOverlap
NoSameTargetRemap == ~badSame
NoLateReopen == ~badLate
LiveFlow == phase \in {"shown", "switching"} => flow /\ ~cancel
ShownRequestsTarget == phase = "shown" => requested = target
LostRequiresRestart == ~badTopology /\ (phase = "lost" => cancel /\ restart /\ ~flow)
GuidanceScheduled == phase = "lost" => guidance \in {"queued", "shown"}
====
