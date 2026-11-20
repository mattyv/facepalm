---- MODULE DaemonAdmission ----
\* One approved session; camera retirement and analyser retirement are distinct.
CONSTANTS GuardConsent, GuardApproval, GuardRetirement
VARIABLES consent, approved, subscribed, camera, model, stopped, openedConsent, openedApproval
vars == <<consent, approved, subscribed, camera, model, stopped, openedConsent, openedApproval>>
Init == /\ consent = FALSE /\ approved = FALSE /\ subscribed = FALSE
        /\ camera = "closed" /\ model = "none" /\ stopped = FALSE
        /\ openedConsent = FALSE /\ openedApproval = FALSE
Grant == /\ ~stopped /\ ~consent /\ consent' = TRUE
         /\ UNCHANGED <<approved, subscribed, camera, model, stopped, openedConsent, openedApproval>>
Connect == /\ ~stopped /\ ~subscribed
           /\ subscribed' = TRUE /\ approved' \in {TRUE, FALSE}
           /\ UNCHANGED <<consent, camera, model, stopped, openedConsent, openedApproval>>
Open == /\ camera = "closed" /\ model = "none" /\ ~stopped /\ subscribed
        /\ (consent \/ ~GuardConsent) /\ (approved \/ ~GuardApproval)
        /\ camera' = "open" /\ model' = "live"
        /\ openedConsent' = consent /\ openedApproval' = approved
        /\ UNCHANGED <<consent, approved, subscribed, stopped>>
Disconnect == /\ subscribed /\ subscribed' = FALSE
              /\ UNCHANGED <<consent, approved, camera, model, stopped, openedConsent, openedApproval>>
Close == /\ camera = "open" /\ ~subscribed
         /\ camera' = "closed" /\ model' = "retiring"
         /\ UNCHANGED <<consent, approved, subscribed, stopped, openedConsent, openedApproval>>
Retire == /\ model = "retiring" /\ model' = "none"
          /\ UNCHANGED <<consent, approved, subscribed, camera, stopped, openedConsent, openedApproval>>
Stop == /\ ~stopped /\ ~subscribed /\ camera = "closed"
        /\ (model = "none" \/ ~GuardRetirement)
        /\ stopped' = TRUE
        /\ UNCHANGED <<consent, approved, subscribed, camera, model, openedConsent, openedApproval>>
Next == Grant \/ Connect \/ Open \/ Disconnect \/ Close \/ Retire \/ Stop
Spec == Init /\ [][Next]_vars
OpenHasConsent == camera = "open" => openedConsent
OpenHasApproval == camera = "open" => openedApproval
StoppedAfterRetirement == stopped => model = "none"
====
