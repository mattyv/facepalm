---- MODULE CameraRetryRetirement --------------------------------------------
EXTENDS Naturals
CONSTANTS GuardHoldUntilRetired, GuardRetry
VARIABLES camera, consent, demand, device, timer, retiring, overlap
vars == <<camera, consent, demand, device, timer, retiring, overlap>>
Init == /\ camera = "open" /\ consent = TRUE /\ demand = TRUE /\ device = TRUE
        /\ timer = FALSE /\ retiring = FALSE /\ overlap = FALSE
Lost == /\ camera = "open"
        /\ camera' = IF GuardHoldUntilRetired THEN "closing" ELSE "closed"
        /\ device' = FALSE /\ retiring' = TRUE
        /\ UNCHANGED <<consent, demand, timer, overlap>>
Retired == /\ retiring /\ retiring' = FALSE
           /\ camera' = IF camera = "closing" THEN "closed" ELSE camera
           /\ UNCHANGED <<consent, demand, device, timer, overlap>>
Leave == /\ demand /\ demand' = FALSE /\ timer' = FALSE
         /\ UNCHANGED <<camera, consent, device, retiring, overlap>>
Join == /\ ~demand /\ demand' = TRUE
        /\ UNCHANGED <<camera, consent, device, timer, retiring, overlap>>
Revoke == /\ consent /\ consent' = FALSE /\ timer' = FALSE
          /\ UNCHANGED <<camera, demand, device, retiring, overlap>>
Grant == /\ ~consent /\ consent' = TRUE
         /\ UNCHANGED <<camera, demand, device, timer, retiring, overlap>>
Arm == /\ GuardRetry /\ camera = "closed" /\ demand /\ consent /\ ~device /\ ~timer
       /\ timer' = TRUE /\ UNCHANGED <<camera, consent, demand, device, retiring, overlap>>
Retry == /\ camera = "closed" /\ timer /\ demand /\ consent
         /\ camera' = "opening" /\ timer' = FALSE /\ device' = TRUE
         /\ overlap' = (overlap \/ retiring)
         /\ UNCHANGED <<consent, demand, retiring>>
Open == /\ camera = "closed" /\ device /\ demand /\ consent
        /\ camera' = "opening" /\ overlap' = (overlap \/ retiring)
        /\ UNCHANGED <<consent, demand, device, timer, retiring>>
Back == IF retiring
           THEN UNCHANGED vars
           ELSE /\ device' = TRUE
                /\ UNCHANGED <<camera, consent, demand, timer, retiring, overlap>>
LostAgain == /\ camera = "closing" /\ UNCHANGED vars
StaleTimer == UNCHANGED vars
Opened == /\ camera = "opening" /\ camera' = "open"
          /\ UNCHANGED <<consent, demand, device, timer, retiring, overlap>>
Next == Lost \/ Retired \/ Leave \/ Join \/ Revoke \/ Grant \/ Arm \/ Retry
        \/ Open \/ Opened \/ Back \/ LostAgain \/ StaleTimer
Spec == Init /\ [][Next]_vars /\ WF_vars(Arm) /\ WF_vars(Retry)
        /\ WF_vars(Open) /\ WF_vars(Opened) /\ WF_vars(Retired)
NoReopenDuringRetirement == ~overlap
Recovery == [](<>[](demand /\ consent /\ ~retiring) => <>(camera = "open"))
=============================================================================
