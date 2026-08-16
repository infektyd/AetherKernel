# AetherKernel — Milestone 4 design: async/await on bare metal (cooperative executor)

**Status:** ✅ IMPLEMENTED. Stage 2/3 were hardware-verified 2026-06-04; Runtime V2 shared-CNTP
timer arbitration, Runtime V3 UART shell/control plane, Runtime V4 IRQ-backed UART RX, and
Runtime V5 diagnostics shell were hardware-verified 2026-06-05.
Runtime V6 retained panic/fault records were hardware-verified 2026-06-05.
Runtime V7 memory map and frame allocator invariants were hardware-verified 2026-06-05.
Runtime V8 allocator/frame guardrails were hardware-verified 2026-06-05.
Runtime V9 bounded pressure tests, Runtime V10 guard probes, and Runtime V11 boot/soak
invariants were hardware-verified 2026-06-05. Runtime V12 adds a fixed C-owned
kernel object table and cooperative task registry and was hardware-verified 2026-06-05.
Runtime V13 adds bounded mailbox queues and was hardware-verified 2026-06-05.
Runtime V14 adds deterministic task supervision and was hardware-verified 2026-06-05.
Runtime V15 adds capability-tagged kernel object handles and was hardware-verified
2026-06-05. Runtime V16 adds a fixed event log ring and was hardware-verified
2026-06-05. Runtime V17 adds a deterministic boot certificate and was
hardware-verified 2026-06-05. Runtime V18 adds cooperative cancellation tokens
and was hardware-verified 2026-06-05. Runtime V19 adds structured Aether task
spawn metadata and was hardware-verified 2026-06-05. Runtime V20 adds bounded
Swift-facing async channels over fixed mailbox queues and was hardware-verified
2026-06-05. Runtime V21 adds read-only MMU ownership introspection and was
hardware-verified 2026-06-05. Runtime V22 adds guarded typed pools beside the
heap and was hardware-verified 2026-06-05. Runtime V23 adds allocator/pool
pressure telemetry and was hardware-verified 2026-06-06. Runtime V24 adds a
fixed driver registry and was hardware-verified 2026-06-06. Runtime V25 adds
scriptable command protocol v2 and was hardware-verified 2026-06-06. Runtime
V26 host soak harness was hardware-verified 2026-06-06 as a host-side repeated
proof loop. Runtime V27 panic taxonomy and symbolic retained records were
hardware-verified 2026-06-06. Runtime V28 Swift runtime dependency audit was
hardware-verified 2026-06-06. Runtime V29 agent-oriented control session was
hardware-verified 2026-06-06. Runtime V30 Swift-native kernel substrate
certificate was hardware-verified 2026-06-06. Runtime V31 preemptive scheduler
substrate was hardware-verified 2026-06-06. Runtime V32 SMP secondary-core
bring-up was hardware-verified 2026-06-06. Runtime V33 atomics, spinlocks, and
per-core run queues were hardware-verified 2026-06-06. Runtime V34 timer-driven
SMP scheduler dispatch was hardware-verified 2026-06-06. Runtime V35
secondary-owned scheduler workers were hardware-verified 2026-06-06. Runtime
V36 timer-fed secondary scheduler workers were hardware-verified 2026-06-06.
Runtime V37 timer-fed secondary C scheduler jobs was hardware-verified
2026-06-06. Runtime V38 secondary scheduler wake protocol was
hardware-verified 2026-06-06. Runtime V39 secondary scheduler handoff protocol
was hardware-verified 2026-06-06. Runtime V40 scheduler backpressure protocol
was hardware-verified 2026-06-06. Runtime V41 secondary scheduler
work-stealing protocol was hardware-verified 2026-06-06. Runtime V42 secondary
scheduler load-balancing protocol was hardware-verified 2026-06-06. Runtime V43
secondary scheduler priority/preemption protocol was hardware-verified 2026-06-07.
Runtime V44 bounded SMP concurrency soak protocol was hardware-verified 2026-06-07.

Current live UART shell strings on the shipped build: `bootcert ok=1 version=66 ...`,
`certificate ok=1 version=63 ...`, and `sched12 ok=1 version=44 ...` (sched12 keeps
the V44 concurrency-soak feature version). Current cold-boot serial floor (after
`xhci ok=1 version=63`) adds boot-only markers: `runtime v64: xHCI controller init`,
`xhci_run ok=1 version=64`, `runtime v65: USB device enumeration`,
`usb_enum ok=[01] version=65`, `runtime v66: HID boot-protocol keyboard`, and
`kbd ok=[01] version=66` (`usb_enum`/`kbd` may report `ok=0` on unattended netboot
when no USB device or keyboard is attached).

