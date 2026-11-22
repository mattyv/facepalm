---- MODULE McpSocket ----
EXTENDS Naturals
\* One local session, one request at a time. Remote retirement may lag
\* close. Losing the link is not the same as the caller closing the
\* session (#117): link models the live TCP connection, which comes and
\* goes, while state is the caller's own, terminal "closed". A lost link
\* is retried with a backoff bounded by MaxBackoff, counted down by
\* BackoffTick one tick at a time; a closed session never retries.
CONSTANTS GuardClosed, GuardSingle, GuardBackoffBounded, MaxBackoff
VARIABLES state, link, backoff, active, sentAfterClose, remoteWaiting
vars == <<state, link, backoff, active, sentAfterClose, remoteWaiting>>

States == {"open", "closed"}
Links == {"up", "down"}

TypeOK == /\ state \in States /\ link \in Links /\ backoff \in 0..MaxBackoff
          /\ active \in 0..2 /\ sentAfterClose \in BOOLEAN
          /\ remoteWaiting \in BOOLEAN

\* The link starts down: a session dials only once something asks it to.
Init == /\ state = "open" /\ link = "down" /\ backoff = 0 /\ active = 0
        /\ sentAfterClose = FALSE /\ remoteWaiting = FALSE

\* A request reaches the wire: needs a session not yet closed and a link
\* that is actually up (a request made while down waits on a reconnect, or
\* is refused, neither of which is this action). sentAfterClose, despite
\* its name, also catches one sent while the link is down with the guard
\* off, since a request has no business reaching the wire either way.
Send == /\ (state = "open" \/ ~GuardClosed) /\ (link = "up" \/ ~GuardClosed)
        /\ (active = 0 \/ ~GuardSingle) /\ active < 2
        /\ active' = active + 1
        /\ sentAfterClose' =
             (sentAfterClose \/ state = "closed" \/ link = "down")
        /\ remoteWaiting' = TRUE
        /\ UNCHANGED <<state, link, backoff>>

Reply == /\ state = "open" /\ link = "up" /\ active > 0
         /\ active' = active - 1 /\ remoteWaiting' = FALSE
         /\ UNCHANGED <<state, link, backoff, sentAfterClose>>

\* The caller closes this session for good (closed is terminal, as before
\* this change): no Reconnect follows.
Cancel == /\ state = "open" /\ active > 0
          /\ state' = "closed" /\ active' = 0 /\ link' = "down"
          /\ backoff' = 0
          /\ UNCHANGED <<sentAfterClose, remoteWaiting>>

\* The link drops on its own (the daemon turned off, or the TCP connection
\* failed): not terminal, unlike Cancel. Every in-flight exchange ends
\* unanswered, and the remote side is gone with it, unlike Cancel's own
\* RemoteRetires below.
LinkDown == /\ state = "open" /\ link = "up"
            /\ link' = "down" /\ active' = 0 /\ backoff' = 0
            /\ remoteWaiting' = FALSE
            /\ UNCHANGED <<state, sentAfterClose>>

\* A reconnect attempt, only once backoff has run out and only while this
\* session was never itself closed: succeeds, bringing the link back up.
ReconnectSucceeds == /\ state = "open" /\ link = "down" /\ backoff = 0
                     /\ link' = "up" /\ backoff' = 0
                     /\ UNCHANGED <<state, active, sentAfterClose,
                                   remoteWaiting>>

\* As ReconnectSucceeds, but fails and sets a new backoff before the next
\* attempt may run, bounded by MaxBackoff either way (GuardBackoffBounded).
ReconnectFails == /\ state = "open" /\ link = "down" /\ backoff = 0
                  /\ \E next \in 1..MaxBackoff :
                       backoff' = IF GuardBackoffBounded
                                  THEN next
                                  ELSE MaxBackoff + next
                  /\ UNCHANGED <<state, link, active, sentAfterClose,
                                remoteWaiting>>

\* Backoff counts down on its own towards the next attempt.
BackoffTick == /\ backoff > 0 /\ backoff' = backoff - 1
               /\ UNCHANGED <<state, link, active, sentAfterClose,
                             remoteWaiting>>

\* Cancelling may close the local socket while the native wait on the
\* remote side is still running; it eventually retires on its own.
RemoteRetires == /\ state = "closed" /\ remoteWaiting
                 /\ remoteWaiting' = FALSE
                 /\ UNCHANGED <<state, link, backoff, active, sentAfterClose>>

Next == Send \/ Reply \/ Cancel \/ LinkDown \/ ReconnectSucceeds
           \/ ReconnectFails \/ BackoffTick \/ RemoteRetires

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(BackoffTick) /\ SF_vars(ReconnectSucceeds)

NoSendAfterClose == ~sentAfterClose
OneRequest == active <= 1
BackoffBounded == backoff <= MaxBackoff
\* Something must happen: a link that goes down, and a session that was
\* never itself closed, always comes back up (or the session closes
\* first, giving up the right to retry at all).
LinkRecovers == (link = "down" /\ state = "open")
                   ~> (link = "up" \/ state = "closed")
====
