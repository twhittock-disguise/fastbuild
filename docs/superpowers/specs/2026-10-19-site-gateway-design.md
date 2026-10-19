# Site gateway for distributed FBuild over thin links

## Intent

Developers building over a thin uplink (the measured case: about 30 Mbit/s) against the fleet's
datacenter workers spend the whole build uploading the same precompiled headers (PCHs) once per
worker. In the build analysed (fbuild run of 2026-10-16, 24m43s, about 3500 objects, 62 PCHs,
15 workers):

- 5.6 GB of PCH was uploaded (about 350 MB compressed per worker), against about 70 MB of job data.
- The uplink was saturated for the entire build; PCH delivery to every worker finished only at
  1410-1458 s of a 1481 s build.
- Remote concurrency averaged about 165 of roughly 340 available slots; the client compiled 43% of
  the CPU time itself.
- The client queues PCHs for every worker eagerly, and does not re-send a PCH a worker already
  holds within a connection. The waste is replication: the same ~1.08 GB set copied to every
  worker.

A **site gateway** removes the replication. The client sends each PCH and toolchain once, to the
gateway. The gateway fans them out to the site's workers over the datacenter LAN.

## Shape

- The gateway is a new mode of the worker executable (FBuildWorker), run on a machine in the
  datacenter.
- To a developer's FBuild, the gateway is one worker with a large slot count. One connection, one
  address.
