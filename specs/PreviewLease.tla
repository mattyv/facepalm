---- MODULE PreviewLease ----
\* The camera preview's lease (#61 item 8, #36, #114): the menu may watch
\* the camera only once it explicitly starts the preview, and only while
\* the camera is open from a subscription DaemonAdmission.tla already
\* governs; losing either ends it. No image ever reaches a client over
\* the approved-token service: this models the daemon's own gate on a
\* second, user-only local channel the menu alone connects to (#89's
\* trust boundary, not a new one), never whether to publish at all.
\*
\* #114: the menu's request can outlive the menu that made it -- it quits,
\* crashes, or its session logs out while the preview is open, and only a
\* clean quit ever removes the request file itself (StopPreview). So the
\* request alone is not enough to keep streaming: it must also be fresh.
\* `requested` is the request file's presence; `age` counts how long it
\* has gone without a touch from a live menu (StartPreview, fired again on
\* the menu's own refresh timer while its window stays open, resets it to
\* zero). `menu_alive` is the model's own ghost of whether the menu that
\* made the request still exists: nothing in the daemon ever reads it
\* directly (it has no way to), but it is what the liveness properties
\* below hold the freshness check accountable to. While the menu is
\* alive, its timer keeps `age` at zero (StartPreview is the only action
\* that ever resets it, and it needs `menu_alive`), so a live menu's own
\* request never goes stale on its own; `age` only ever climbs once
\* `menu_alive` is FALSE, and only `Frame` -- the daemon's own per-frame
\* check, the same one that grants and revokes streaming, modeling
\* src/preview_publisher.cc's PreviewPublisher::OnFrame -- advances it,
\* since that check runs only while the camera is open and frames are
\* flowing, the only time a stale request could otherwise still be seen
\* to stream.
\*
\* An earlier revision tracked which camera session (a counter bumped on
\* every CameraOpens) a grant was made for, and compared it against the
\* current one before letting streaming stay on. The #61 item 8 Fable
\* review (#114) found the comparison had no real counterpart in the
\* code: CameraOpened and CameraClosed already force streaming off
\* directly, on every transition, so nothing a later grant's session
\* number could still be stale against ever survives one. The model
\* dropped the counters along with src/preview_lease.cc's own
\* camera_session_/started_session_, rather than inventing a check for
\* code that already worked without it.
\*
\* #129: the file on disk is now modeled too. `frame_exists` is whether
\* PreviewPublisher's own `publish_` has a frame written; `pending_publish`
\* is whether OnFrame has decided to encode one and is still waiting, this
\* same call, to publish it -- which is where the finding lived:
\* PreviewPublisher::OnFrame checked streaming, encoded (which takes real
\* time, unlike anything else here), and only then published, so a
\* Stop/CameraClosed/CameraOpens landing during that encode -- on a
\* different queue, per src/preview_lease.h's own comment on why it needs
\* a mutex at all -- could already have cleared the frame by the time the
\* encode's own publish went on to write it back. `Publish` is OnFrame's
\* publish step, modeled as its own action precisely so CameraOpens,
\* CameraCloses, StopPreview and Frame's own revoke can interleave with
\* it, the way the real control queue can run during a real encode; it is
\* enabled only once `Frame` has committed to one (`pending_publish`), and
\* `Frame` itself cannot start a new one while a previous call's publish
\* is still outstanding, matching OnFrame's own calls being serial on one
\* queue. `GuardPublishChecksStreaming` is `PreviewLease::Publish`'s own
\* re-check, under its mutex, of `streaming` at the moment of the write,
\* rather than the moment the encode began; without it, the write always
\* lands regardless. `GuardStartClearsFrame` is `Engine::Start` clearing
\* any frame a previous crash left behind before the daemon can ever
\* stream again; without it, `Init` allows one already sitting there.
EXTENDS Naturals
CONSTANTS StaleAge, MaxAge,
          GuardStartNeedsCamera, GuardStreamingTracksLease, GuardRequestFresh,
          GuardPublishChecksStreaming, GuardStartClearsFrame
VARIABLES camera, streaming, requested, menu_alive, age, frame_exists,
          pending_publish
vars == <<camera, streaming, requested, menu_alive, age, frame_exists,
          pending_publish>>

Init == /\ camera = "closed" /\ streaming = FALSE
        /\ requested = FALSE /\ menu_alive = TRUE /\ age = 0
        /\ pending_publish = FALSE
        /\ frame_exists \in (IF GuardStartClearsFrame THEN {FALSE} ELSE BOOLEAN)

\* Whether the request is still within the couple of seconds the daemon
\* grants a live menu's own refresh timer to catch up (#114):
\* GuardRequestFresh is the guard that makes this matter at all.
Fresh == age <= StaleAge \/ ~GuardRequestFresh

\* DaemonAdmission.tla's own first-join, taken as given here. Forces
\* streaming off directly, the same as CameraCloses: a stale grant from
\* before this open never carries over, with no session number to check.
\* #129: PreviewLease's own streaming_changed hook clears the frame file
\* synchronously, within this same transition, whenever it turns
\* streaming off -- so frame_exists follows streaming' here too.
CameraOpens == /\ camera = "closed"
               /\ camera' = "open" /\ streaming' = FALSE
               /\ frame_exists' = FALSE
               /\ UNCHANGED <<requested, menu_alive, age, pending_publish>>

\* DaemonAdmission.tla's own last-leave, taken as given here: ends any
\* preview at once, same as the indicator (#37 item 8) stopping with it.
CameraCloses == /\ camera = "open" /\ camera' = "closed"
                /\ streaming' = (IF GuardStreamingTracksLease
                                  THEN FALSE ELSE streaming)
                /\ frame_exists' = (IF streaming' THEN frame_exists ELSE FALSE)
                /\ UNCHANGED <<requested, menu_alive, age, pending_publish>>