> ## Runtime V44 bounded SMP concurrency soak protocol ground truth (2026-06-07)
> V44 keeps Swift execution on core 0 and proves three bounded concurrency soak
> rounds while SMP dispatch and timer-fed secondary workers stay active. Each round
> feeds worker tokens into secondary queues, signals secondary cores, and requires
> every queue to drain back to zero before the next round starts. The live Pi proof
> used `kernel8.img` sha256
> `da94ea815b600951a82fc6ca46c23b679fb3362869f8a49aed1ceb00cd88a2cd` and passed
> Wemo cold-cycle `netboot-auto.sh` plus a clean 3-cycle `soak-loop.sh` repeat.
> Proof lines included `runtime v44: bounded smp concurrency soak`,
> `bootcert ok=1 version=44 concurrency=1 priority=1 fairness=1 stealing=1
> backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1
> ... events_lost=0`, `certificate ok=1 version=44 substrate=1 bootcert=1
> concurrency=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
> `sched12 ok=1 version=44 concurrency=1 rounds=3 completions=3 failures=0
> dispatches=9 soak_core1=3 soak_core2=3 soak_core3=3 selftest=1`.

> ## Runtime V43 secondary scheduler priority/preemption protocol ground truth (2026-06-07)
> V43 keeps Swift execution on core 0 and adds bounded high-priority scheduler
> lanes on secondary cores. High-priority tokens use lane bit 12, secondary
> workers preempt/yield through the existing per-core runqueue locks, and the
> selftest quiesces timer SMP dispatch while proving preempt/yield counters and
> draining every queue back to zero. The live Pi proof used `kernel8.img` sha256
> `d2ddea45690c0b6180ab92c61dbc5ba26a9a01eb2d272ad3ae860f14a01b6610` and
> passed a normal `net-iterate.sh` run after power-cycle recovery. Proof lines
> included `runtime v43: secondary scheduler priority preemption`, `bootcert ok=1 version=43 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=43 substrate=1 bootcert=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched11 ok=1 version=43 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 preemptions=2 yields=2 completions=4 total=0 capacity=8 low_core1=2 high_core1=2 preempt_core1=2 yield_core1=2 selftest=1`.

> ## Runtime V42 secondary scheduler load-balancing protocol ground truth (2026-06-06)
> V42 keeps Swift execution on core 0 and lets underloaded C-only secondary
> scheduler workers pull bounded balance-job tokens from an overloaded peer queue
> through the existing per-core runqueue locks. The overloaded source core cannot
> execute balance tokens locally; only another secondary can balance, execute,
> record destination/source counters, and drain every queue back to zero. The
> live Pi proof used `kernel8.img` sha256
> `a480b4c4e5df7ee114dc63bb0c17edf2dedaddd5a2f00033f5b8d96f527e2b97` and
> passed a normal `net-iterate.sh` run plus a clean 3-cycle repeat. Proof lines
> included `runtime v42: secondary scheduler load balancing`, `bootcert ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=42 substrate=1 bootcert=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched10 ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 balances=3 completions=3 total=0 capacity=8 source_core1=3 source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=1 queue_imbalance=0 selftest=1`.

> ## Runtime V41 secondary scheduler work-stealing protocol ground truth (2026-06-06)
> V41 keeps Swift execution on core 0 and lets idle C-only secondary scheduler
> workers steal bounded steal-job tokens from another secondary queue through
> the existing per-core runqueue locks. The source core cannot execute steal
> tokens locally; only another secondary can steal, execute, record the steal
> destination/source counters, and drain every queue back to zero. The live Pi
> proof used `kernel8.img` sha256
> `525a237533d0d1dbe0782f8c9a9d1c03a839676fc1cf4119b2625a41df0d78e0` and
> passed a normal `net-iterate.sh` run plus a clean 3-cycle repeat. Proof lines
> included `runtime v41: secondary scheduler work stealing`, `bootcert ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=41 substrate=1 bootcert=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched9 ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1 steals=4 completions=4 total=0 capacity=8 source_core1=4 source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=2 selftest=1`.

> ## Runtime V40 scheduler backpressure protocol ground truth (2026-06-06)
> V40 keeps Swift execution on core 0, keeps secondary cores in C-only loops,
> and proves the SMP scheduler's fixed per-core queues fail closed under
> saturation. The selftest fills each bounded runqueue to capacity, verifies one
> extra enqueue is rejected and recorded as overflow, then drains every queue
> back to zero. The live Pi proof used `kernel8.img` sha256
> `46bc501a6513cf8a2187203c216be0dd6e54cf49223db3547f4d01578ca78372` and
> passed a normal `net-iterate.sh` run plus a clean 3-cycle repeat. Proof lines
> included `runtime v40: scheduler backpressure protocol`, `bootcert ok=1 version=40
> backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1
> secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=40
> substrate=1 bootcert=1 backpressure=1 handoff=1 wake=1 job_exec=1
> worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched8 ok=1 version=40
> backpressure=1 handoff=1 wake=1 high_water=8 overflows=16 total=0
> capacity=8 core0_high=8 core1_high=8 core2_high=8 core3_high=8
> core0_overflow=4 core1_overflow=4 core2_overflow=4 core3_overflow=4
> selftest=1`. The repeat kept `resp id=41 ok=1 cmd=sched8 end`,
> `sched7 ok=1`, `runqueues total=0`, and `events_lost=0`.

> ## Runtime V39 secondary scheduler handoff protocol ground truth (2026-06-06)
> V39 keeps Swift execution on core 0 and records bounded cross-core scheduler
> handoffs for timer-fed C-only jobs: issue counters increment when core 0
> enqueues secondary jobs, and completion counters increment when cores 1-3
> finish those jobs in their C-only scheduler loops. The live Pi proof used
> `kernel8.img` sha256
> `f24f26c85da4058853e5c7ec4af1822b7a77545259e492786c233421a45b831f`
> and passed a normal `net-iterate.sh` run plus a clean 3-cycle repeat. Proof
> lines included `runtime v39: secondary scheduler handoff protocol`,
> `bootcert ok=1 version=39 handoff=1 wake=1 job_exec=1 worker_feed=1
> secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`,
> `certificate ok=1 version=39 substrate=1 bootcert=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
> `sched7 ok=1 version=39 handoff=1 wake=1 job_exec=1 issued=885
> completed=885 gap=0 imbalance=0 core0_issue=0 core1_issue=295
> core2_issue=295 core3_issue=295 core0_done=0 core1_done=295
> core2_done=295 core3_done=295 selftest=1`. The repeat kept
> `resp id=40 ok=1 cmd=sched7 end`, `runqueues total=0`, and `events_lost=0`.

> ## Runtime V38 secondary scheduler wake protocol ground truth (2026-06-06)
> V38 keeps Swift execution on core 0, parks the C-only secondary scheduler
> loops with WFE between ticks, and emits bounded SEV wake signals when core 0
> enqueues timer-fed secondary scheduler jobs. The live Pi proof passed a normal
> `net-iterate.sh` run and a clean 3-cycle repeat. Proof lines included
> `runtime v38: secondary scheduler wake protocol`, `bootcert ok=1 version=38
> wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 ... events_lost=0`, `certificate ok=1 version=38 substrate=1
> bootcert=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`,
> and `sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1 signals=825
> mask=0xe targets=825 waits=84020214 wakes=84028069 gap=1
> imbalance=6818555 core0_wait=0 core1_wait=30418819 core2_wait=23601879
> core3_wait=30047539 core0_wake=0 core1_wake=30432773
> core2_wake=23612725 core3_wake=30060963 selftest=1`. Repeat cycles kept
> `sched6 ok=1`, `wake=1`, `runqueues total=0`, and `events_lost=0`. WFE
> imbalance is telemetry, not a gate, because the A72 can resume WFE for
> architectural events beyond scheduler SEV.

> ## Runtime V37 timer-fed secondary C scheduler jobs ground truth (2026-06-06)
> V37 keeps Swift task execution on core 0 and extends V36's bounded timer-fed
> secondary worker path into typed C-only scheduler jobs. Secondary cores 1-3
> execute those jobs in their own loops and report execution, completion, noop,
> checksum, gap, and imbalance counters. The live Pi proof passed a normal
> `net-iterate.sh` run and a clean 3-cycle repeat. Proof lines included
> `runtime v37: timer-fed secondary C scheduler jobs`, `bootcert ok=1 version=37
> job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 ... events_lost=0`, `certificate ok=1 version=37 substrate=1
> bootcert=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 ... events_lost=0`, and `sched5 ok=1 version=37 job_exec=1
> worker_feed=1 secondary_workers=1 executions=756 completions=756 noops=0
> checksum=698517273110 gap=0 imbalance=0 core0_exec=0 core1_exec=252
> core2_exec=252 core3_exec=252 core0_done=0 core1_done=252 core2_done=252
> core3_done=252 selftest=1`. Repeat cycles stayed at `738/738`, `699/699`,
> and `699/699` executions/completions with `noops=0 gap=0 imbalance=0`;
> V33 `runqueues` stayed `total=0`.

