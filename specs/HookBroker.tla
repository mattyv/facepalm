---- MODULE HookBroker ----
EXTENDS Naturals
\* One hook request: disconnect cannot send a queued read, and closing the
\* owner does not pretend an already-sent native request has retired.
CONSTANTS GuardPeer, GuardOwner, GuardRetire
VARIABLES owner, peer, phase, native, afterCancel, afterClose
vars == <<owner, peer, phase, native, afterCancel, afterClose>>
Init == /\ owner = "open" /\ peer = TRUE /\ phase = "idle"
        /\ native = FALSE /\ afterCancel = FALSE /\ afterClose = FALSE
Queue == /\ owner = "open" /\ phase = "idle" /\ phase' = "queued"
         /\ UNCHANGED <<owner, peer, native, afterCancel, afterClose>>
Disconnect == /\ peer /\ peer' = FALSE
              /\ UNCHANGED <<owner, phase, native, afterCancel, afterClose>>
Send == /\ phase = "queued" /\ ~native
        /\ (peer \/ ~GuardPeer) /\ (owner = "open" \/ ~GuardOwner)
        /\ phase' = "sent" /\ native' = TRUE
        /\ afterCancel' = (afterCancel \/ ~peer)
        /\ afterClose' = (afterClose \/ (owner = "closed"))
        /\ UNCHANGED <<owner, peer>>
Reply == /\ phase = "sent" /\ native
         /\ native' = FALSE
         /\ phase' = "buffered"
         /\ UNCHANGED <<owner, peer, afterCancel, afterClose>>
Close == /\ owner = "open" /\ owner' = "closed"
         /\ native' = IF GuardRetire THEN native ELSE FALSE
         /\ UNCHANGED <<peer, phase, afterCancel, afterClose>>
Ack == /\ phase = "buffered" /\ peer /\ owner = "open"
       /\ phase' = "delivered"
       /\ UNCHANGED <<owner, peer, native, afterCancel, afterClose>>
Next == Queue \/ Disconnect \/ Send \/ Reply \/ Ack \/ Close
Spec == Init /\ [][Next]_vars
NoCanceledSend == ~afterCancel
NoSendAfterClose == ~afterClose
SentRemainsOwned == phase = "sent" => native
====