- Which workers sit behind a gateway is configured manually, once, on the `d3fleet` side. The
  generated `G:\workers-user.bff` lists the gateway instead of the workers behind it. Workers that
  are not behind a gateway (for example a developer's private worker on a home network) stay listed
  directly and are reached with a regular connection.
- More than one gateway, and a mix of gateways and direct workers in one list, must work.
- A gateway entry may have an ordered list of backup gateways. If none are reachable the generated
  list falls back to listing the workers directly.

## Goals

1. Each PCH and toolchain crosses the developer's uplink once per build, regardless of the number of
   workers behind the gateway.
2. No change to the developer's workflow beyond the generated worker list.
3. The client needs minimal changes: a gateway looks like a worker.
4. A downstream worker failing must not remove the site from the build.
5. Gateways serve several developers at once and share downstream capacity fairly.

## Non-goals

- No peer-to-peer or worker-to-worker PCH fetching.
- No automatic site discovery or bandwidth testing; the grouping is manual configuration.
- No backward compatibility with older client or worker protocol versions. The protocol is bumped
  once from the base; the NodeGraph DB likewise.
- No change to how the client builds its PCH locally, and the binary PCH keeps being shipped
  (decided earlier; worker-side PCH building is rejected).
- No cut-through relaying of PCH chunks in the first version (store-and-forward through disk first).

## Anti-goals

- A gateway that makes the build slower than direct connections when the uplink is fast.
- One flaky downstream worker causing the client to blacklist the whole site.
- A design that needs the graph-bound client dispatch refactored. Reusing the client's job
  machinery inside the worker was investigated and rejected: the client connection class is private
  to `Client.cpp`, takes jobs from the graph's `JobQueue`, and serialises through
  `ObjectNode::SaveRemote`, which dereferences a graph owner.

## Constraints from the code (verified by reading, not by running)

- **Frontend** is the existing `Server`. It decodes the per-connection job stream, creates a `Job`
  and queues it for the local thread pool (`Server.cpp:621-681`). The gateway replaces that last
  step with forwarding. `Server` already runs in-process in tests (`TestDistributed.cpp:519`).
- **Backend** is a trimmed downstream connection built for the gateway, not the full client. The
  reusable pure pieces are `PchSendState`, `PchDispatch::SelectJob`/`SelectPchToQueue`,
  `PrioritySendQueue` and the file-slice send queue item (`Client.cpp:95-113`).
- **Slot count** is a `uint8_t` in the handshake (`Protocol.h:153,161`). Fifteen workers need more
  than 255 slots. A protocol bump using the padding bytes, or a clamp, is required.
- **Job payloads** are one stateful zstd stream per connection (`JobStream.h`), so the gateway must
  decode every upstream chunk and re-encode per downstream connection. The encoder costs about
  70 MiB and the decoder about 64 MiB per connection; a cheaper encoder setting on the LAN side
  is expected.
- **PCH chunks, toolchain files and results** are opaque compressed blobs and pass through
  untouched. The gateway stores the compressed PCH blob and does not unpack it. Downstream workers
  verify it again.
- **Job id** is the first 4 bytes of the decoded job and can be rewritten without a full parse. The
  PCH id is the last field and needs a small peek helper.
- **Results** carry `systemError`; any `systemError` result, unknown-PCH request or send-limit
  breach makes the client blacklist the connection for the rest of the build
  (`Client.cpp:1315-1317`, `888-908`). The gateway must parse results and absorb these itself,
  retrying on another worker.
- **Toolchains**: the gateway stashes the original compressed `MsgFile` payloads and manifest, and
  answers `MsgRequestManifest` and `MsgRequestFile` from the stash.
- **No cancel or heartbeat message exists**, and no job-level timeout was found in the code. A
  hung downstream worker that does not disconnect would hang a job forever, so the gateway needs
  its own watchdog. (The 2026-10-16 logs show `TIMEOUT` results at about 480 s; their source was
  not found in the code.)
- **Thread ids** in results can collide across downstream workers; the gateway rewrites them.
- **Persistence**: the PCH cache, LRU and toolchain inventory logic already persist on disk
  (`Server.cpp:246-247`); a restarted gateway needs no re-upload.

## Related fixes for direct workers (independent of the gateway)

Found in the same analysis; they apply to every direct connection and stay in scope as separate,
smaller changes:

- Gate `QueuePchsToWorker` (`Client.cpp:989-1030`) on the worker having free job capacity. A fast
  private worker (pk-4090 in the data) whose slots were all occupied by hung jobs still received
  60 of 62 PCHs (1.05 GB) and used about 0.26 GB of them.
- Consider sending a PCH to a worker only once it has been assigned a job for it, or when enough
  jobs for it are waiting; pairs of (worker, PCH) with 3 or fewer jobs accounted for 51 pairs and
  about 0.8 GB sent.
- A persistent timestamped worker log (PCH received/evicted, job start/finish). Workers log only
  to console and the worker window today, so a hung worker could not be diagnosed afterwards.

## Prerequisite cleanup: remove the capability mechanism

The capability mechanism exists only on the `pch-distribution` branch (introduced in commit
`9b2b274b`, "Negotiate capabilities at the handshake and refuse other versions with a message";
`origin/main` has none of it), so removing it returns that code to the base's plain protocol
version check, and the one protocol bump from the base stays one bump.

`CAP_PCH_TRANSFORM` goes: the transformed PCH form is always used (`Client.cpp:1167` stops
deriving `allowTransform` from the worker's capabilities; `Protocol.h:48,50` drop the flag; about
13 references, 9 of them in `TestDistributed.cpp`). Because the fleet runs one worker build and
there is no backward compatibility, the same reasoning covers the other three capabilities
(`CAP_PCH_INVENTORY`, `CAP_TOOLCHAIN_INVENTORY`, `CAP_PCH_REQUEST`) and `Protocol::Negotiate`;
the whole capability negotiation goes (confirmed by the user: "just have the protocol version be
the protocol version"). Every feature those flags gated is unconditionally on, and the protocol
version alone says what the peers speak. The gateway is built without any capability handling.

## Order of work (decided with the user)

1. Remove the capability mechanism.
2. The direct-worker fixes (PCH queueing gated on free capacity, PCH sent only when worth it, the
   persistent worker log).
3. The gateway.

Each is a separate change, landed in that order, so the gateway is built on the simpler protocol.

## Decided with the user

- Approach B (forwarding proxy with a Server frontend and a trimmed downstream connection), not
  reuse of the client machinery and not a PCH-only relay.
- Manual, once-only site grouping on the `d3fleet` side; the gateway is the discoverable worker
  for the workers behind it.
- Gateway failure degrades speed, not correctness: ordered backups, then listing the workers
  directly.

## Left to the builder

- Protocol change for slot counts above 255.
- Fair division of downstream capacity between concurrent developers.
- Watchdog values and retry policy.
- Memory sizing of the per-connection encoders and gateway queues, and backpressure design.
- The `d3fleet` config schema and how `workers-user.bff` is generated.
- Extracting the shared "push compressed blobs to a worker" connection (about 300 lines), which
  the gateway's downstream side needs.
- Tests: the in-process `Server` and `HandshakeProbe` helpers (`TestDistributed.cpp:4271-4296`)
  give the base for gateway tests; the estimate is 15 to 25 cases.

## Calls made by the writer (not agreed with the user; change freely)

- **Advertised slots:** the sum of downstream slots at handshake time, using the widened field.
  Over-advertising is acceptable: the gateway queues excess jobs and places them as workers free
  up. No mid-connection slot update message.
- **Capabilities:** none to handle. The single worker executable is deployed across the fleet
  within minutes of publishing, so the gateway and its workers always run the same build and the
  gateway assumes every worker supports everything it does. No capability checks, no dual-form
  storage.
- **Retries:** a worker failure (disconnect, handshake failure, unknown-PCH request, send-limit
  breach, watchdog expiry) retries the job on another worker, at most twice. A `systemError`
  result from a connected worker counts against that worker and retries the same way. After the
  retries the failure is passed to the client as the job's result. A genuine compile failure is an
  ordinary failed result and is passed through unretried.
- **Client reconnect or gateway restart:** per-client-connection state. Jobs in flight on a dropped
  client connection finish downstream and their results are discarded. The persisted PCH and
  toolchain caches are shared across client connections.
- **Fairness:** round-robin per client connection over waiting jobs. No developer identity in the
  handshake.
- **Send limit:** sized for the whole site, not one worker, with the gateway applying its own
  backpressure to the client connection.
- **Acceptance:** no slower than direct connections on a fast uplink in a local benchmark with
  the in-process test harness; on a 30 Mbit/s shaped uplink the PCH bytes crossing the uplink
  equal one copy of the PCH set. Store-and-forward is kept replaceable.

## Size estimate

About 2000 to 2500 new lines for the upstream facade, downstream pool, scheduler (affinity for PCH
and toolchain, retry, watchdog), toolchain stash and worker mode switch; about 200 changed lines in
`Server.cpp` and `Client.cpp`; about 1200 to 1800 lines of tests.

## Known unknowns

- Gateway NIC and CPU limits under real job rates (the 2026-10-16 build moved 1.9 GB of results).
- Concurrent load on one gateway: about 20 developers in total, actual concurrency is hard to
  gauge. Size for several simultaneous builds and degrade fairly rather than fail.
- Whether the per-worker static slot count suits workers whose CPU count changes in idle mode.
- Whether PCH ids repeat across builds. The user states PCH identity has been checked extensively;
  this document treats it as settled.