> ## Runtime V36 timer-fed secondary scheduler workers ground truth (2026-06-06)
> V36 keeps Swift task execution on core 0, but lets the scheduler timer feed
> bounded worker tokens to secondary per-core queues. Cores 1-3 drain those
> tokens in their C-only SMP loops, and the proof surface must show feed counts,
> drain counts, drops, and bounded gap/imbalance. The live Pi proof passed a
> normal `net-iterate.sh` run and a clean 3-cycle repeat. Proof lines included
> `runtime v36: timer-fed secondary scheduler workers`, `bootcert ok=1 version=36
> worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`,
> `certificate ok=1 version=36 substrate=1 bootcert=1 worker_feed=1
> secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`, and
> `sched4 ok=1 version=36 worker_feed=1 secondary_workers=1 feeds=708
> drains=711 drops=0 gap=0 feed_imbalance=0 drain_imbalance=0 core0_feed=0
> core1_feed=236 core2_feed=236 core3_feed=236 core0_drain=0 core1_drain=237
> core2_drain=237 core3_drain=237 selftest=1`. Repeat cycles stayed at
> `693/696`, `684/687`, and `684/687` feeds/drains with `drops=0 gap=0`;
> V33 `runqueues` stayed `total=0`.

> ## Runtime V35 secondary-owned scheduler workers ground truth (2026-06-06)
> V35 keeps secondary cores out of Swift runtime state, but gives their C-only
> SMP loop a scheduler worker hook. The proof path injects bounded V35 worker
> tokens into cores 1-3 queues; each secondary core drains only its own worker
> token while core 0 remains at zero worker drains. The live Pi proof passed a
> normal `net-iterate.sh` run and a clean 3-cycle repeat. Proof lines included
> `runtime v35: secondary-owned scheduler workers`, `bootcert ok=1 version=35
> secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1
> smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`,
> `certificate ok=1 version=35 substrate=1 bootcert=1 secondary_workers=1
> preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
> agent=1 runtime=1 ... events_lost=0`, and `sched3 ok=1 version=35
> secondary_workers=1 active=1 cores=4 online=4 worker_drains=3
> worker_idles=1396994 min=1 max=1 imbalance=0 core0=0 core1=1 core2=1
> core3=1 selftest=1`. Repeat cycles kept `core0=0`, cores 1-3 at `1/1/1`,
> and V34 `sched2` dispatch counters balanced.

> ## Runtime V34 timer-driven SMP scheduler dispatch ground truth (2026-06-06)
> V34 keeps Swift task execution on the cooperative executor, but the scheduler
> IRQ path now routes bounded dispatch tokens through each online A72 core queue
> and records per-core dispatch/fairness counters. This proves the first
> timer-driven SMP scheduler surface without making secondary cores enter Swift
> runtime state. The live Pi proof passed a normal `net-iterate.sh` run and a
> clean 3-cycle repeat. Proof lines included `runtime v34: timer-driven smp
> scheduler dispatch`, `bootcert ok=1 version=34 preemptive=1 smp_scheduler=1
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1
> ... events_lost=0`, `certificate ok=1 version=34 substrate=1 bootcert=1
> preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
> agent=1 runtime=1 ... events_lost=0`, and `sched2 ok=1 version=34
> preemptive=1 smp_scheduler=1 active=1 cores=4 online=4 dispatches=548
> routes=548 min=137 max=137 imbalance=0 core0=137 core1=137 core2=137
> core3=137 selftest=1`. Repeat cycles stayed exactly balanced at
> `186/186/186/186`, `160/160/160/160`, and `157/157/157/157`.

> ## Runtime V33 atomics, spinlocks, and per-core run queues ground truth (2026-06-06)
> V33 adds the first Aether-owned synchronization substrate needed before real
> cross-core dispatch: `__atomic_*` wrappers, spinlock selftests, and four fixed
> per-core scheduler run queues protected by one lock per queue. Swift task
> execution still stays on the cooperative executor; this slice proves the
> bounded C surface and shell/certificate evidence. The live Pi proof passed a
> normal `net-iterate.sh` run and a clean 3-cycle repeat. Proof lines included
> `runtime v33: atomics spinlocks per-core run queues`, `bootcert ok=1 version=33
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1
> ... events_lost=0`, `certificate ok=1 version=33 substrate=1 bootcert=1
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`,
> `locks ok=1 version=33 atomics=1 spinlocks=1 acquisitions=2 contentions=0
> selftest=1`, and `runqueues ok=1 version=33 cores=4 capacity=8 total=0
> core0=0 core1=0 core2=0 core3=0 enqueues0=8 dequeues0=8 selftest=1`.

> ## Runtime V32 SMP secondary-core bring-up ground truth (2026-06-06)
> V32 keeps Swift execution on core 0 and releases A72 cores 1-3 into a fixed
> C-only heartbeat/accounting loop. The first hardware attempt booted V32 but
> reported `smp=0`; the root cause was that the default Raspberry Pi `armstub8`
> had parked secondaries before Aether's `_start`. The accepted path writes
> `_start` to the armstub8 64-bit spin-table slots `0xe0`, `0xe8`, and `0xf0`,
> cleans those cache lines, and issues `sev`. Proof lines included `runtime v32:
> smp secondary-core bring-up`, `bootcert ok=1 version=32 smp=1 scheduler=1
> certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate ok=1
> version=32 substrate=1 bootcert=1 smp=1 scheduler=1 agent=1 runtime=1
> ... events_lost=0`, and `cores ok=1 version=32 capacity=4 online=4 mask=0xf
> primary=0 release=0xe selftest=1 core0=1 core1=1 core2=1 core3=1`. Paired
> `cores` probes showed secondary heartbeats advancing.

> ## Runtime V31 preemptive scheduler substrate ground truth (2026-06-06)
> V31 keeps the V25 request envelope and V30 certificate surface, then adds a
> fixed C-owned scheduler timer client plus a `sched` command. This is the first
> preemptive layer over the cooperative executor: the IRQ path records scheduler
> ticks/preemption accounting before the existing sleep and executor timer
> clients run. The live Pi proof passed a normal `net-iterate.sh` run and a clean
> 3-cycle live netboot repeat. Proof lines included `runtime v31: preemptive
> scheduler substrate`, `bootcert ok=1 version=31 scheduler=1 certificate=1
> agent=1 runtime=1 ... events_lost=0`, `certificate ok=1 version=31 substrate=1
> bootcert=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`, and
> `sched ok=1 version=31 active=1 cores=1 core=0 interval_ticks=2700000
> ticks=152 irq_ticks=152 preemptions=152 runqueue=0/8 enqueues=4 dequeues=4
> selftest=1`.

> ## Runtime V30 Swift-native kernel substrate certificate ground truth (2026-06-06)
> V30 keeps the V25 request envelope, keeps the V29 `agent` session, and adds a
> `certificate` command plus `certificate-loop.sh` host proof harness. The live
> Pi proof passed a normal `net-iterate.sh` run and a 3-cycle certificate loop.
> Proof lines included `runtime v30: swift-native kernel substrate certificate`,
> `bootcert ok=1 version=30 certificate=1 agent=1 runtime=1 taxonomy=1 ... events_lost=0`,
> `certificate ok=1 version=30 substrate=1 bootcert=1 agent=1 runtime=1 memory=1
> objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1
> channels=1 drivers=1 pressure=1 pools=1 mmu=1 ... events_lost=0`, and
> `certificate-loop ok=1 version=30 cycles=3 completed=3 substrate=1 bootcert=1
> agent=1 runtime=1 events_lost=0`.

