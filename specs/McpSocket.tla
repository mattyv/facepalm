---- MODULE McpSocket ----
EXTENDS Naturals
\* One local session, one request at a time. Remote retirement may lag close.
CONSTANTS GuardClosed, GuardSingle
VARIABLES state, active, sentAfterClose, remoteWaiting
vars == <<state, active, sentAfterClose, remoteWaiting>>
Init == /\ state = "open" /\ active = 0
        /\ sentAfterClose = FALSE /\ remoteWaiting = FALSE
Send == /\ (state = "open" \/ ~GuardClosed)
        /\ (active = 0 \/ ~GuardSingle) /\ active < 2
        /\ active' = active + 1
        /\ sentAfterClose' = (sentAfterClose \/ (state = "closed"))
        /\ remoteWaiting' = TRUE /\ UNCHANGED state
Reply == /\ state = "open" /\ active > 0
         /\ active' = active - 1 /\ remoteWaiting' = FALSE
         /\ UNCHANGED <<state, sentAfterClose>>
Cancel == /\ state = "open" /\ active > 0
          /\ state' = "closed" /\ active' = 0
          /\ UNCHANGED <<sentAfterClose, remoteWaiting>>
RemoteRetires == /\ state = "closed" /\ remoteWaiting
                 /\ remoteWaiting' = FALSE
                 /\ UNCHANGED <<state, active, sentAfterClose>>
Next == Send \/ Reply \/ Cancel \/ RemoteRetires
Spec == Init /\ [][Next]_vars
NoSendAfterClose == ~sentAfterClose
OneRequest == active <= 1
====