\* PreviewLease!Start: the menu's own ask (#108), fired again by its own
\* refresh timer every time its window stays open (#114), so a camera
\* that opens after the first ask still gets granted -- the same
\* repeated enablement the original StartPreview already relied on, now
\* also resetting the request's own freshness. A dead menu asks nothing
\* new. Whether streaming follows depends only on GuardStartNeedsCamera
\* here (freshness is trivially met: age' = 0): a start wrongly let
\* through while the camera is closed really does start streaming with
\* no camera, rather than being caught twice over.
StartPreview == /\ menu_alive
                /\ requested' = TRUE
                /\ age' = 0
                /\ streaming' = (IF GuardStreamingTracksLease
                                  THEN camera = "open" \/ ~GuardStartNeedsCamera
                                  ELSE streaming)
                /\ UNCHANGED <<camera, menu_alive, frame_exists,
                              pending_publish>>

\* PreviewLease!Stop: the menu's own explicit stop -- toggling the menu
\* item off, or its preview window's own close button (#108) -- while it
\* is still alive to do so. A menu that has died (#114) leaves its
\* request behind instead; nothing removes it but Frame noticing it has
\* gone stale. #129: same synchronous clear as CameraOpens/CameraCloses.
StopPreview == /\ requested /\ menu_alive
               /\ requested' = FALSE
               /\ streaming' = (IF GuardStreamingTracksLease
                                 THEN FALSE ELSE streaming)
               /\ frame_exists' = (IF streaming' THEN frame_exists ELSE FALSE)
               /\ UNCHANGED <<camera, menu_alive, age, pending_publish>>

\* The daemon's own per-frame check (#37 item 8, #114),
\* src/preview_publisher.cc's PreviewPublisher::OnFrame: runs only while
\* the camera is open and frames are flowing, which is the only time
\* streaming is ever possibly on, so it is also the only place staleness
\* ever needs to be caught. Grants or keeps granting whenever the
\* request is there and still fresh, revokes otherwise, and -- while the
\* menu that made the request is gone (#114) -- is what ages it towards
\* GuardRequestFresh's own limit, since a dead menu's own timer will
\* never touch it again.
\* #129: ~pending_publish, since OnFrame's calls are serial on one queue
\* -- the next call to it cannot begin while this one is still inside its
\* own encode and publish, so a second Frame can never interleave with
\* the Publish a first one is still waiting on, only CameraOpens,
\* CameraCloses and StopPreview (the control queue's own actions) can.
\* Granting here (streaming' = TRUE) is OnFrame proceeding to encode,
\* decided here but not yet published, hence pending_publish' = streaming':
\* an immediate revoke clears the frame synchronously, the same as
\* CameraOpens/CameraCloses/StopPreview, since lease_.Stop() runs inside
\* this same OnFrame call rather than racing a later one.
Frame == /\ camera = "open" /\ ~pending_publish
         /\ age' = (IF ~menu_alive /\ age < MaxAge THEN age + 1 ELSE age)
         /\ streaming' = (IF GuardStreamingTracksLease THEN requested /\ Fresh'
                           ELSE streaming)
         /\ pending_publish' = streaming'
         /\ frame_exists' = (IF streaming' THEN frame_exists ELSE FALSE)
         /\ UNCHANGED <<camera, requested, menu_alive>>

\* #129: PreviewPublisher::OnFrame's own publish_ call, once its encode
\* (which this model does not need its own step for: nothing but time
\* passes during it) has finished. GuardPublishChecksStreaming is
\* PreviewLease::Publish re-checking `streaming` under its own mutex at
\* the moment of the write, the same mutex CameraOpened/CameraClosed/
\* Start/Stop already take, rather than trusting the decision Frame made
\* when the encode began: with it on, a transition to not-streaming that
\* landed during the encode (CameraOpens, CameraCloses, StopPreview or a
\* later Frame's own revoke, every one of which already cleared
\* frame_exists when it happened) is never undone by this write; with it
\* off, the write lands unconditionally, reproducing #129's own finding.
Publish == /\ pending_publish
           /\ pending_publish' = FALSE
           /\ frame_exists' = (IF GuardPublishChecksStreaming
                                THEN streaming ELSE TRUE)
           /\ UNCHANGED <<camera, streaming, requested, menu_alive, age>>