> ## Runtime V29 agent-oriented control session ground truth (2026-06-06)
> V29 keeps the V25 request envelope and adds an `agent` command for a compact
> machine-checkable control-session summary. The hardware proof passed a normal
> `net-iterate.sh` run and a clean `set -e` 3-cycle netboot + `agent-session.sh`
> loop. Proof lines included `runtime v29: agent-oriented control session`,
> `bootcert ok=1 version=29 agent=1 runtime=1 taxonomy=1 ... events_lost=0`,
> `agent ok=1 version=29 health=green bootcert=1 runtime=1 protocol=2 agent=1 events_lost=0`,
> and `agent-session ok=1 version=29 health=green bootcert=1 runtime=1 stress=1 soak=1 events_lost=0`.

> ## Runtime V28 Swift runtime dependency audit ground truth (2026-06-06)
> V28 turns the Swift runtime boundary into a checked contract. The kernel shell
> reports `runtime ok=1 version=28 swift=6.3.2 source_hooks=10 linked_hooks=2
> heap_shims=5 linked_heap_shims=3 required_symbols=5 audit=1`; the boot
> certificate reports `bootcert ok=1 version=28 runtime=1 taxonomy=1 ... events_lost=0`;
> and host `./runtime-audit.sh .build/release/Application` reports
> `runtime-audit ok=1 version=28 ... missing=none`. The linked-hook count is
> intentionally smaller than the source-owned hook count because only
> load-bearing symbols present in the built Mach-O are required by the host nm
> audit.

> ## Runtime V27 panic taxonomy and symbolic retained records ground truth (2026-06-06)
> V27 keeps the retained-record storage model from V6, but adds stable numeric
> `kind_id`, `category`, and `reason_id` fields so host tools and future agents
> do not need to parse free-form reason text. `bootcert` now reports taxonomy
> support with `bootcert ok=1 version=27 taxonomy=1 ... events_lost=0`.
> Hardware proof: a normal `net-iterate.sh` run passed, `panic-test` rebooted
> and `retained` reported `retained valid=1 kind=panic kind_id=1 category=1 reason_id=1
> ... reason=panic-test`, `fault-test` rebooted and `retained`
> reported `kind=fault kind_id=2 category=2 reason_id=2 esr=0xf20000a5
> elr=0x92968 ... reason=sync-fault`, `symbolicate-retained.sh` returned
> `symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault ...`, and a
> clean `set -e` 3-cycle netboot loop passed.

> ## Runtime V26 host soak harness ground truth (2026-06-06)
> V26 does not bump the kernel boot certificate or add a new kernel marker. It
> adds `soak-loop.sh`, a host-side repeatability harness that runs
> `net-iterate.sh`, then uses the V25 request protocol to collect `status`,
> `bootcert`, `stress`, `soak`, and `events` summaries into a log. The TFTP
> provider lifecycle stays external. Hardware proof: `AETHER_SOAK_CYCLES=3
> ./soak-loop.sh "$HOME/aether-tftp"` passed all cycles. Proof
> lines included `soak summary cycle=3 command=bootcert id=2622 line=bootcert
> ok=1 version=25 ... events_lost=0`, `soak summary cycle=3 command=stress ...
> heap_leak=0 frame_leak=0`, `soak summary cycle=3 command=soak ...
> failures=0 heap_leak=0 frame_leak=0`, `soak summary cycle=3 command=events
> ... lost=0 ... selftest=1`, and
> `soak result ok=1 cycles=3 completed=3 log=/tmp/aether-soak-v26.log`.

> ## Runtime V25 scriptable command protocol v2 ground truth (2026-06-06)
> V25 does not change the scheduler, driver model, or shell command meanings.
> It adds an ASCII request/response envelope for agent control while preserving
> direct human commands and the `r`/`R` reset aliases. The protocol command
> reports the current wire contract, request-wrapped commands use
> `req id=<n> cmd=<command>`, and responses emit stable begin/end or error
> lines. `bootcert` reports `protocol=1`. Hardware proof: a single
> `net-iterate.sh` run passed, followed by a clean `set -e` 3-cycle loop.
> Proof lines included `runtime v25: scriptable command protocol v2`,
> `protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1
> errors=1 max_line=80`, `bootcert ok=1 version=25 protocol=1 ... drivers=1
> pressure=1 pools=1 ... events_lost=0`, and
> `resp id=25 ok=1 cmd=status end`.

> ## Runtime V24 fixed driver registry ground truth (2026-06-06)
> V24 does not add a dynamic device model or rewrite driver hot paths. It adds
> `kernel_driver.c`, a fixed C-owned registry for UART0, CNTP, GIC, and
> watchdog. Each driver gets a kernel object handle plus stable name, state,
> INTID, base address, capabilities, IRQ count, error count, and operation
> count surfaces. The UART shell adds `drivers` and `drivercheck`; `bootcert`
> reports `drivers=1`. Hardware proof: a single `net-iterate.sh` run passed,
> followed by a clean `set -e` 3-cycle loop. Proof lines included
> `runtime v24: fixed driver registry`, `bootcert ok=1 version=24 ...
> drivers=1 pressure=1 pools=1 ... events_lost=0`, `drivers count=4 capacity=4 selftest=1`,
> and `drivercheck ok=1 ... uart_irq=16 timer_irq=689
> gic_total=705 watchdog_resets=0 unknown_irq=0 selftest=1`.

> ## Runtime V23 allocator/pool pressure telemetry ground truth (2026-06-06)
> V23 does not replace the heap, change pool ownership, or add dynamic remaps.
> It adds read-only heap fragmentation counters over the existing boundary-tag
> allocator: free block count, allocated block count, smallest/largest free
> block, fragmentation permille, and pressure-run snapshots. It also adds
> aggregate pool pressure counters over the fixed V22 pool records. The UART
> shell adds `heapfrag` and `poolstats`; `bootcert` reports `pressure=1`.
> Hardware proof: a single `net-iterate.sh` run passed, followed by a clean
> `set -e` 3-cycle loop. Proof lines included `runtime v23: allocator and pool
> pressure telemetry`, `bootcert ok=1 version=23 ... pressure=1 pools=1 ...
> events_lost=0`, `heapfrag ok=1 ... fragmentation_permil=0 ...
> pressure_largest_free=4184112`, and `poolstats ok=1 ... total_slots=24 ...
> failed_allocs=1`.

> ## Runtime V22 guarded typed pools ground truth (2026-06-05)
> V22 does not replace the heap or add dynamic remaps. It adds
> `kernel_pool.c`, a fixed C-owned typed pool substrate with 4 pool descriptors,
> 8 slots per pool, guard words, generation counters, high-water telemetry, and
> deterministic selftests for overflow, bad-free, and double-free handling. The
> UART shell adds `pools` and `poolcheck`; `bootcert` reports `pools=1`.
> Hardware proof: a single `net-iterate.sh` run passed, followed by a clean
> `set -e` 3-cycle loop. Proof lines included `runtime v22: guarded typed
> pools`, `bootcert ok=1 version=22 ... pools=1 ... mmu=1 ... events_lost=0`,
> `poolcheck ok=1 ... bad_frees=1 double_frees=1`, and `pools count=3
> capacity=4 selftest=1`.

