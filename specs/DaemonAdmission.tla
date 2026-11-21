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
(* Item 10 of #61 (#52) splits "the open failed" into two reports: no      *)
(* device to use (the existing "openFailed", unchanged: GuardOpenFailStops *)
(* still decides whether that stops the daemon) and everything else Start  *)
(* can fail at, such as a denied permission or a configuration race (the   *)
(* new "openBlocked"), which never stops the daemon and instead retries,   *)
(* fairly, through the new Retry action. `canStart` stands for whatever    *)
(* Start needs that the device and the controller's own state are not     *)
(* (specs/CameraLifecycle.tla's `permitted /\ startWorks`): while it is    *)
(* false, an open attempt can only come back blocked, never succeed, so    *)
(* `Wanted` folds it in the way CameraLifecycle.tla's own `Wanted` does,   *)
(* and `JoinOpens` moves to that spec's `[](<>[]P => <>Q)` shape to say    *)
(* so: the plain "~>" it had before cannot express a cause that the        *)
(* controller itself can clear (`ctl.blocked`, by Retry) the way it could  *)
(* for one only a device reconnecting clears. `ctl.blocked` itself stands  *)
(* for the CameraController's own blocked_.                                *)
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
    GuardOpenFailStops,   \* A camera with no device to use stops the daemon.
    GuardRetries          \* A denied permission or a failed start is retried.

VARIABLES
    sessions,  \* The clients connected to ApprovedFeed.
    subq,      \* Subscription changes on their way to the control queue.
    ctl,       \* CameraController: consent, subscribed, camera state,
               \* device, blocked.
    sq,        \* The session queue's tasks: "open" or "close".
    running,   \* Whether the camera session is capturing.
    reports,   \* The session's reports on their way to the control queue.
    iq,        \* The inference queue's tasks: create, or destroy and say so.
    model,     \* "none" or "live": whether a face model exists.
    dones,     \* Retirements on their way to the control queue: ack or not.
    dc,        \* DaemonControl: stopping, retiring, device back, exited.
    canStart   \* Whatever else Start needs, apart from the device (#52).

vars == <<sessions, subq, ctl, sq, running, reports, iq, model, dones, dc,
          canStart>>

\* "openBlocked" is a denied permission or a failed start (#52); "openFailed"
\* stays no device to use.
Reports == {"opened", "openFailed", "openBlocked", "closed", "lost", "back"}
CamStates == {"closed", "opening", "open", "closing"}

Init ==
    /\ sessions = {} /\ subq = <<>>
    /\ ctl = [consent |-> TRUE, sub |-> FALSE, cam |-> "closed",
              device |-> TRUE, blocked |-> FALSE]
    /\ sq = <<>> /\ running = FALSE /\ reports = <<>>
    /\ iq = <<>> /\ model = "none" /\ dones = <<>>
    /\ dc = [stopping |-> FALSE, retiring |-> FALSE, deviceBack |-> FALSE,
             exited |-> FALSE]
    /\ canStart \in BOOLEAN

-----------------------------------------------------------------------------
(* CameraController's methods, as operators on its state and the session  *)
(* queue: each returns [c |-> controller, q |-> session queue].            *)

\* OpenIfWanted, with GuardOpenNeedsConsent. blocked_ holds it back the same
\* way device does (#52), until Retry clears it.
OpenIf(c, q) ==
    IF c.cam = "closed" /\ (c.consent \/ ~GuardConsent) /\ c.sub /\ c.device
       /\ ~c.blocked
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
\* off, a signal, a failed model, or a camera with no device to use. A
\* second Stop, such as one re-entered from OnOpenFailed, changes nothing.
\* Stopping the service disconnects every client; their subscription
\* changes are then ignored, so they are not queued.
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
    /\ UNCHANGED <<subq, running, reports, model, dones, canStart>>

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
    /\ UNCHANGED <<ctl, sq, running, reports, iq, model, dones, dc, canStart>>

Disconnect(cl) ==
    /\ ~dc.exited /\ cl \in sessions
    /\ Len(subq) < MaxQueue
    /\ sessions' = sessions \ {cl}
    /\ subq' = IF sessions' = {} \/ ~GuardFirstLast
                  THEN Append(subq, FALSE) ELSE subq
    /\ UNCHANGED <<ctl, sq, running, reports, iq, model, dones, dc, canStart>>

\* DaemonControl::Subscription: ignored once stopping.
Subscription ==
    /\ ~dc.exited /\ subq # <<>>
    /\ subq' = Tail(subq)
    /\ IF dc.stopping
          THEN UNCHANGED <<ctl, sq>>
          ELSE LET r == IF Head(subq) THEN Subscribe(ctl, sq)
                                      ELSE Leave(ctl, sq)
               IN ctl' = r.c /\ sq' = r.q
    /\ UNCHANGED <<sessions, running, reports, iq, model, dones, dc, canStart>>

-----------------------------------------------------------------------------
(* The camera session and the OS.                                          *)

\* The session queue's next task. While Start has whatever else it needs
\* (canStart), opening succeeds, or still finds no device to use, the same
\* free choice as before; without it, opening always comes back blocked
\* (#52), never succeeding and never that specific failure, since the
\* device was never the cause.
SessionTask ==
    /\ ~dc.exited /\ sq # <<>>
    /\ sq' = Tail(sq)
    /\ IF Head(sq) = "open"
          THEN IF canStart
                  THEN \/ running' = TRUE
                          /\ reports' = Append(reports, "opened")
                       \/ running' = running
                          /\ reports' = Append(reports, "openFailed")
                  ELSE running' = running
                       /\ reports' = Append(reports, "openBlocked")
          ELSE running' = FALSE /\ reports' = Append(reports, "closed")
    /\ UNCHANGED <<sessions, subq, ctl, iq, model, dones, dc, canStart>>

\* The camera in use goes away, or its session fails, which stops it.
Lost ==
    /\ ~dc.exited /\ Len(reports) < MaxQueue
    /\ running' = FALSE /\ reports' = Append(reports, "lost")
    /\ UNCHANGED <<sessions, subq, ctl, sq, iq, model, dones, dc, canStart>>

\* A camera comes back.
Back ==
    /\ ~dc.exited /\ Len(reports) < MaxQueue
    /\ reports' = Append(reports, "back")
    /\ UNCHANGED <<sessions, subq, ctl, sq, running, iq, model, dones, dc,
                   canStart>>

\* The OS permission changes, or whatever else Start needs starts or stops
\* working, at any time (#52): the person can deny it, and grant it later.
\* Guarded by ~dc.exited for the same reason Lost and Back are: once the
\* daemon has exited, nothing need happen.
ToggleCanStart ==
    /\ ~dc.exited
    /\ canStart' = ~canStart
    /\ UNCHANGED <<sessions, subq, ctl, sq, running, reports, iq, model,
                   dones, dc>>

-----------------------------------------------------------------------------
(* The control queue handles the session's next report.                    *)

\* DaemonControl::OnOpened: only the camera that was opening creates a model.
OnOpened ==
    /\ ctl' = IF ctl.cam = "opening" THEN [ctl EXCEPT !.cam = "open"] ELSE ctl
    /\ iq' = IF ctl.cam = "opening" \/ ~GuardOpenOnce
                THEN Append(iq, [op |-> "create", ack |-> FALSE]) ELSE iq
    /\ UNCHANGED <<sq, dc, sessions>>

\* DaemonControl::OnOpenFailed, for OpenFailure::kNoDevice: a camera that
\* was opening is closed and its device unusable, and the daemon stops, as
\* a failure.
OnOpenFailed ==
    IF ctl.cam = "opening"
       THEN LET c == [ctl EXCEPT !.cam = "closed", !.device = FALSE]
            IN IF GuardOpenFailStops
                  THEN LET s == StopFrom(c, sq, dc, iq)
                       IN /\ ctl' = s.c /\ sq' = s.q /\ dc' = s.d
                          /\ iq' = s.i /\ sessions' = {}
                  ELSE ctl' = c /\ UNCHANGED <<sq, dc, iq, sessions>>
       ELSE UNCHANGED <<ctl, sq, dc, iq, sessions>>

\* DaemonControl::OnOpenFailed, for OpenFailure::kPermissionDenied or
\* kStartFailed (#52): a camera that was opening is closed and blocked, but
\* the daemon never stops for this cause; Retry runs this process's own
\* retry instead.
OnOpenBlocked ==
    IF ctl.cam = "opening"
       THEN ctl' = [ctl EXCEPT !.cam = "closed", !.blocked = TRUE]
            /\ UNCHANGED <<sq, dc, iq, sessions>>
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
         [] Head(reports) = "openBlocked" -> OnOpenBlocked
         [] Head(reports) = "closed" -> OnClosed
         [] Head(reports) = "lost" -> OnDeviceLost
         [] Head(reports) = "back" -> OnDeviceBack
    /\ UNCHANGED <<subq, running, model, dones, canStart>>

\* CameraController::Retry, with GuardRetries (#52): clears blocked_ and
\* tries OpenIfWanted again. Off, a blocked camera stays blocked forever,
\* as today's code without this item would. Unlike OnDeviceBack, this is
\* CameraController's own method, scheduled by itself rather than routed
\* through DaemonControl (src/camera_controller.cc's schedule_retry_), so
\* it has no stopping flag of its own to check: it relies on OpenIf's
\* existing consent check, the same as Subscribe or Leave do, which is
\* exactly why GuardStopRevokes turning this off matters to it too.
\* Guarded by ~dc.exited for the same reason every other action is: once
\* the daemon has exited, nothing need happen.
Retry ==
    /\ ~dc.exited /\ GuardRetries /\ ctl.blocked
    /\ LET r == OpenIf([ctl EXCEPT !.blocked = FALSE], sq)
       IN ctl' = r.c /\ sq' = r.q
    /\ UNCHANGED <<sessions, subq, running, reports, iq, model, dones, dc,
                   canStart>>

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
    /\ UNCHANGED <<sessions, subq, ctl, sq, running, reports, dc, canStart>>

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
    /\ UNCHANGED <<sessions, subq, running, reports, iq, model, canStart>>

-----------------------------------------------------------------------------

Next ==
    \/ Stop \/ Subscription \/ SessionTask \/ Lost \/ Back \/ Report
    \/ InferenceTask \/ Retired \/ Retry \/ ToggleCanStart
    \/ \E cl \in Clients : Join(cl) \/ Disconnect(cl)

\* The queues are fair: each runs its next task in time, and so is a
\* pending retry (#52). The person, the clients and the OS are not.
Spec ==
    /\ Init /\ [][Next]_vars
    /\ WF_vars(Subscription) /\ WF_vars(SessionTask) /\ WF_vars(Report)
    /\ WF_vars(InferenceTask) /\ WF_vars(Retired) /\ WF_vars(Retry)

TypeOK ==
    /\ sessions \subseteq Clients
    /\ subq \in Seq(BOOLEAN)
    /\ ctl \in [consent : BOOLEAN, sub : BOOLEAN, cam : CamStates,
                device : BOOLEAN, blocked : BOOLEAN]
    /\ sq \in Seq({"open", "close"})
    /\ running \in BOOLEAN
    /\ reports \in Seq(Reports)
    /\ iq \in Seq([op : {"create", "destroy"}, ack : BOOLEAN])
    /\ model \in {"none", "live"}
    /\ dones \in Seq(BOOLEAN)
    /\ dc \in [stopping : BOOLEAN, retiring : BOOLEAN, deviceBack : BOOLEAN,
               exited : BOOLEAN]
    /\ canStart \in BOOLEAN

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

\* The camera is wanted, and nothing but a device coming back or a retry
\* stands in the way.
Wanted == sessions # {} /\ ~dc.stopping /\ ctl.device /\ canStart

\* Liveness, and something must happen: from every point, a connected
\* client, the device and whatever else Start needs, all held, lead to the
\* camera being open. The plain "~>" this had before #52 could not say
\* this: `ctl.blocked` is not a device, so `~ctl.device` could not stand
\* in for it, and unlike the device cause, Retry (GuardRetries) clears it
\* on its own rather than needing an outside event, so a single early
\* escape could not cover it either; folding every cause into `Wanted` and
\* asking it to hold from every point, the way
\* specs/CameraLifecycle.tla's `WantedOpens` does, can.
JoinOpens == [](<>[]Wanted => <>(ctl.cam = "open"))
==============================================================================