\* The menu quits without cleaning up, crashes, or the session logs out
\* while the preview is open (#114): irreversible, and asks and removes
\* nothing more, leaving whatever request it last made exactly where it
\* was for Frame to find stale, eventually, on its own.
MenuDies == /\ menu_alive
            /\ menu_alive' = FALSE
            /\ UNCHANGED <<camera, streaming, requested, age, frame_exists,
                          pending_publish>>

Next == CameraOpens \/ CameraCloses \/ StartPreview \/ StopPreview \/ Frame
           \/ Publish \/ MenuDies
\* Frame needs to be fair, not only CameraOpens (#114): it is what both
\* grants a fresh request once the camera opens and ages a dead menu's
\* stale one away, and neither liveness property below can hold if the
\* daemon's own per-frame check is never required to run. Publish needs
\* fairness too (#129): otherwise a pending_publish stuck forever would
\* keep Frame disabled forever with it, the same way.
Spec == Init /\ [][Next]_vars
           /\ WF_vars(CameraOpens) /\ WF_vars(Frame) /\ WF_vars(Publish)

\* The camera can always eventually open: something must happen, so a
\* model that can never open it fails this.
EventuallyOpens == <>(camera = "open")

NeverStreamsWithoutCamera == streaming => camera = "open"
\* #114: the request alone is not enough either -- it must still be
\* fresh. GuardRequestFresh = FALSE does not, by itself, make TLC flag
\* this one broken, since with the guard off `Fresh` itself reads TRUE
\* regardless of `age`, so the broken `Fresh` appears on both sides and
\* the two stay trivially consistent (specs/README.md explains why
\* `MenuDeathEndsStreaming` below is what actually proves the guard
\* matters).
NeverStreamsOnAStaleRequest == streaming => (requested /\ Fresh)

\* #129: the frame file on disk never outlives the preview, whether a
\* crash left one behind before the daemon could clear it
\* (GuardStartClearsFrame) or a publish in flight recreated one after a
\* transition had already cleared it (GuardPublishChecksStreaming).
NeverFrameWithoutStreaming == frame_exists => streaming

\* Liveness (#114): once the menu that made a request is gone, streaming
\* always eventually stops -- and, since nothing but a live menu's own
\* StartPreview can ever turn it back on (menu_alive stays FALSE for
\* good), it stops for good. This is the safety the acceptance bar asks
\* for ("nothing streams without a live menu window"), phrased as a
\* leads-to rather than an instant cutoff, because the daemon can only
\* ever learn of the menu's death indirectly, on its own per-frame check,
\* the couple of seconds GuardRequestFresh's own grace period already
\* allows for.
MenuDeathEndsStreaming == ~menu_alive ~> ~streaming

\* The camera, the request and the menu behind it, all held from some
\* point on, lead to the preview actually streaming (#114): something
\* must happen, so a design that can always find a reason not to grant a
\* live, fresh, open request fails this. `[](<>[]P => <>Q)`, as
\* specs/DaemonAdmission.tla's own JoinOpens uses it, rather than the
\* plain "~>" a momentary ask could satisfy only by chance: a request
\* made and withdrawn in the same instant should not have to lead
\* anywhere, so only a request (and the menu behind it, and the camera)
\* that settles does.
Wanted == requested /\ Fresh /\ menu_alive /\ camera = "open"
PreviewStreamsWhenWanted == [](<>[]Wanted => <>streaming)
====