> ## Runtime V21 ground truth (2026-06-05)
> V21 does not add dynamic remapping. It records the current EL1 stage-1 MMU
> ownership boundary before future isolation work. `MMU_OWNERSHIP.md` documents
> the current identity map, BCM2711 low-peripheral boundary, and future TLBI /
> table-allocation invariants. `mmu.c` exposes read-only introspection for the
> static L1 table: 512 entries, four live 1 GiB block regions, Normal cacheable
> mappings for `0x00000000` through `0xBfffffff`, Device mapping for
> `0xC0000000` through `0xffffffff`, and fault entries above that. The UART
> shell adds `mmu`, and `bootcert` reports `mmu=1`. Hardware proof: a single
> `net-iterate.sh` run passed, followed by a clean `set -e` 3-cycle loop. Proof
> lines included `runtime v21: mmu ownership boundary`, `bootcert ok=1
> version=21 ... mmu=1 ... channels=1 ... events_lost=0`, and `mmu ok=1
> regions=4 entries=512 block_size=0x40000000 ... selftest=1`.

> ## Runtime V20 ground truth (2026-06-05)
> V20 adds `AetherChannel.swift`, a small Swift wrapper over the fixed C-owned
> mailbox queues. The wrapper exposes nonblocking `send` and `tryReceive` plus
> an async `receive` loop backed by `timerSleepMillis(25)`, while the bounded
> storage and counters stay in `kernel_mailbox.c`. The demo mail producer and
> consumer now use `AetherChannelU64`, the shell adds `channeltest`, and
> `bootcert` reports `channels=1`. Hardware proof: a single net-iterate run
> passed attempt 1, followed by a 3-cycle `net-iterate.sh` loop with all cycles
> passing attempt 1. Proof lines included `runtime v20: bounded async channels`,
> `bootcert ok=1 version=20 ... channels=1 taskspawns=1 cancellations=1 ...
> events_lost=0`, `channeltest ok=1 mailbox=1 sent=1 received=1
> value=0x000000000000c020`, `kobjects count=12 capacity=16 active=12`, and
> `events count=17 capacity=64 lost=0 sequence=17 selftest=1`.

> ## Runtime V19 ground truth (2026-06-05)
> V19 adds `AetherTask.swift`, a small Swift-owned registration/spawn boundary
> for Embedded Swift tasks. The fixed C task registry now stores parent task ID,
> task handle, spawn count, and completion count; `tasks2` prints those fields
> and `taskcheck` summarizes the live task substrate. The wrapper signature is
> `@escaping @Sendable () async -> Void`; Swift 6.3.2 rejected the first
> non-Sendable closure shape under strict concurrency, which is exactly the kind
> of guardrail this kernel is trying to preserve from the metal up. Hardware
> proof: a 3-cycle `net-iterate.sh` loop passed. Cycle 1 passed on attempt 2
> after stale SD fallback; cycles 2 and 3 passed on attempt 1. Proof lines
> included `runtime v19: structured aether task spawn`, `bootcert ok=1
> version=19 ... taskspawns=1 cancellations=1 ... events_lost=0`, `taskcheck
> ok=1 count=7 capacity=8 spawns=6 completions=0`, `supervisor count=7
> capacity=8 unhealthy=0`, and `events count=16 capacity=64 lost=0 sequence=16
> selftest=1`.

> ## Runtime V18 ground truth (2026-06-05)
> V18 adds a fixed 16-slot C-owned cooperative cancellation token table. Tokens
> are generation-tagged raw IDs with active, cancelled, and completed states;
> create/request/complete/state queries do not allocate and report stable
> capacity/bad-token errors. Swift registers a logical `cancel` task so the
> task/supervisor surfaces account for the subsystem, and the UART shell adds
> `canceltest`. Hardware proof: a 3-cycle `net-iterate.sh` loop passed. Cycle 1
> passed on attempt 1; cycles 2 and 3 passed on attempt 2 after stale/partial
> pre-V18 fallback attempts. The proof lines included `bootcert ok=1 version=18
> ... cancellations=1 ... events_lost=0`, `canceltest ok=1 capacity=16 active=0
> requested=1 completed=1 last_error=0`, `supervisor count=7 capacity=8
> unhealthy=0 total_missed=0 selftest=1`, and `events count=15 capacity=64
> lost=0 sequence=15 selftest=1`.

> ## Runtime V17 ground truth (2026-06-05)
> V17 adds `bootcert`, a single UART shell certificate that aggregates the
> currently load-bearing invariants: memory map, heap guard, frame allocator,
> retained-record visibility, object registry, task registry, mailboxes,
> supervisor, and event log. `retained_valid` is reported but intentionally does
> not fail the certificate, because destructive diagnostics are allowed to leave
> a previous retained record. `events_lost=0` is load-bearing. Hardware proof:
> a fresh 3-cycle `net-iterate.sh` loop passed; the certificate lines included
> `bootcert ok=1 version=17 memmap=1 heap=1 frames=1 retained_valid=0 kobjects=1
> tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0`, and `events` still
> reported `count=13 capacity=64 lost=0 sequence=13 selftest=1` with boot event
> `a0=0x11`.

> ## Runtime V16 ground truth (2026-06-05)
> V16 adds a 64-record C-owned event ring for machine-checkable kernel/agent
> observability. Records store sequence, CNTP ticks, stable event kind, and three
> raw args. The ring keeps newest records on overflow and increments `lost`.
> Emission is deliberately coarse: boot, supervisor registration, handle selftest,
> first task/timer ticks, first mailbox tx/rx, shell events command, and selftest.
> Hardware proof: `events count=11 capacity=64 lost=0 sequence=11 selftest=1`,
> with event kinds `boot`, `supervisor`, `handle`, `task`, `timer`, `mailbox`,
> `shell`, and `selftest`.

> ## Runtime V15 ground truth (2026-06-05)
> V15 keeps kernel object lifetime in the fixed C registry and exposes raw 64-bit
> handles to Swift/serial only. Handles encode slot, generation, kind, and granted
> capability mask. Lookup rejects stale generations and insufficient capabilities
> with stable `KERNEL_OBJECT_LOOKUP_*` reason codes. `kobjects` prints handle,
> generation, and caps fields; `capcheck` proves inspect success, denied control,
> and stale-handle rejection. Hardware proof: `handlecheck ok=1 handle_selftest=1
> cap_selftest=1`, `kobjects count=11 capacity=16 active=11 selftest=1
> handle_selftest=1 cap_selftest=1`, `object index=0 ... handle=0x0000000103000101
> generation=1`, and `capcheck ok=1 inspect=1 denied=1 stale=1 last_error=2`.

> ## Runtime V14 ground truth (2026-06-05)
> V14 introduces a fixed C-owned supervisor table keyed by V12 task IDs. Each record has
> a heartbeat deadline, last heartbeat time, miss count, state, and observe/panic policy.
> The normal proof path uses observe policy so it can report unhealthy tasks without
> destructive resets. `fast` runs the periodic supervisor check, and the shell exposes
> `supervisor` plus `health`. Hardware proof: `supervisor count=6 capacity=8
> unhealthy=0 total_missed=0 selftest=1` and `health ok=1 supervised=6 unhealthy=0`.

