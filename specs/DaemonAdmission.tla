---- MODULE DaemonAdmission ---------------------------------------------------
(***************************************************************************)
(* The facepalm daemon's control logic (#37, #63): which approved clients  *)
(* are connected, when the camera opens and closes for them, and how the   *)
(* daemon stops. It follows src/daemon_control.cc and the queues the       *)
(* daemon runs it on, in tools/daemon/main.cc.                             *)
(*                                                                         *)
(* Every approved client shares one camera subscription: ApprovedFeed      *)
(* tells the control queue when the first one joins and when the last one  *)
(* leaves, and every client reads the same reactions from ReactionFeed.    *)
(*                                                                         *)
(* Four serial queues run the work. The control queue owns DaemonControl   *)
(* and CameraController (specs/CameraLifecycle.tla), so each task there is *)
(* one atomic action. It takes subscription changes from ApprovedFeed,     *)
(* reports from the camera session, and the frame pipeline's word that a   *)
(* face model was destroyed. The session queue opens and closes the camera *)
(* (src/session_camera.cc); the inference queue creates and destroys the   *)
(* face model (src/frame_pipeline.cc). Each queue is FIFO; the order       *)
(* between the control queue's three sources is left free, which allows    *)
(* more interleavings than the code has.                                   *)
(*                                                                         *)
(* Each Guard constant is one of the design's protections. A mutation sets *)
(* one to FALSE in a copy of the config, and TLC must then find a          *)
(* counterexample (specs/README.md lists what each breaks).                *)
(***************************************************************************)
EXTENDS Naturals, Sequences

CONSTANTS
    Clients,              \* The clients that may connect.
    Approved,             \* The clients holding an approved token.
    MaxQueue,             \* Bounds what the clients and the OS may queue.
    GuardApproval,        \* Only an approved client joins.
    GuardFirstLast,       \* Only the first join and the last leave count.
    GuardConsent,         \* The camera opens only with consent.
    GuardStopRevokes,     \* Stopping revokes consent, which closes it.
    GuardStopRetires,     \* Stopping with the camera closed retires the model.
    GuardRetireOnce,      \* One retirement at a time.
    GuardAckAfterRetire,  \* A closed camera counts as closed once retired.
    GuardDeferDeviceBack, \* A device coming back waits for the retirement.
    GuardOpenOnce,        \* Only the opening camera's Opened creates a model.
    GuardOpenFailStops    \* A camera that fails to open stops the daemon.

VARIABLES
    sessions,  \* The clients connected to ApprovedFeed.
    subq,      \* Subscription changes on their way to the control queue.
    ctl,       \* CameraController: consent, subscribed, camera state, device.
    sq,        \* The session queue's tasks: "open" or "close".
    running,   \* Whether the camera session is capturing.
    reports,   \* The session's reports on their way to the control queue.
    iq,        \* The inference queue's tasks: create, or destroy and say so.
    model,     \* "none" or "live": whether a face model exists.
    dones,     \* Retirements on their way to the control queue: ack or not.
    dc         \* DaemonControl: stopping, retiring, device back, exited.

vars == <<sessions, subq, ctl, sq, running, reports, iq, model, dones, dc>>

Reports == {"opened", "openFailed", "closed", "lost", "back"}
CamStates == {"closed", "opening", "open", "closing"}

Init ==
    /\ sessions = {} /\ subq = <<>>
    /\ ctl = [consent |-> TRUE, sub |-> FALSE, cam |-> "closed",
              device |-> TRUE]
    /\ sq = <<>> /\ running = FALSE /\ reports = <<>>
    /\ iq = <<>> /\ model = "none" /\ dones = <<>>
    /\ dc = [stopping |-> FALSE, retiring |-> FALSE, deviceBack |-> FALSE,
             exited |-> FALSE]

-----------------------------------------------------------------------------
(* CameraController's methods, as operators on its state and the session  *)
(* queue: each returns [c |-> controller, q |-> session queue].            *)

\* OpenIfWanted, with GuardOpenNeedsConsent.
OpenIf(c, q) ==
    IF c.cam = "closed" /\ (c.consent \/ ~GuardConsent) /\ c.sub /\ c.device
       THEN [c |-> [c EXCEPT !.cam = "opening"], q |-> Append(q, "open")]
       ELSE [c |-> c, q |-> q]

StartClosing(c, q) ==
    IF c.cam \in {"opening", "open"}
       THEN [c |-> [c EXCEPT !.cam = "closing"], q |-> Append(q, "close")]
       ELSE [c |-> c, q |-> q]

Revoke(c, q) ==
    IF c.consent THEN StartClosing([c EXCEPT !.consent = FALSE], q)
                 ELSE [c |-> c, q |-> q]

Subscribe(c, q) == OpenIf([c EXCEPT !.sub = TRUE], q)

Leave(c, q) ==
    IF c.sub THEN StartClosing([c EXCEPT !.sub = FALSE], q)
             ELSE [c |-> c, q |-> q]

Closed(c, q) ==
    IF c.cam = "closing" THEN OpenIf([c EXCEPT !.cam = "closed"], q)
                         ELSE [c |-> c, q |-> q]

DeviceBack(c, q) == OpenIf([c EXCEPT !.device = TRUE], q)

-----------------------------------------------------------------------------
(* DaemonControl's own steps.                                              *)

\* DaemonControl::Retire: the pipeline destroys the model on the inference
\* queue, then says so on the control queue. Returns [d, i].
Retire(d, i, ack) ==
    IF d.retiring /\ GuardRetireOnce
       THEN [d |-> d, i |-> i]
       ELSE [d |-> [d EXCEPT !.retiring = TRUE],
             i |-> Append(i, [op |-> "destroy", ack |-> ack])]

\* DaemonControl::Stop, from the control queue: the person turning Facepalm
\* off, a signal, a failed model, or a camera that failed to open. A second
\* Stop, such as one re-entered from OnOpenFailed, changes nothing. Stopping
\* the service disconnects every client; their subscription changes are
\* then ignored, so they are not queued.
StopFrom(c0, q0, d0, i0) ==
  IF d0.stopping THEN [c |-> c0, q |-> q0, d |-> d0, i |-> i0] ELSE
    LET r == IF GuardStopRevokes THEN Revoke(c0, q0) ELSE [c |-> c0, q |-> q0]
        d1 == [d0 EXCEPT !.stopping = TRUE]
        t == IF r.c.cam = "closed" /\ (~d1.retiring \/ ~GuardRetireOnce)
                /\ GuardStopRetires
                THEN Retire(d1, i0, FALSE) ELSE [d |-> d1, i |-> i0]
    IN [c |-> r.c, q |-> r.q, d |-> t.d, i |-> t.i]

Stop ==
    /\ ~dc.exited /\ ~dc.stopping
    /\ LET s == StopFrom(ctl, sq, dc, iq)
       IN /\ ctl' = s.c /\ sq' = s.q /\ dc' = s.d /\ iq' = s.i
    /\ sessions' = {}
    /\ UNCHANGED <<subq, running, reports, model, dones>>

-----------------------------------------------------------------------------
(* Clients, through Service and ApprovedFeed. They connect only while the  *)
(* service runs, which ends when stopping begins.                          *)

Join(cl) ==
    /\ ~dc.exited /\ ~dc.stopping /\ cl \notin sessions
    /\ (cl \in Approved \/ ~GuardApproval)
    /\ Len(subq) < MaxQueue
    /\ sessions' = sessions \cup {cl}
    /\ subq' = IF sessions = {} \/ ~GuardFirstLast
                  THEN Append(subq, TRUE) ELSE subq
    /\ UNCHANGED <<ctl, sq, running, reports, iq, model, dones, dc>>

Disconnect(cl) ==
    /\ ~dc.exited /\ cl \in sessions
    /\ Len(subq) < MaxQueue
    /\ sessions' = sessions \ {cl}
    /\ subq' = IF sessions' = {} \/ ~GuardFirstLast
                  THEN Append(subq, FALSE) ELSE subq
    /\ UNCHANGED <<ctl, sq, running, reports, iq, model, dones, dc>>

\* DaemonControl::Subscription: ignored once stopping.
Subscription ==
    /\ ~dc.exited /\ subq # <<>>
    /\ subq' = Tail(subq)
    /\ IF dc.stopping
          THEN UNCHANGED <<ctl, sq>>
          ELSE LET r == IF Head(subq) THEN Subscribe(ctl, sq)
                                      ELSE Leave(ctl, sq)
               IN ctl' = r.c /\ sq' = r.q
    /\ UNCHANGED <<sessions, running, reports, iq, model, dones, dc>>

-----------------------------------------------------------------------------
(* The camera session and the OS.                                          *)

\* The session queue's next task. Opening may succeed or fail: the
\* permission is refused, or no camera can be used.
SessionTask ==
    /\ ~dc.exited /\ sq # <<>>
    /\ sq' = Tail(sq)
    /\ IF Head(sq) = "open"
          THEN \/ running' = TRUE /\ reports' = Append(reports, "opened")
               \/ running' = running /\ reports' = Append(reports, "openFailed")
          ELSE running' = FALSE /\ reports' = Append(reports, "closed")
    /\ UNCHANGED <<sessions, subq, ctl, iq, model, dones, dc>>

\* The camera in use goes away, or its session fails, which stops it.
Lost ==
    /\ ~dc.exited /\ Len(reports) < MaxQueue
    /\ running' = FALSE /\ reports' = Append(reports, "lost")
    /\ UNCHANGED <<sessions, subq, ctl, sq, iq, model, dones, dc>>

\* A camera comes back.
Back ==
    /\ ~dc.exited /\ Len(reports) < MaxQueue
    /\ reports' = Append(reports, "back")
    /\ UNCHANGED <<sessions, subq, ctl, sq, running, iq, model, dones, dc>>

-----------------------------------------------------------------------------
(* The control queue handles the session's next report.                    *)

\* DaemonControl::OnOpened: only the camera that was opening creates a model.
OnOpened ==
    /\ ctl' = IF ctl.cam = "opening" THEN [ctl EXCEPT !.cam = "open"] ELSE ctl
    /\ iq' = IF ctl.cam = "opening" \/ ~GuardOpenOnce
                THEN Append(iq, [op |-> "create", ack |-> FALSE]) ELSE iq
    /\ UNCHANGED <<sq, dc, sessions>>

\* DaemonControl::OnOpenFailed: a camera that was opening is closed and its
\* device unusable, and the daemon stops, as a failure.
OnOpenFailed ==
    IF ctl.cam = "opening"
       THEN LET c == [ctl EXCEPT !.cam = "closed", !.device = FALSE]
            IN IF GuardOpenFailStops
                  THEN LET s == StopFrom(c, sq, dc, iq)
                       IN /\ ctl' = s.c /\ sq' = s.q /\ dc' = s.d
                          /\ iq' = s.i /\ sessions' = {}
                  ELSE ctl' = c /\ UNCHANGED <<sq, dc, iq, sessions>>
       ELSE UNCHANGED <<ctl, sq, dc, iq, sessions>>

\* DaemonControl::OnClosed: the model is retired before the controller
\* counts the camera closed, so a reopen waits for it.
OnClosed ==
    IF GuardAckAfterRetire
       THEN LET t == Retire(dc, iq, TRUE)
            IN dc' = t.d /\ iq' = t.i /\ UNCHANGED <<ctl, sq, sessions>>
       ELSE LET r == Closed(ctl, sq)
                t == Retire(dc, iq, FALSE)
            IN /\ ctl' = r.c /\ sq' = r.q /\ dc' = t.d /\ iq' = t.i
               /\ UNCHANGED sessions

\* DaemonControl::OnDeviceLost: an open camera is closed by the loss, and
\* its model retired.
OnDeviceLost ==
    LET c == [ctl EXCEPT !.device = FALSE,
                         !.cam = IF ctl.cam = "open" THEN "closed" ELSE @]
        t == IF c.cam = "closed" THEN Retire(dc, iq, FALSE)
                                 ELSE [d |-> dc, i |-> iq]
    IN /\ ctl' = c /\ dc' = t.d /\ iq' = t.i /\ UNCHANGED <<sq, sessions>>

\* DaemonControl::OnDeviceBack: waits for a retirement under way, and is
\* ignored once stopping.
OnDeviceBack ==
    IF dc.retiring /\ GuardDeferDeviceBack
       THEN dc' = [dc EXCEPT !.deviceBack = TRUE]
            /\ UNCHANGED <<ctl, sq, iq, sessions>>
       ELSE IF ~dc.stopping
          THEN LET r == DeviceBack(ctl, sq)
               IN ctl' = r.c /\ sq' = r.q /\ UNCHANGED <<dc, iq, sessions>>
          ELSE UNCHANGED <<ctl, sq, dc, iq, sessions>>

Report ==
    /\ ~dc.exited /\ reports # <<>>
    /\ reports' = Tail(reports)
    /\ CASE Head(reports) = "opened" -> OnOpened
         [] Head(reports) = "openFailed" -> OnOpenFailed
         [] Head(reports) = "closed" -> OnClosed
         [] Head(reports) = "lost" -> OnDeviceLost
         [] Head(reports) = "back" -> OnDeviceBack
    /\ UNCHANGED <<subq, running, model, dones>>

-----------------------------------------------------------------------------
(* The face model.                                                         *)

\* The inference queue's next task: FramePipeline creates or destroys the
\* face model, and after a destroy says so on the control queue.
InferenceTask ==
    /\ ~dc.exited /\ iq # <<>>
    /\ iq' = Tail(iq)
    /\ IF Head(iq).op = "create"
          THEN model' = "live" /\ UNCHANGED dones
          ELSE model' = "none" /\ dones' = Append(dones, Head(iq).ack)
    /\ UNCHANGED <<sessions, subq, ctl, sq, running, reports, dc>>

\* The retirement's callback on the control queue: the closed camera now
\* counts as closed, then the daemon exits if stopping, or else a device
\* that came back meanwhile is let in.
Retired ==
    /\ ~dc.exited /\ dones # <<>>
    /\ dones' = Tail(dones)
    /\ LET r == IF Head(dones) THEN Closed(ctl, sq) ELSE [c |-> ctl, q |-> sq]
           d == [dc EXCEPT !.retiring = FALSE]
       IN IF d.stopping
             THEN ctl' = r.c /\ sq' = r.q /\ dc' = [d EXCEPT !.exited = TRUE]
             ELSE IF d.deviceBack
                THEN LET b == DeviceBack(r.c, r.q)
                     IN /\ ctl' = b.c /\ sq' = b.q
                        /\ dc' = [d EXCEPT !.deviceBack = FALSE]
                ELSE ctl' = r.c /\ sq' = r.q /\ dc' = d
    /\ UNCHANGED <<sessions, subq, running, reports, iq, model>>

-----------------------------------------------------------------------------

Next ==
    \/ Stop \/ Subscription \/ SessionTask \/ Lost \/ Back \/ Report
    \/ InferenceTask \/ Retired
    \/ \E cl \in Clients : Join(cl) \/ Disconnect(cl)

\* The queues are fair: each runs its next task in time. The person, the
\* clients and the OS are not.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Subscription) /\ WF_vars(SessionTask) /\ WF_vars(Report)
    /\ WF_vars(InferenceTask) /\ WF_vars(Retired)

TypeOK ==
    /\ sessions \subseteq Clients
    /\ subq \in Seq(BOOLEAN)
    /\ ctl \in [consent : BOOLEAN, sub : BOOLEAN, cam : CamStates,
                device : BOOLEAN]
    /\ sq \in Seq({"open", "close"})
    /\ running \in BOOLEAN
    /\ reports \in Seq(Reports)
    /\ iq \in Seq([op : {"create", "destroy"}, ack : BOOLEAN])
    /\ model \in {"none", "live"}
    /\ dones \in Seq(BOOLEAN)
    /\ dc \in [stopping : BOOLEAN, retiring : BOOLEAN, deviceBack : BOOLEAN,
               exited : BOOLEAN]

\* Only approved clients are connected.
ApprovedOnly == sessions \subseteq Approved

\* The camera is opening or open only with consent.
OpenHasConsent == ctl.cam \in {"opening", "open"} => ctl.consent

\* Once the subscription changes are handled, the camera is wanted exactly
\* while some client is connected: one client leaving does not take the
\* camera from another.
SubscriptionMatches ==
    (subq = <<>> /\ ~dc.stopping) => (ctl.sub <=> sessions # {})

\* A camera opens only once the previous face model is destroyed.
ReopenAfterRetire ==
    ctl.cam = "opening" =>
        /\ model = "none" /\ ~dc.retiring
        /\ \A k \in 1..Len(iq) : iq[k].op # "destroy"

\* The daemon exits only with the camera closed and stopped, and every face
\* model destroyed.
ExitClean ==
    dc.exited =>
        /\ ctl.cam = "closed" /\ ~running /\ sq = <<>>
        /\ model = "none" /\ iq = <<>>

\* Liveness: stopping always ends with the daemon exiting.
StopEnds == dc.stopping ~> dc.exited

\* Liveness: with no client left, the camera closes, unless one joins.
LastLeaveCloses ==
    sessions = {} ~> ((ctl.cam = "closed" /\ ~running) \/ sessions # {})

\* Liveness, and something must happen: a connected client gets the camera
\* open, unless every client leaves, the daemon stops, or the device is gone.
JoinOpens ==
    sessions # {} ~>
        (ctl.cam = "open" \/ sessions = {} \/ dc.stopping \/ ~ctl.device)
==============================================================================
