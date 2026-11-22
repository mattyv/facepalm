---- MODULE McpSocket ----
EXTENDS Naturals
\* One local session, one request at a time. Remote retirement may lag
\* close. Losing the link is not the same as the caller closing the
\* session (#117): link models the live TCP connection, which comes and
\* goes, while state is the caller's own, terminal "closed". A lost link
\* is retried with a backoff bounded by MaxBackoff, counted down by
\* BackoffTick one tick at a time; a closed session never retries.
\*
\* #129: two small corrections to LinkDown, found matching it against
\* mcp/session.js's own disconnect(), and one new piece, the adapter
\* forgetting an interpretation id once the link drops under it.
\*   - disconnect() always bumps this session's own backoff, on every
\*     disconnect, not only a reconnect attempt that goes on to fail:
\*     LinkDown read `backoff' = 0`, which let ReconnectSucceeds or
\*     ReconnectFails fire on the very next step, an immediate redial
\*     code never allows. Corrected to `backoff' = 1`: by the time
\*     LinkDown can fire at all (it needs link = "up", which only
\*     ReconnectSucceeds sets, and that action always resets backoff to
\*     its own minimum first), backoff is always exactly that minimum,
\*     so this is not a guarded design choice, the way GuardBackoffBounded
\*     is, but a plain fact about the code corrected to match it.
\*   - LinkDown read `remoteWaiting' = FALSE`, asserting the remote side's
\*     own wait ends the instant the local link drops. Nothing in
\*     session.js tells the remote side anything when a link merely
\*     drops (disconnect() only ever tears down the local socket), so
\*     this was never true; Cancel already leaves remoteWaiting
\*     unchanged, for the same reason (its own comment: the remote side's
\*     "native wait... is still running" and "eventually retires on its
\*     own"), and LinkDown now does too.
\*   - `knownId`, `generation`, `learnedGeneration` and `stale` model
\*     mcp/session.js's own noteInterpretation()/knowsInterpretation(),
\*     added for the same reason given ids can repeat: with
\*     next_interpretation_ (src/reaction_feed.cc) restarting at 1 on
\*     every daemon run, an id this session was told about before a lost
\*     link could mean a different beat once reconnected.
\*     GuardForgetsIdsOnLinkDown is disconnect()'s own
\*     knownInterpretations.clear(); `generation`, bumped by every real
\*     LinkDown regardless of the guard, is the model's own ghost of how
\*     many times the link has actually dropped since an id was learned,
\*     independent of whether the fix remembers to forget it.
CONSTANTS GuardClosed, GuardSingle, GuardBackoffBounded,
          GuardForgetsIdsOnLinkDown, MaxBackoff, MaxGeneration
VARIABLES state, link, backoff, active, sentAfterClose, remoteWaiting,
          knownId, generation, learnedGeneration, stale
vars == <<state, link, backoff, active, sentAfterClose, remoteWaiting,
          knownId, generation, learnedGeneration, stale>>

States == {"open", "closed"}
Links == {"up", "down"}

TypeOK == /\ state \in States /\ link \in Links /\ backoff \in 0..MaxBackoff
          /\ active \in 0..2 /\ sentAfterClose \in BOOLEAN
          /\ remoteWaiting \in BOOLEAN
          /\ knownId \in BOOLEAN /\ stale \in BOOLEAN
          /\ generation \in 0..MaxGeneration
          /\ learnedGeneration \in 0..MaxGeneration

\* The link starts down: a session dials only once something asks it to.
Init == /\ state = "open" /\ link = "down" /\ backoff = 0 /\ active = 0
        /\ sentAfterClose = FALSE /\ remoteWaiting = FALSE
        /\ knownId = FALSE /\ stale = FALSE
        /\ generation = 0 /\ learnedGeneration = 0

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
        /\ UNCHANGED <<state, link, backoff, knownId, generation,
                      learnedGeneration, stale>>

Reply == /\ state = "open" /\ link = "up" /\ active > 0
         /\ active' = active - 1 /\ remoteWaiting' = FALSE
         /\ UNCHANGED <<state, link, backoff, sentAfterClose, knownId,
                       generation, learnedGeneration, stale>>

\* The caller closes this session for good (closed is terminal, as before
\* this change): no Reconnect follows.
Cancel == /\ state = "open" /\ active > 0
          /\ state' = "closed" /\ active' = 0 /\ link' = "down"
          /\ backoff' = 0
          /\ UNCHANGED <<sentAfterClose, remoteWaiting, knownId, generation,
                        learnedGeneration, stale>>

\* The link drops on its own (the daemon turned off, or the TCP connection
\* failed): not terminal, unlike Cancel. Every in-flight exchange ends
\* unanswered, and the remote side is gone with it -- but not its own
\* native wait, which Cancel's own RemoteRetires already does not end
\* either (#129, see the module comment on both corrections below).
LinkDown == /\ state = "open" /\ link = "up"
            /\ link' = "down" /\ active' = 0 /\ backoff' = 1
            /\ generation' = (IF generation < MaxGeneration
                               THEN generation + 1 ELSE generation)
            /\ knownId' = (IF GuardForgetsIdsOnLinkDown THEN FALSE ELSE knownId)
            /\ UNCHANGED <<state, sentAfterClose, remoteWaiting,
                          learnedGeneration, stale>>

\* A reconnect attempt, only once backoff has run out and only while this
\* session was never itself closed: succeeds, bringing the link back up.
ReconnectSucceeds == /\ state = "open" /\ link = "down" /\ backoff = 0
                     /\ link' = "up" /\ backoff' = 0
                     /\ UNCHANGED <<state, active, sentAfterClose,
                                   remoteWaiting, knownId, generation,
                                   learnedGeneration, stale>>

\* As ReconnectSucceeds, but fails and sets a new backoff before the next
\* attempt may run, bounded by MaxBackoff either way (GuardBackoffBounded).
ReconnectFails == /\ state = "open" /\ link = "down" /\ backoff = 0
                  /\ \E next \in 1..MaxBackoff :
                       backoff' = IF GuardBackoffBounded
                                  THEN next
                                  ELSE MaxBackoff + next
                  /\ UNCHANGED <<state, link, active, sentAfterClose,
                                remoteWaiting, knownId, generation,
                                learnedGeneration, stale>>

\* Backoff counts down on its own towards the next attempt.
BackoffTick == /\ backoff > 0 /\ backoff' = backoff - 1
               /\ UNCHANGED <<state, link, active, sentAfterClose,
                             remoteWaiting, knownId, generation,
                             learnedGeneration, stale>>

\* Cancelling may close the local socket while the native wait on the
\* remote side is still running; it eventually retires on its own.
RemoteRetires == /\ state = "closed" /\ remoteWaiting
                 /\ remoteWaiting' = FALSE
                 /\ UNCHANGED <<state, link, backoff, active, sentAfterClose,
                               knownId, generation, learnedGeneration, stale>>

\* mcp/server.js's wait_for_reaction, through session.js's own
\* noteInterpretation() (#129): a delivery tells the caller about one
\* interpretation id while the link is up, remembered at this moment's
\* own generation.
Learn == /\ link = "up" /\ ~knownId
         /\ knownId' = TRUE /\ learnedGeneration' = generation
         /\ UNCHANGED <<state, link, backoff, active, sentAfterClose,
                       remoteWaiting, generation, stale>>

\* tell_feeling or ask_feeling, through session.js's own
\* knowsInterpretation() (#129): sends a feeling back for the id only
\* while this session still considers it known -- the adapter's own
\* local gate, regardless of whether the id would also be refused on the
\* wire. `stale` is a ghost: TRUE once this ever happens for an id
\* learned at an earlier generation than the current one, i.e. the link
\* has genuinely dropped at least once since -- the thing the gate
\* exists to prevent, independent of whether GuardForgetsIdsOnLinkDown
\* actually kept knownId in step with that.
Tell == /\ knownId
        /\ stale' = (stale \/ learnedGeneration < generation)
        /\ UNCHANGED <<state, link, backoff, active, sentAfterClose,
                      remoteWaiting, knownId, generation, learnedGeneration>>

Next == Send \/ Reply \/ Cancel \/ LinkDown \/ ReconnectSucceeds
           \/ ReconnectFails \/ BackoffTick \/ RemoteRetires \/ Learn \/ Tell

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
\* #129: a feeling is never sent back for an id learned before the link
\* last dropped, so one daemon run's beat can never train on another's.
NoStaleTell == ~stale
====