> ## Runtime V13 ground truth (2026-06-05)
> V13 introduces fixed C-owned UInt64 mailbox queues. Each mailbox registers as a
> kernel object and tracks depth, sent, received, drop, and stable error counters.
> The Swift demo adds producer/consumer async tasks that exchange values through
> mailbox 0 and print `rtv13 mail tx` / `rtv13 mail rx` lines. `sendtest` uses a
> reserved selftest mailbox so the command is deterministic even while the demo
> mailbox is active. Hardware proof: `rtv13 mail tx/rx`, `mailboxes count=2
> capacity=4 queue_capacity=8 selftest=1`, and `sendtest ok=1`.

> ## Runtime V12 ground truth (2026-06-05)
> V12 introduces bounded kernel object and task registries without allocator use in
> the registry path. The object table names runtime, driver, and task records; the
> task table tracks object id, state, tick count, and period for the current async
> demo tasks plus the UART shell task. Shell commands `kobjects` and `tasks2` expose
> the tables in machine-checkable form. Hardware proof: `kobjects count=7 capacity=16
> active=7 selftest=1` and `tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast`.

> ## Runtime V11 ground truth (2026-06-05)
> V11 adds cheap boot and soak invariants over the V8-V10 memory foundation. Startup and shell
> `bootcheck` report memory-map, heap, frame, retained-record, heap-free, and frame-free state.
> `soak` runs three bounded heap/frame pressure rounds and reports failures plus peak/leak counters.
> `net-iterate.sh` now treats `status`, `bootcheck`, `stress`, and `soak` serial probes as the
> default hardware proof. During V11 proof, `retained`/`retained-clear` exposed a real allocator
> compatibility bug: Swift Embedded `_swift_allocObject` in this 6.3.2 toolchain calls
> `posix_memalign` with an 8-byte floor and then destroys heap objects through direct `free(object)`.
> The allocator's payload gate had incorrectly required 16-byte alignment, so valid Swift objects
> were rejected as `heap-invalid-free`. The fix keeps malloc's 16-byte payload alignment but allows
> 8-byte-aligned `posix_memalign` payloads through the back-pointer/header/footer validation path.
> Hardware proof after the fix: `retained valid=... reason=heap-invalid-free` could be read without
> rebooting, `retained clear ok=1`, `retained valid=0`, and `bootcheck ok=1 ... retained_valid=0`.

> ## Runtime V10 ground truth (2026-06-05)
> V10 adds explicit guard probes. `frameprobe` is non-destructive and verifies bad frame frees and
> double frame frees are rejected while final frame ownership returns to all-free. Destructive heap
> commands `heap-invalid-free-test` and `heap-double-free-test` intentionally panic through the
> retained-record path. Hardware proof: `frameprobe ok=1 last_ok=1 ...`; `heap-invalid-free-test`
> rebooted and the next boot reported retained `reason=heap-invalid-free`.

> ## Runtime V9 ground truth (2026-06-05)
> V9 adds bounded memory pressure self-tests. `heap_pressure_selftest` allocates, touches, and frees
> fixed-size heap blocks in a non-linear order; `kernel_frame_pressure_selftest` does the same for a
> fixed set of frames. The UART `stress` command reports pass/fail plus peak and leak counters.
> Hardware proof: `stress ok=1 heap=1 frames=1 heap_peak=62928 frame_peak=16 heap_leak=0 frame_leak=0`.

> ## Runtime V8 ground truth (2026-06-05)
> V8 keeps the V7 physical memory ownership model: heap allocation still lives in the fixed
> `0x00400000`-`0x00800000` window, and the frame allocator still manages only
> `0x00800000`-`0x04000000`. It does not add frame-backed heap allocation, paging, or
> desktop/networking work. It hardens the existing allocator paths instead: `free` and
> `realloc` validate heap pointers before metadata mutation, detect double frees, keep
> stable `HEAP_GUARD_*` reason codes, count invalid/double/corruption events, and poison
> freed payloads. Fresh `malloc` payloads are not filled. Real allocator misuse panics;
> shell self-checks stay non-destructive. The frame allocator now tracks bad frees and
> double frees and exposes a fixed-storage stress selftest. Hardware proof: fresh netboot
> printed `runtime v8: allocator guardrails`; `heapcheck` reported
> `ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0`; `framecheck` reported
> `ok=1 total=14336 free=14336 used=0 bad_frees=0 double_frees=0 error=0 stress=1`;
> a 3-cycle `net-iterate.sh` loop passed.

> ## Runtime V7 ground truth (2026-06-05)
> The kernel has an explicit fixed low-memory ownership map and a fixed-storage
> 4 KiB frame allocator for a conservative managed window from `0x00800000` to
> `0x04000000`. The existing heap remains fixed at `0x00400000`-`0x00800000`;
> V7 does not move heap allocation onto frames and does not add dynamic page-table
> remapping. Hardware proof: fresh netboot printed `runtime v7: memory map + frame allocator`;
> `memmap` reported `valid=1 regions=7 page_size=4096 reserved=8388608 error=0`;
> `frames` reported `total=14336 free=14336 used=0 reserved=0 base=0x800000 limit=0x4000000 selftest=1`;
> a 3-cycle `net-iterate.sh` loop passed.

> ## Runtime V6 ground truth (2026-06-05)
> Panic/fault paths now write a checksum-protected retained record before watchdog reset.
> The record lives at `0x003ff000`, outside the loaded image and just below `HEAP_BASE`.
> Because Runtime V2+ enables cacheable RAM, retained writes and clears must clean the record's
> D-cache lines (`dc cvac` over the struct, then `dsb sy`) before reset; a `dsb` alone was
> hardware-proven insufficient. Hardware proof: `panic-test` rebooted and `retained` reported
> `valid=1 kind=panic reason=panic-test`; `fault-test` rebooted through the sync vector and
> `retained` reported `valid=1 kind=fault esr=0xf20000a5 ... reason=sync-fault`.

> ## Runtime V5 ground truth (2026-06-05)
> The IRQ-backed UART shell now exposes diagnostics commands:
> `diag`, `irqs`, `timers`, `memcheck`, `faults`, `panic-test`, and `fault-test`.
> Safe command proof on hardware returned machine-checkable `diag version=v5`, `irqs total=`,
> `timers now=`, `memcheck ok=1`, and `faults seen=0` lines while `rtv2 fast/slow/long`
> cadences continued. `panic-test` and `fault-test` are destructive diagnostics and are not
> normal liveness checks; Runtime V6 routes them through watchdog reset plus retained records.

> ## Runtime V4 ground truth (2026-06-05)
> PL011 RX is IRQ-backed. UART0 receive/receive-timeout interrupts drain into a fixed C byte
> ring, route through GIC INTID 153 to CPU0, and wake one Swift async shell waiter. The shell no
> longer polls RX every 25 ms; it awaits `uartReadByteAsync()`.

> ## Runtime V3 ground truth (2026-06-05)
> A dedicated async UART shell task polls PL011 RX every 25 ms using `timerSleepMillis`.
> It accepts line commands (`help`, `status`, `heap`, `queues`, `tasks`, `reboot`) and prints
> machine-checkable `key=value` response lines. Runtime stats come from C support APIs for
> heap, executor queues, and timer active clients, plus Swift sleeper/task counters. `r`/`R`
> remain watchdog-reset aliases for the netboot iteration loop.

> ## Runtime V2 ground truth (2026-06-05)
> CNTP is now owned by a shared timer arbiter in `timersleep_hw.c`, with separate clients for
> continuation sleeps and executor delayed jobs. `TimerSleep.swift` uses an 8-slot continuation
> queue (`timerSleepMillis`/`timerSleepSeconds`), and `executor.c` implements
> `swift_task_enqueueGlobalWithDelayImpl` plus `swift_task_enqueueGlobalWithDeadlineImpl` against
> the same arbiter. The live hardware proof is a netbooted image printing independent
> `rtv2 fast`, `rtv2 slow`, and `rtv2 long` cadences.

> ## Resolution: build on the Mach-O path, which ships `_Concurrency`
> `_Concurrency` is NOT built for `aarch64-none-none-elf` (true in both 6.0 and 6.3.2), but IT IS
> built for **`arm64-apple-none-macho`**. We migrated the build to that triple/object-format
> (commit 07de443, branch `feat/macho-concurrency`) and **hardware-re-verified the full MS1–3
> foundation** (boot/UART/timer/GIC IRQ all identical to the ELF kernel) on the real Pi 4. Empirically
> confirmed `import _Concurrency` + `Task{}` compiles for `arm64-apple-none-macho` on swift-6.3.2.
> So async/await is available on a proven base; the executor-hook design below is now buildable.
>
> Build recipe + Mach-O gotchas: see `BRINGUP_PLAYBOOK.md` + the Minni notes. The `[VERIFY]` tags below
> are resolved by the concurrency research (Agent recipe): hooks are `@_cdecl` define-the-symbol (not
> `*_hook` pointers); run jobs via `UnownedJob.runSynchronously(on:)` with a dummy `SerialExecutor`;
> heap is mandatory — provide `malloc`/`free`/`posix_memalign` (a bump allocator is INSUFFICIENT, since
> `Task.sleep` continuations are freed); `Task.sleep(nanoseconds:)` → `enqueueGlobalWithDelay` (ns) →
> `CNTP_CVAL_EL0`; `wfi` wakes on a pending IRQ even with `PSTATE.I` masked (race-free drain).
>
> CORRECTIONS to fold in when implementing: prefer `Task.sleep(nanoseconds:)` for the demo (routes
> through the delay hook we implement; the research's deadline-hook "enqueue immediately" fallback would
> NOT actually delay a `ContinuousClock` sleep). Watch the executor's own allocations — the research used
> Swift `Array` queues (which allocate); keep enqueue paths simple and the allocator reentrancy-safe
> (enqueue runs in task context, the timer IRQ only matures the delay queue → wakes `wfi`).
>
> ## GROUND TRUTH (2026-06-04) — `nm` + `ExecutorImpl.h` from our actual 6.3.2 toolchain
> Verified empirically against `usr/lib/swift/embedded/arm64-apple-none-macho/libswift_Concurrency.a`,
> `libswift_ConcurrencyDefaultExecutor.a`, and `usr/include/swift/ExecutorImpl.h`. **Three corrections to
> the original plan below — the §2/§3b text under them is superseded by this block:**
> 1. **Define the `…Impl` symbols, NOT the public trampolines.** `swift_task_enqueueGlobal` /
>    `…WithDelay` / `asyncMainDrainQueue` / `enqueueMainExecutor` are already **defined (T)** in
>    `libswift_Concurrency.a`; redefining them = duplicate-symbol link error. The real seam is the
>    `…Impl` set, which `libswift_ConcurrencyDefaultExecutor.a` defines. So: **do NOT link
>    DefaultExecutor.a**, and provide the `…Impl` functions ourselves.
> 2. **Write the executor in C, not Swift `@_cdecl`.** The Impl functions are `SWIFT_CC(swift)`
>    (`__attribute__((swiftcall))`); `@_cdecl` emits the C convention → ABI mismatch. `ExecutorImpl.h`
>    is explicitly "the declarations you need to write a custom global executor in plain C." → new file
>    `Sources/Support/executor.c` that `#include <swift/ExecutorImpl.h>` and implements the contract with
>    the header's own macros. Run jobs via the header's inline `swift_job_run(job, swift_executor_generic())`
>    (→ `_swift_job_run_c`, defined in the archive — we call it, don't provide it).
> 3. **`swift_slowAlloc`/`swift_slowDealloc` are also required** (true externals alongside `malloc`/`free`).
>    Stage 1's `malloc`/`free` is the right base; add thin `swift_slowAlloc`(→`posix_memalign`/`malloc`)
>    / `swift_slowDealloc`(→`free`) shims.
>
> **Contract to implement (exact signatures from `ExecutorImpl.h`):**
> ```c
> #include <swift/ExecutorImpl.h>
> SWIFT_CC(swift) void swift_task_enqueueGlobalImpl(SwiftJob *job);                       // push ready ring
> SWIFT_CC(swift) void swift_task_enqueueGlobalWithDelayImpl(SwiftJobDelay delayNs, SwiftJob *job); // delay FIRST, ns
> SWIFT_CC(swift) void swift_task_enqueueMainExecutorImpl(SwiftJob *job);                 // == enqueueGlobal (1 thread)
> SWIFT_CC(swift) SwiftExecutorRef swift_task_getMainExecutorImpl(void);                  // return swift_executor_generic()
> SWIFT_CC(swift) bool  swift_task_isMainExecutorImpl(SwiftExecutorRef e);                // return true
> SWIFT_CC(swift) void  swift_task_checkIsolatedImpl(SwiftExecutorRef e);                 // no-op
> SWIFT_CC(swift) int8_t swift_task_isIsolatingCurrentContextImpl(SwiftExecutorRef e);    // return 1 (isolated)
> SWIFT_RUNTIME_ATTRIBUTE_NORETURN SWIFT_CC(swift) void swift_task_asyncMainDrainQueueImpl(void); // THE PUMP
> SWIFT_CC(swift) void swift_task_enqueueGlobalWithDeadlineImpl(long long s,long long ns,long long ts,long long tns,int clk,SwiftJob*); // route to delay queue or enqueue if due
> SWIFT_CC(swift) void swift_task_donateThreadToGlobalExecutorUntilImpl(bool(*cond)(void*),void*ctx);      // dummy/assert (optional)
> // run a job: swift_job_run(job, swift_executor_generic());   // inline in the header → _swift_job_run_c
> // SwiftJobDelay = unsigned long long (ns). Job priority via swift_job_getPriority(job) if we want priority ordering.
> ```
> The pump (`asyncMainDrainQueueImpl`): loop { pop a ready job → `swift_job_run(job, generic)`; when ready
> ring empty → promote delay-queue jobs whose deadline ≤ now into ready (also done from the timer IRQ),
> arm `CNTP` for the next deadline, `wfi` if nothing due }. Ready/delay queues are fixed C arrays guarded by
> `irq_save()/irq_restore()` (IRQ matures the delay queue → wakes `wfi`). Still prefer `Task.sleep(nanoseconds:)`
> for the demo so we hit `WithDelayImpl` (ns, no clock dependency) rather than the deadline/clock path.

## 0. Goal
Run real Swift `async/await` on the metal: a single-threaded **cooperative executor** whose time
source is the GIC-400 timer IRQ we already have. Historical Stage 3 payoff demo — a one-task heartbeat:
```swift
func asyncMain() async {
  var n: UInt64 = 0
  while true {
    uartPuts("rtv2 slow ")             // StaticString — NOT interpolation (Embedded)
    uartPutHex(n)
    uartPuts("\n")
    try? await Task.sleep(for: .seconds(1))   // suspends; CPU sleeps in wfi
    n &+= 1
  }
}
```
Current Runtime V2 success on hardware = independent `rtv2 fast/slow/long` cadences with the CPU
**idle in `wfi` between jobs**, woken when the timer IRQ matures continuation sleeps or executor delays.
Structured concurrency, hardware-timer-backed.

## 1. Why this is more than the polled/IRQ heartbeat
Milestone 3 was an IRQ that prints. This is the IRQ **resuming a suspended `async` task** — i.e. the
Swift concurrency runtime actually scheduling work on bare metal. Requires us to supply the runtime's
executor + allocator hooks that an OS would normally provide.

## 2. The contract (from the Embedded `_Concurrency` module — names confirmed, signatures `[VERIFY]`)
The stdlib leaves these to us; with `--unresolved-symbols=ignore-in-object-files` they link to nothing
unless we define them, then crash on first use. We MUST provide:
- `swift_task_enqueueGlobal(job)` — push a ready job onto the run queue.
- `swift_task_enqueueGlobalWithDelay(ns, job)` — schedule after a delay (backs `Task.sleep`). `[VERIFY units = ns]`
- `swift_task_enqueueGlobalWithDeadline(...)` — absolute-deadline variant (ContinuousClock). `[VERIFY if needed]`
- `swift_task_asyncMainDrainQueue()` — the main pump. `[VERIFY: do we implement it, or write our own loop + bootstrap a Task?]`

`[VERIFY]` (research): exact symbol spelling + C/Swift signatures; **define-the-function** vs
**set a `*_hook` pointer**; how to *run* a job (`UnownedJob.runSynchronously(on:)` — and which
`UnownedSerialExecutor` ref with no real executor object — vs a C `swift_job_run`); whether `@main
static func main() async` works in 6.0 Embedded or we bootstrap a `Task{}` from sync `main` then drain;
whether `SerialExecutor`/`TaskExecutor` conformance (the Grok stub approach) is valid in Embedded or the
C-hook path is the only one (suspected: C-hooks only).

## 3. Components

### 3a. Minimal allocator — `Sources/Support/alloc.c` (NEW) `[VERIFY which symbols]`
Swift `Task`s allocate. With no OS heap we provide one over a static arena (in BSS):
```c
static unsigned char heap_arena[256 * 1024] __attribute__((aligned(16)));
// provide whichever the runtime calls: posix_memalign/free, malloc/free, OR swift_slowAlloc/Dealloc
```
Start with a **bump allocator** (fast, never frees) to get a demo booting; if task slab churn needs it,
upgrade to a fixed-block free-list. Single-allocation-context (tasks allocate in task context, the IRQ
only pushes job pointers — no alloc in IRQ), so no locking needed for the bump path. `[VERIFY task
alloc/dealloc pattern + required symbol set + arena size]`

### 3b. Executor — `Sources/Application/Executor.swift` (NEW)
- **Run queue:** fixed ring buffer of job handles (e.g. 64), no alloc.
- `@_cdecl("swift_task_enqueueGlobal")` (or hook): push job. **Called from both task and IRQ context →
  wrap push/pop in a DAIF critical section** (`irq_save()`/`irq_restore()` helpers, below).
- **Drain/pump:** pop → run job → repeat; when run queue empty AND no due timers → `wfi`.
- **Timer queue:** small array of `(deadlineTicks, job)` for delayed jobs. `enqueueGlobalWithDelay`
  converts ns→CNTP ticks (`ticks = ns * CNTFRQ / 1_000_000_000`, CNTFRQ=54 MHz) and inserts; arm
  `CNTP_TVAL` for the nearest deadline.

### 3c. Timer IRQ becomes the scheduler tick — edit `IRQHandler.swift`
On INTID 30: move every due `(deadline ≤ now)` job from the timer queue into the run queue
(`enqueueGlobal`), then re-arm `CNTP` for the next-nearest deadline (or leave disabled if none).
Re-arm BEFORE EOI (level-triggered, as established). This wakes the `wfi` in the drain loop.

### 3d. Critical-section helpers — `Support.h` (append)
```c
static inline unsigned long irq_save(void){ unsigned long f; __asm__ volatile("mrs %0,daif; msr daifset,#2":"=r"(f)::"memory"); return f; }
static inline void irq_restore(unsigned long f){ __asm__ volatile("msr daif,%0"::"r"(f):"memory"); }
```
(Replaces bare `irq_enable` for queue ops so an IRQ can't corrupt a half-updated ring.)

### 3e. Bootstrap — edit `Application.swift`
Sync `main`: `uartInit` → banner → `gicInitTimerIRQ` → allocator init → start the first task
(`Task { await asyncMain() }` or detached) → `irq_enable()` → enter drain pump.
`[VERIFY exact bootstrap shape depending on async-main support]`

## 4. Embedded Swift gotchas (carry forward + new)
- **No string interpolation** in the demo — `uartPuts(StaticString)` + `uartPutHex`. (Interpolation
  allocates / may be unavailable.)
- Handlers/executor entry stay **integer-only** (no FP/SIMD → CPACR). Disassembly-check.
- Globals: `nonisolated(unsafe)` or encapsulate queue state in a C struct.
- `swift_beginAccess` = no-op `ret` stub (already confirmed).

## 5. Ranked risks
1. **Heap/allocator**: wrong symbol set or too-small arena ⇒ Task creation traps/hangs. Prime unknown.
2. **Job-run ABI**: calling the wrong run primitive / bad executor ref ⇒ crash on first job.
3. **async-main support in 6.0 Embedded**: if absent, need the manual bootstrap+drain (have a plan).
4. **Queue reentrancy**: IRQ enqueues while task pops ⇒ must use the DAIF critical section.
5. **Task.sleep/Clock availability** in Embedded 6.0 ⇒ may need a custom suspension primitive instead.

## 6. Verification plan
- Independent rebuild; `git diff` additive; **disassembly-verify** the hook symbols are defined
  (`swift_task_enqueueGlobal` etc. present, not unresolved), the run-queue critical section masks IRQ,
  the timer→run-queue handoff, allocator integer-only, no FP in the executor.
- Hardware: `rtv2 fast/slow/long` cadences, `wfi` idle between jobs (serial cadence is the liveness signal).
- Fallback if `Task.sleep` is unavailable in 6.0 Embedded: implement a custom awaitable backed directly
  by the timer IRQ (a continuation the IRQ resumes), proving the executor without depending on Clock.
