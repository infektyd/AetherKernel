# AetherKernel

A bare-metal kernel for the Raspberry Pi 4B (BCM2711, Cortex-A72) written in
**Embedded Swift** — no OS, no SDK, no Node, boots straight from `kernel8.img`.

> Status: **Runtime V44 bounded SMP concurrency soak protocol hardware-verified on real Raspberry Pi 4B**
> (2026-06-07) — netbooted image fetched `kernel8.img`, printed banner +
> padded `CurrentEL = 0x0000000000000004` (EL1), `rtv2 fast/slow/long`
> async cadences, the IRQ-backed UART shell marker, the Runtime V5 diagnostics
> marker, the Runtime V6 retained-record marker, the Runtime V7 memory marker,
> the Runtime V8 allocator-guard marker, Runtime V9-V25 self-test markers, and
> UART shell command responses over PL011 serial @ 115200. Runtime V25 scriptable
> command protocol v2 proved `protocol version=2 request=req id_field=id
> cmd_field=cmd begin_end=1 errors=1 max_line=80`, `bootcert ok=1 version=28
> runtime=1 taxonomy=1 protocol=1 ... drivers=1 pressure=1 pools=1 ... events_lost=0`, and
> `resp id=25 ok=1 cmd=status end` across a clean `set -e` 3-cycle netboot loop.
> Runtime V26 host soak harness ran a 3-cycle `soak-loop.sh` proof. Runtime V27
> panic/fault retained taxonomy proved `retained valid=1 kind=panic kind_id=1
> category=1 reason_id=1 ... reason=panic-test`, `retained valid=1 kind=fault
> kind_id=2 category=2 reason_id=2 ... reason=sync-fault`, and
> `symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault`. Runtime V28
> Swift runtime dependency audit proved `runtime ok=1 version=28 swift=6.3.2
> source_hooks=10 linked_hooks=2 heap_shims=5 linked_heap_shims=3
> required_symbols=5 audit=1`; host `runtime-audit.sh` reported
> `runtime-audit ok=1 ... missing=none`. Runtime V29 added the agent session
> health surface and proved `bootcert ok=1 version=29 agent=1 runtime=1 ... events_lost=0`,
> `agent ok=1 version=29 health=green bootcert=1 runtime=1 protocol=2 agent=1 events_lost=0`,
> and `agent-session ok=1 version=29 health=green bootcert=1 runtime=1 stress=1 soak=1 events_lost=0`
> across a clean 3-cycle netboot + agent-session loop. Runtime V30 added the
> substrate certificate and proved `bootcert ok=1 version=30 certificate=1 agent=1
> runtime=1 ... events_lost=0`, `certificate ok=1 version=30 substrate=1 bootcert=1
> agent=1 runtime=1 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1
> events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 ... events_lost=0`,
> and `certificate-loop ok=1 version=30 cycles=3 completed=3 substrate=1 bootcert=1
> agent=1 runtime=1 events_lost=0`. Runtime V31 added the fixed C-owned
> preemptive scheduler substrate over the existing cooperative executor and proved
> `bootcert ok=1 version=31 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`,
> `certificate ok=1 version=31 substrate=1 bootcert=1 scheduler=1 agent=1 runtime=1
> ... events_lost=0`, and `sched ok=1 version=31 active=1 cores=1 core=0
> interval_ticks=2700000 ticks=... irq_ticks=... preemptions=... runqueue=0/8
> enqueues=4 dequeues=4 selftest=1` across live netboot proof and a clean 3-cycle
> repeat. Runtime V32 released the A72 secondary cores through the default
> armstub8 spin-table slots and proved `bootcert ok=1 version=32 smp=1 scheduler=1
> certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate ok=1
> version=32 substrate=1 bootcert=1 smp=1 scheduler=1 agent=1 runtime=1
> ... events_lost=0`, and `cores ok=1 version=32 capacity=4 online=4 mask=0xf
> primary=0 release=0xe selftest=1 ...` with advancing secondary heartbeats across
> a clean 3-cycle live netboot repeat. Runtime V33 added the first Aether-owned
> cross-core synchronization surface and proved `bootcert ok=1 version=33
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1
> ... events_lost=0`, `certificate ok=1 version=33 substrate=1 bootcert=1
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`,
> `locks ok=1 version=33 atomics=1 spinlocks=1 acquisitions=2 contentions=0
> selftest=1`, and `runqueues ok=1 version=33 cores=4 capacity=8 total=0
> core0=0 core1=0 core2=0 core3=0 enqueues0=8 dequeues0=8 selftest=1` across a
> clean 3-cycle live netboot repeat. Runtime V34 added timer-driven SMP
> scheduler dispatch over those bounded per-core queues and proved `bootcert
> ok=1 version=34 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1
> scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate
> ok=1 version=34 substrate=1 bootcert=1 preemptive=1 smp_scheduler=1 atomics=1
> locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`, and
> `sched2 ok=1 version=34 preemptive=1 smp_scheduler=1 active=1 cores=4
> online=4 dispatches=548 routes=548 min=137 max=137 imbalance=0 core0=137
> core1=137 core2=137 core3=137 selftest=1`; a 3-cycle live repeat stayed
> balanced at `186/186/186/186`, `160/160/160/160`, and `157/157/157/157`.
> Runtime V35 added C-only secondary-owned scheduler workers and proved
> `bootcert ok=1 version=35 secondary_workers=1 preemptive=1 smp_scheduler=1
> atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1
> ... events_lost=0`, `certificate ok=1 version=35 substrate=1 bootcert=1
> secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1
> smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`, and `sched3 ok=1
> version=35 secondary_workers=1 active=1 cores=4 online=4 worker_drains=3
> worker_idles=1396994 min=1 max=1 imbalance=0 core0=0 core1=1 core2=1
> core3=1 selftest=1`; a 3-cycle repeat kept core0 at `0` and cores1-3 at
> `1/1/1`.
> Runtime V36 added timer-fed secondary scheduler workers and proved
> `bootcert ok=1 version=36 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1
> agent=1 runtime=1 ... events_lost=0`, `certificate ok=1 version=36
> substrate=1 bootcert=1 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1
> runtime=1 ... events_lost=0`, and `sched4 ok=1 version=36 worker_feed=1
> secondary_workers=1 feeds=708 drains=711 drops=0 gap=0 feed_imbalance=0
> drain_imbalance=0 core0_feed=0 core1_feed=236 core2_feed=236 core3_feed=236
> core0_drain=0 core1_drain=237 core2_drain=237 core3_drain=237 selftest=1`;
> a 3-cycle repeat stayed balanced at `693/696`, `684/687`, and `684/687`
> feeds/drains with `drops=0 gap=0`.
> Runtime V37 added timer-fed secondary C scheduler jobs and proved `bootcert
> ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1
> smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1
> agent=1 runtime=1 ... events_lost=0`, `certificate ok=1 version=37
> substrate=1 bootcert=1 job_exec=1 worker_feed=1 secondary_workers=1
> preemptive=1 smp_scheduler=1 ... events_lost=0`, and `sched5 ok=1
> version=37 job_exec=1 worker_feed=1 secondary_workers=1 executions=756
> completions=756 noops=0 checksum=698517273110 gap=0 imbalance=0
> core0_exec=0 core1_exec=252 core2_exec=252 core3_exec=252 core0_done=0
> core1_done=252 core2_done=252 core3_done=252 selftest=1`; a clean 3-cycle
> live repeat kept `sched5` at `738/738`, `699/699`, and `699/699`
> executions/completions with `noops=0 gap=0 imbalance=0`, `sched4` at
> `666/669`, `627/630`, and `630/633` feeds/drains with `drops=0 gap=0`, and
> `runqueues total=0`. Runtime V38 added the secondary scheduler wake protocol:
> core 0 emits bounded SEV wake signals when timer-fed secondary jobs are
> enqueued, and C-only secondary loops use WFE between scheduler work checks.
> Live proof showed `bootcert ok=1 version=38 wake=1 job_exec=1 worker_feed=1
> secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1
> smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`,
> `certificate ok=1 version=38 substrate=1 bootcert=1 wake=1 job_exec=1
> worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`,
> and `sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1 signals=825
> mask=0xe targets=825 waits=84020214 wakes=84028069 gap=1 imbalance=6818555
> core0_wait=0 core1_wait=30418819 core2_wait=23601879 core3_wait=30047539
> core0_wake=0 core1_wake=30432773 core2_wake=23612725
> core3_wake=30060963 selftest=1`. A clean 3-cycle live repeat kept
> `bootcert`/`certificate` at `wake=1`, `sched6 ok=1`, `runqueues total=0`,
> and `events_lost=0`. WFE wait/wake imbalance is telemetry, not a pass/fail
> gate; the A72 can resume WFE for architectural events beyond this scheduler
> SEV path.
> Runtime V39 added the secondary scheduler handoff protocol: core 0 records
> timer-fed secondary C job issues, while cores 1-3 record matching completion
> acknowledgements in their C-only scheduler loops. Live proof used image
> `kernel8.img` sha256
> `f24f26c85da4058853e5c7ec4af1822b7a77545259e492786c233421a45b831f`
> and passed a normal `net-iterate.sh` run plus a clean 3-cycle repeat. Proof
> lines included `bootcert ok=1 version=39 handoff=1 wake=1 job_exec=1
> worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`,
> `certificate ok=1 version=39 substrate=1 bootcert=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
> `sched7 ok=1 version=39 handoff=1 wake=1 job_exec=1 issued=885
> completed=885 gap=0 imbalance=0 core0_issue=0 core1_issue=295 core2_issue=295
> core3_issue=295 core0_done=0 core1_done=295 core2_done=295 core3_done=295
> selftest=1`. The repeat kept `runqueues total=0`, `resp id=40 ok=1
> cmd=sched7 end`, and `events_lost=0`.
> Runtime V40 added the scheduler backpressure protocol and proved bounded
> fixed-queue saturation on the live Pi. The accepted image hash was
> `46bc501a6513cf8a2187203c216be0dd6e54cf49223db3547f4d01578ca78372`.
> A normal `net-iterate.sh` run and clean 3-cycle repeat proved `bootcert ok=1
> version=40 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1
> secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=40
> substrate=1 bootcert=1 backpressure=1 handoff=1 wake=1 job_exec=1
> worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched8 ok=1
> version=40 backpressure=1 handoff=1 wake=1 high_water=8 overflows=16 total=0
> capacity=8 core0_high=8 core1_high=8 core2_high=8 core3_high=8
> core0_overflow=4 core1_overflow=4 core2_overflow=4 core3_overflow=4
> selftest=1`. The repeat kept `sched7 ok=1`, `runqueues total=0`, and
> `events_lost=0`.
> Runtime V41 secondary scheduler work-stealing protocol is hardware-accepted.
> Live Pi proof (2026-06-06, `kernel8.img` sha256
> `525a237533d0d1dbe0782f8c9a9d1c03a839676fc1cf4119b2625a41df0d78e0`) printed
> `bootcert ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate
> ok=1 version=41 substrate=1 bootcert=1 stealing=1 backpressure=1 handoff=1
> wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
> `sched9 ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1
> steals=4 completions=4 total=0 capacity=8 source_core1=4 source_core2=0
> source_core3=0 dest_core1=0 dest_core2=2 dest_core3=2 selftest=1`.
> Runtime V42 secondary scheduler load-balancing protocol is hardware-accepted.
> Live Pi proof (2026-06-06, `kernel8.img` sha256
> `a480b4c4e5df7ee114dc63bb0c17edf2dedaddd5a2f00033f5b8d96f527e2b97`) printed
> `bootcert ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate
> ok=1 version=42 substrate=1 bootcert=1 fairness=1 stealing=1 backpressure=1
> handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`,
> and `sched10 ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1
> wake=1 balances=3 completions=3 total=0 capacity=8 source_core1=3
> source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=1
> queue_imbalance=0 selftest=1`.
> Runtime V43 secondary scheduler priority/preemption protocol is hardware-accepted.
> Live Pi proof (2026-06-07, `kernel8.img` sha256
> `d2ddea45690c0b6180ab92c61dbc5ba26a9a01eb2d272ad3ae860f14a01b6610`) printed
> `bootcert ok=1 version=43 priority=1 fairness=1 stealing=1 backpressure=1
> handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`,
> `certificate ok=1 version=43 substrate=1 bootcert=1 priority=1 fairness=1
> stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1
> secondary_workers=1 ... events_lost=0`, and `sched11 ok=1 version=43
> priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1
> preemptions=2 yields=2 completions=4 total=0 capacity=8 low_core1=2
> high_core1=2 preempt_core1=2 yield_core1=2 selftest=1`.
> Runtime V44 bounded SMP concurrency soak protocol is hardware-accepted.
> Live Pi proof (2026-06-07, `kernel8.img` sha256
> `da94ea815b600951a82fc6ca46c23b679fb3362869f8a49aed1ceb00cd88a2cd`) printed
> `bootcert ok=1 version=44 concurrency=1 priority=1 fairness=1 stealing=1
> backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1
> ... events_lost=0`, `certificate ok=1 version=44 substrate=1 bootcert=1
> concurrency=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1
> job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
> `sched12 ok=1 version=44 concurrency=1 rounds=3 completions=3 failures=0
> dispatches=9 soak_core1=3 soak_core2=3 soak_core3=3 selftest=1`.

## What works (verified)

| Milestone | State | Verified how |
|-----------|-------|--------------|
| Toolchain → Mach-O `arm64-apple-none-macho` → `kernel8.img` | ✅ | `build.sh` uses Swift 6.3.2 + `macho2bin.py`; latest local build emits `kernel8.img` |
| PL011 UART0 driver + banner + `CurrentEL` readout | ✅ | **banner received over serial on real Pi 4** |
| EL2 → EL1 drop | ✅ | **`CurrentEL = 0x0000000000000004` read back over serial on hardware** |
| GPIO42 ACT-LED blink | historical ✅ | verified in earlier bring-up; current liveness is serial `rtv2 fast/slow/long` |
| GPIO14/15 → ALT0 in code (don't trust the overlay) | ✅ | disassembly `bfi w9,w8,#12,#6`; serial works on hardware |
| Generic timer (CNTP), polled 1 s tick | ✅ | `CNTFRQ = 54 MHz`; tick measured 1.0005 s mean on hardware |
| GIC-400 IRQ routing — CNTP (INTID 30) → EL1 vector → `wfi` idle | ✅ | **interrupt-driven `irq N` @ 1.0002 s mean on hardware; CPU idles in `wfi`** |
| Embedded Swift Runtime V2 async scheduler | ✅ | hardware run printed independent `rtv2 fast/slow/long` cadences; shared CNTP arbiter drives continuation sleeps + executor delays |
| Runtime V3 UART shell/control plane | ✅ | hardware run printed `shell ready`; `status`, `heap`, `queues`, and `tasks` returned machine-checkable `key=value` lines |
| Runtime V4 IRQ-backed UART RX shell | ✅ | hardware run printed `runtime v4: irq-backed uart shell`; `status`, `heap`, `queues`, and `tasks` returned over PL011 RX interrupts; `serial-reset.sh` rebooted back into netboot |
| Runtime V5 diagnostics shell | ✅ | hardware run printed `runtime v5: diagnostics shell`; `diag`, `irqs`, `timers`, `memcheck`, and `faults` returned machine-checkable lines; 3-cycle netboot loop passed |
| Runtime V6 retained panic/fault records | ✅ | hardware run printed `runtime v6: retained panic/fault records`; `panic-test` and `fault-test` watchdog-reset and the next boot reported `retained valid=1 kind=panic/fault` |
| Runtime V7 memory map + frame allocator | ✅ | hardware run printed `runtime v7: memory map + frame allocator`; `memmap` reported `valid=1 regions=7 page_size=4096`; `frames` reported `total=14336 free=14336 used=0 selftest=1`; 3-cycle netboot loop passed |
| Runtime V8 allocator/frame guardrails | ✅ | hardware run printed `runtime v8: allocator guardrails`; `heapcheck` reported `ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0`; `framecheck` reported `ok=1 total=14336 free=14336 used=0 stress=1`; 3-cycle netboot loop passed |
| Runtime V9 bounded memory pressure self-tests | ✅ | hardware run printed `runtime v9: bounded memory pressure self-tests`; `stress` reported `ok=1 heap=1 frames=1 heap_leak=0 frame_leak=0` |
| Runtime V10 explicit guard probes | ✅ | hardware run printed `runtime v10: explicit guard probes`; `frameprobe` reported `ok=1 last_ok=1`; destructive `heap-invalid-free-test` wrote retained `reason=heap-invalid-free` |
| Runtime V11 boot/soak invariants | ✅ | hardware run printed `runtime v11: boot and soak invariants`; `bootcheck` and `soak` reported `ok=1`; retained clear/readback survived after fixing 8-byte Swift heap-object dealloc |
| Runtime V12 kernel object/task registry | ✅ | hardware run printed `runtime v12: kernel object table + task registry`; `kobjects count=7 capacity=16 active=7 selftest=1`; `tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast` |
| Runtime V13 bounded mailbox queues | ✅ | hardware run printed `runtime v13: bounded mailbox message queues`, `rtv13 mail tx/rx`, `mailboxes count=2 capacity=4 queue_capacity=8 selftest=1`, and `sendtest ok=1` |
| Runtime V14 deterministic task supervisor | ✅ | hardware run printed `runtime v14: deterministic task supervisor`; `supervisor count=6 capacity=8 unhealthy=0 total_missed=0 selftest=1`; `health ok=1 supervised=6 unhealthy=0` |
| Runtime V15 capability-tagged kernel handles | ✅ | hardware run printed `runtime v15: capability-tagged kernel handles`; `handlecheck ok=1`; `kobjects count=11 capacity=16 active=11 selftest=1 handle_selftest=1 cap_selftest=1`; `capcheck ok=1 inspect=1 denied=1 stale=1` |
| Runtime V16 fixed event log ring | ✅ | hardware run printed `runtime v16: kernel event log ring`; `events count=11 capacity=64 lost=0 sequence=11 selftest=1`; event kinds included boot, supervisor, handle, task, timer, mailbox, shell, and selftest |
| Runtime V17 deterministic boot certificate | ✅ | hardware run printed `runtime v17: deterministic boot certificate`; `bootcert ok=1 version=17 memmap=1 heap=1 frames=1 kobjects=1 tasks=1 mailboxes=1 supervisor=1 events=1 events_lost=0`; 3-cycle netboot loop passed |
| Runtime V18 cooperative cancellation tokens | ✅ | hardware run printed `runtime v18: cooperative cancellation tokens`; `bootcert ok=1 version=18 ... cancellations=1 ... events_lost=0`; `canceltest ok=1 capacity=16 active=0 requested=1 completed=1`; 3-cycle netboot loop passed |
| Runtime V19 structured Aether task spawn | ✅ | hardware run printed `runtime v19: structured aether task spawn`; `bootcert ok=1 version=19 ... taskspawns=1 cancellations=1 ... events_lost=0`; `taskcheck ok=1 count=7 capacity=8 spawns=6 completions=0`; 3-cycle netboot loop passed |
| Runtime V20 bounded async channels | ✅ | hardware run printed `runtime v20: bounded async channels`; `bootcert ok=1 version=20 ... channels=1 taskspawns=1 cancellations=1 ... events_lost=0`; `channeltest ok=1 mailbox=1 sent=1 received=1 value=0x000000000000c020`; 3-cycle netboot loop passed |
| Runtime V21 MMU ownership boundary | ✅ | hardware run printed `runtime v21: mmu ownership boundary`; `bootcert ok=1 version=21 ... mmu=1 ... events_lost=0`; `mmu ok=1 regions=4 entries=512 block_size=0x40000000 ... selftest=1`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V22 guarded typed pools | ✅ | hardware run printed `runtime v22: guarded typed pools`; `bootcert ok=1 version=22 ... pools=1 ... mmu=1 ... events_lost=0`; `poolcheck ok=1 ... bad_frees=1 double_frees=1`; `pools count=3 capacity=4 selftest=1`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V23 allocator/pool pressure telemetry | ✅ | hardware run printed `runtime v23: allocator and pool pressure telemetry`; `bootcert ok=1 version=23 ... pressure=1 pools=1 ... events_lost=0`; `heapfrag ok=1 ... fragmentation_permil=0 ... pressure_largest_free=4184112`; `poolstats ok=1 ... total_slots=24 ... failed_allocs=1`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V24 fixed driver registry | ✅ | hardware run printed `runtime v24: fixed driver registry`; `bootcert ok=1 version=24 ... drivers=1 pressure=1 pools=1 ... events_lost=0`; `drivers count=4 capacity=4 selftest=1`; `drivercheck ok=1 ... uart_irq=16 timer_irq=689 gic_total=705 watchdog_resets=0 unknown_irq=0 selftest=1`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V25 scriptable command protocol v2 | ✅ | hardware run printed `runtime v25: scriptable command protocol v2`; `protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1 errors=1 max_line=80`; `bootcert ok=1 version=25 protocol=1 ... drivers=1 pressure=1 pools=1 ... events_lost=0`; `resp id=25 ok=1 cmd=status end`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V26 host soak harness | ✅ | host-side `soak-loop.sh` ran 3 netboot cycles, then harvested request-wrapped `status`, `bootcert`, `stress`, `soak`, and `events` summaries; proof ended `soak result ok=1 cycles=3 completed=3` |
| Runtime V27 panic taxonomy and symbolic retained records | ✅ | hardware run printed `runtime v27: panic taxonomy and symbolic retained records`; `bootcert ok=1 version=27 taxonomy=1 ... events_lost=0`; `panic-test` reported `retained valid=1 kind=panic kind_id=1 category=1 reason_id=1`; `fault-test` reported `kind_id=2 category=2 reason_id=2`; `symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault`; clean `set -e` 3-cycle netboot loop passed |
| Runtime V28 Swift runtime dependency audit | ✅ | hardware run printed `runtime v28: swift runtime dependency audit`; `bootcert ok=1 version=28 runtime=1 taxonomy=1 ... events_lost=0`; `runtime ok=1 version=28 swift=6.3.2 source_hooks=10 linked_hooks=2 heap_shims=5 linked_heap_shims=3 required_symbols=5 audit=1`; host `runtime-audit ok=1 ... missing=none` |
| Runtime V29 agent-oriented control session | ✅ | hardware run printed `runtime v29: agent-oriented control session`; `bootcert ok=1 version=29 agent=1 runtime=1 taxonomy=1 ... events_lost=0`; `agent ok=1 version=29 health=green bootcert=1 runtime=1 protocol=2 agent=1 events_lost=0`; `agent-session ok=1 version=29 health=green ... events_lost=0`; clean `set -e` 3-cycle netboot + agent-session loop passed |
| Runtime V30 Swift-native kernel substrate certificate | ✅ | hardware run printed `runtime v30: swift-native kernel substrate certificate`; `bootcert ok=1 version=30 certificate=1 agent=1 runtime=1 taxonomy=1 ... events_lost=0`; `certificate ok=1 version=30 substrate=1 bootcert=1 agent=1 runtime=1 memory=1 objects=1 tasks=1 mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1 drivers=1 pressure=1 pools=1 mmu=1 ... events_lost=0`; `certificate-loop ok=1 version=30 cycles=3 completed=3 ... events_lost=0` |
| Runtime V31 preemptive scheduler substrate | ✅ | hardware run printed `runtime v31: preemptive scheduler substrate`; `bootcert ok=1 version=31 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=31 substrate=1 bootcert=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `sched ok=1 version=31 active=1 cores=1 core=0 interval_ticks=2700000 ticks=152 irq_ticks=152 preemptions=152 runqueue=0/8 enqueues=4 dequeues=4 selftest=1`; clean 3-cycle live netboot repeat passed |
| Runtime V32 SMP secondary-core bring-up | ✅ | hardware run printed `runtime v32: smp secondary-core bring-up`; `bootcert ok=1 version=32 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=32 substrate=1 bootcert=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `cores ok=1 version=32 capacity=4 online=4 mask=0xf primary=0 release=0xe selftest=1 core0=1 core1=1 core2=1 core3=1`; paired `cores` samples showed secondary heartbeats advancing |
| Runtime V33 atomics, spinlocks, and per-core run queues | ✅ | hardware run printed `runtime v33: atomics spinlocks per-core run queues`; `bootcert ok=1 version=33 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=33 substrate=1 bootcert=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `locks ok=1 version=33 atomics=1 spinlocks=1 acquisitions=2 contentions=0 selftest=1`; `runqueues ok=1 version=33 cores=4 capacity=8 total=0 core0=0 core1=0 core2=0 core3=0 enqueues0=8 dequeues0=8 selftest=1`; clean 3-cycle live netboot repeat passed |
| Runtime V34 timer-driven SMP scheduler dispatch | ✅ | hardware run printed `runtime v34: timer-driven smp scheduler dispatch`; `bootcert ok=1 version=34 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=34 substrate=1 bootcert=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `sched2 ok=1 version=34 preemptive=1 smp_scheduler=1 active=1 cores=4 online=4 dispatches=548 routes=548 min=137 max=137 imbalance=0 core0=137 core1=137 core2=137 core3=137 selftest=1`; clean 3-cycle live netboot repeat passed with balanced dispatch counters |
| Runtime V35 secondary-owned scheduler workers | ✅ | hardware run printed `runtime v35: secondary-owned scheduler workers`; `bootcert ok=1 version=35 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=35 substrate=1 bootcert=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `sched3 ok=1 version=35 secondary_workers=1 active=1 cores=4 online=4 worker_drains=3 worker_idles=1396994 min=1 max=1 imbalance=0 core0=0 core1=1 core2=1 core3=1 selftest=1`; clean 3-cycle live netboot repeat passed |
| Runtime V36 timer-fed secondary scheduler workers | ✅ | hardware run printed `runtime v36: timer-fed secondary scheduler workers`; `bootcert ok=1 version=36 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=36 substrate=1 bootcert=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`; `sched4 ok=1 version=36 worker_feed=1 secondary_workers=1 feeds=708 drains=711 drops=0 gap=0 feed_imbalance=0 drain_imbalance=0 core0_feed=0 core1_feed=236 core2_feed=236 core3_feed=236 core0_drain=0 core1_drain=237 core2_drain=237 core3_drain=237 selftest=1`; clean 3-cycle live netboot repeat passed with `drops=0 gap=0` |
| Runtime V37 timer-fed secondary C scheduler jobs | ✅ | hardware run printed `runtime v37: timer-fed secondary C scheduler jobs`; `bootcert ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=37 substrate=1 bootcert=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`; `sched5 ok=1 version=37 job_exec=1 worker_feed=1 secondary_workers=1 executions=756 completions=756 noops=0 checksum=698517273110 gap=0 imbalance=0 core0_exec=0 core1_exec=252 core2_exec=252 core3_exec=252 core0_done=0 core1_done=252 core2_done=252 core3_done=252 selftest=1`; clean 3-cycle live netboot repeat passed with `noops=0 gap=0 imbalance=0` |
| Runtime V38 secondary scheduler wake protocol | ✅ | hardware run printed `runtime v38: secondary scheduler wake protocol`; `bootcert ok=1 version=38 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1 runtime=1 ... events_lost=0`; `certificate ok=1 version=38 substrate=1 bootcert=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`; `sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1 signals=825 mask=0xe targets=825 waits=84020214 wakes=84028069 gap=1 imbalance=6818555 core0_wait=0 core1_wait=30418819 core2_wait=23601879 core3_wait=30047539 core0_wake=0 core1_wake=30432773 core2_wake=23612725 core3_wake=30060963 selftest=1`; clean 3-cycle live netboot repeat passed with `wake=1`, positive core1-3 wait/wake counts, `runqueues total=0`, and `events_lost=0` |
| Runtime V39 secondary scheduler handoff protocol | ✅ | hardware run printed `runtime v39: secondary scheduler handoff protocol`; `bootcert ok=1 version=39 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `certificate ok=1 version=39 substrate=1 bootcert=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `sched7 ok=1 version=39 handoff=1 wake=1 job_exec=1 issued=885 completed=885 gap=0 imbalance=0 core0_issue=0 core1_issue=295 core2_issue=295 core3_issue=295 core0_done=0 core1_done=295 core2_done=295 core3_done=295 selftest=1`; clean 3-cycle live netboot repeat passed |
| Runtime V40 scheduler backpressure protocol | ✅ | hardware run printed `runtime v40: scheduler backpressure protocol`; `bootcert ok=1 version=40 backpressure=1 handoff=1 wake=1 ... events_lost=0`; `certificate ok=1 version=40 substrate=1 bootcert=1 backpressure=1 handoff=1 wake=1 ... events_lost=0`; `sched8 ok=1 version=40 backpressure=1 handoff=1 wake=1 high_water=8 overflows=16 total=0 capacity=8 core0_high=8 core1_high=8 core2_high=8 core3_high=8 core0_overflow=4 core1_overflow=4 core2_overflow=4 core3_overflow=4 selftest=1`; clean 3-cycle live netboot repeat passed |
| Runtime V41 secondary scheduler work-stealing protocol | ✅ | hardware run printed `runtime v41: secondary scheduler work stealing`; `bootcert ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `certificate ok=1 version=41 substrate=1 bootcert=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `sched9 ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1 steals=4 completions=4 total=0 capacity=8 source_core1=4 source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=2 selftest=1`; `runqueues ok=1 version=33 cores=4 capacity=8 total=0`; `events count=41 capacity=64 lost=0`; clean 3-cycle live netboot repeat passed |
| Runtime V42 secondary scheduler load-balancing protocol | ✅ | hardware run printed `runtime v42: secondary scheduler load balancing`; `bootcert ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `certificate ok=1 version=42 substrate=1 bootcert=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `sched10 ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 balances=3 completions=3 total=0 capacity=8 source_core1=3 source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=1 queue_imbalance=0 selftest=1`; `runqueues ok=1 version=33 cores=4 capacity=8 total=0`; clean 3-cycle live netboot repeat passed |
| Runtime V43 secondary scheduler priority/preemption protocol | ✅ | hardware run printed `runtime v43: secondary scheduler priority preemption`; `bootcert ok=1 version=43 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `certificate ok=1 version=43 substrate=1 bootcert=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `sched11 ok=1 version=43 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 preemptions=2 yields=2 completions=4 total=0 capacity=8 low_core1=2 high_core1=2 preempt_core1=2 yield_core1=2 selftest=1`; `net-iterate.sh` passed all shell probes on 2026-06-07 |
| Runtime V44 bounded SMP concurrency soak protocol | ✅ | hardware run printed `runtime v44: bounded smp concurrency soak`; `bootcert ok=1 version=44 concurrency=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `certificate ok=1 version=44 substrate=1 bootcert=1 concurrency=1 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`; `sched12 ok=1 version=44 concurrency=1 rounds=3 completions=3 failures=0 dispatches=9 soak_core1=3 soak_core2=3 soak_core3=3 selftest=1`; `soak ok=1 rounds=3 failures=0 heap_leak=0 frame_leak=0`; Wemo cold-cycle `netboot-auto.sh` + 3-cycle `soak-loop.sh` passed on 2026-06-07 |
| EL1 exception vectors | ✅ | IRQ slot `0x280` → `irq_entry` exercised on hardware; sync `brk` path captured ESR/ELR/FAR and rebooted through the retained fault record |

First hardware boot: 2026-06-04. The one trap worth recording — serial was
silent until the FT232 **RX** was moved to header **pin 8** (GPIO14/Pi-TXD); a
classic RX/TX crossover mistake, not a kernel bug.

## Toolchain reality (why the build looks unusual)

- Built with **`swift-6.3.2-RELEASE`** and the **`arm64-apple-none-macho`**
  triple because the Embedded `_Concurrency` archive exists there, not for
  `aarch64-none-none-elf`.
- **No swift-mmio.** Its macros pull in swift-syntax, which older Embedded toolchains
  can't compile against macOS SDK 26 (`_DarwinFoundation1` ABI break). MMIO is
  done through a tiny C `volatile` shim (`Sources/Support/include/Support.h`)
  instead — guaranteed correct peripheral semantics, zero macro fragility.
- SwiftPM uses `--toolset Toolsets/rpi4-macho.json`; the toolset pins the boot,
  text, and data segments and force-loads Embedded `_Concurrency`.
- `macho2bin.py` extracts `__BOOT,__TEXT,__DATA`, rejects unexpected runtime
  segments, and refuses images that would overlap the heap base at `0x400000`.

## Quickstart

```sh
./build.sh                 # -> kernel8.img
./flash.sh /Volumes/bootfs # copy kernel8.img + config.txt to the SD boot part
# then: screen /dev/cu.usbserial-XXXX 115200   (see RUNBOOK.md for wiring)
```

For the current bench setup (USB-TTL serial already logging and Ethernet wired),
the faster iteration path is Pi 4 EEPROM netboot over the direct Mac-Pi Ethernet
link:

```sh
./prepare-tftp.sh --download
./serve-netboot.sh en0      # foreground repo-owned TFTP server
./netboot-doctor.sh        # guided first netboot: prompts for one reset, verifies
./net-iterate.sh           # build, stage, serial-reset, verify TFTP + serial
```

See `RUNBOOK.md` for the required one-time EEPROM config. Keep `flash.sh` as the
SD recovery path. The exact Pi 4 bootloader settings live in
`netboot-eeprom-config.txt`. `serve-netboot.sh` defaults to `aether_tftp.py`
with 1468-byte blocks and single-port duplicate-RRQ handling because the Pi 4
firmware emits `Early terminate` and retries some files from a new UDP source
port on this bench. Homebrew `dnsmasq` remains an explicit fallback via
`AETHER_TFTP_PROVIDER=dnsmasq`.

## Layout

```
Sources/Support/boot.S        _start: release secondaries, EL2->EL1 drop, VBAR, ->main
Sources/Support/vectors.S     16-entry EL1 vector table -> common syndrome handler
Sources/Support/include/      C volatile MMIO shim (mmio_read32/write32, nop, CurrentEL)
Sources/Application/UART.swift PL011 driver (init/putc/puts/puthex)
Sources/Application/GPIO.swift UART pin mux + historical ACT-LED helpers
Sources/Application/Exceptions.swift  prints machine-checkable sync fault lines + ESR/ELR/FAR
Sources/Application/TimerSleep.swift   8-slot CNTP-backed async continuation sleep
Sources/Application/UARTRX.swift       Runtime V4 IRQ-backed UART RX async byte bridge
Sources/Application/AetherTask.swift   Runtime V19 structured task registration/spawn helper
Sources/Application/AetherChannel.swift Runtime V20 Swift async channel wrapper over mailboxes
Sources/Application/UARTShell.swift    Runtime V25 line shell + request/response protocol over UART RX
Sources/Application/Application.swift  @main: banner, CurrentEL, Runtime V25 async cadences + shell
Sources/Support/kernel_registry.c     Runtime V12 fixed object/task registry
Sources/Support/kernel_mailbox.c      Runtime V13 fixed mailbox queues
Sources/Support/kernel_supervisor.c   Runtime V14 fixed task supervisor
Sources/Support/kernel_event_log.c    Runtime V16 fixed event log ring
Sources/Support/kernel_cancel.c       Runtime V18 fixed cancellation token table
Sources/Support/alloc.c               Runtime V23 fixed heap allocator + guard/pressure telemetry
Sources/Support/kernel_driver.c       Runtime V24 fixed driver registry + stats surface
Sources/Support/diagnostics.c         Runtime V6 IRQ/fault/panic counters + retained reset record
Sources/Support/memory_map.c          Runtime V11 fixed memory map + guarded 4 KiB frame allocator
Sources/Support/mmu.c                 Runtime V21 static EL1 MMU table + read-only introspection
Sources/Support/kernel_pool.c         Runtime V22 guarded typed pools
build.sh / flash.sh / netboot-doctor.sh / netflash.sh / net-iterate.sh
prepare-tftp.sh / serve-netboot.sh / serial-reset.sh / serial-command.sh / serial-probe.sh / soak-loop.sh
macho2bin.py / aether_tftp.py / config.txt / netboot-eeprom-config.txt / RUNBOOK.md
```

## Roadmap (next, once it boots)

> **The forward-looking roadmap now lives in [`ROADMAP.md`](ROADMAP.md)** — the
> far-horizon North Star ("general-purpose OS on the Pi 4") and the dependency-ordered
> epic ladder (user mode → processes → storage → display → USB → networking) that
> autonomous agents climb after V44. The list below is the original boot-bring-up
> roadmap, kept for history; items 1–4 are all hardware-verified.

1. ~~Confirm boot on hardware: banner + `CurrentEL = 0x0000000000000004` (EL1) over serial.~~ ✅ 2026-06-04
2. ~~Generic timer tick (CNTP) → a real periodic heartbeat instead of a busy delay.~~ ✅ 2026-06-04 (polled, 1 s @ 54 MHz)
3. ~~GIC-400 IRQ routing (turns the polled timer into a true interrupt; first use of the vector table).~~ ✅ 2026-06-04 (interrupt-driven, `wfi` idle)
4. **The Embedded-Swift concurrency experiment (custom executor).** 🏆 **`async`/`await`
   running on bare metal — hardware-verified 2026-06-04, expanded to Runtime V2 on 2026-06-05.**
   Swift `async Task`s are scheduled by our own C cooperative executor on the real Pi 4; Runtime V2
   prints independent `rtv2 fast/slow/long` cadences using one shared CNTP timer arbiter.
   - Foundation: migrated ELF → `arm64-apple-none-macho` (swift-6.3.2) to get
     `_Concurrency` (not built for `aarch64-none-none-elf`); MS1–3 re-verified on hardware.
   - **Stage 1 — heap allocator** (`Sources/Support/alloc.c`): first-fit free list +
     boundary-tag coalescing. ✅ hardware-verified — freed-slot reuse (`c == a`),
     4096-aligned `posix_memalign`.
   - **Stage 2 — executor + runtime integration.** ✅ hardware-verified. Plain-C `…Impl` hooks
     (`SWIFT_CC(swift)`, `executor.c`), ready ring, NORETURN drain pump,
     `swift_slowAlloc/Dealloc` + libc shims (`libc_shims.c`), `-force_load libswift_Concurrency.a`
     (DefaultExecutor NOT linked). Two bring-up requirements the runtime forced, both in `boot.S`:
     **CPACR_EL1.FPEN** (the runtime uses FP/NEON) and **the MMU** (`mmu.c`, identity-mapped Normal
     cacheable RAM) — without the MMU, Cortex-A72 has no exclusive monitor on Device memory and
     `swift_task_create`'s `ldxr/stxr` CAS loop spins forever.
   - **Stage 3 — timer-backed async sleep + heartbeat.** ✅ hardware-verified. `Task.sleep` is
     unavailable in Embedded Swift, so suspension is hand-rolled with `withUnsafeContinuation`
     (`TimerSleep.swift`), resumed by the CNTP timer IRQ; the CNTP register ops live in non-inline C
     (`timersleep_hw.c`). Runtime V2 replaces the single-sleeper path with an 8-slot continuation
     sleep queue and routes executor delay/deadline hooks through the same timer arbiter. Hardware proof:
     fresh netboot printed `rtv2 fast 0`, `rtv2 slow 0`, and `rtv2 long 0`; CPU idles in `wfi` between jobs.
     Bonus: `watchdog.c` (BCM2711 PM reset) — hardware-verified self-reboot.
     See `CONCURRENCY_DESIGN.md` (GROUND TRUTH block) for the verified symbol/ABI contract.
   - **Runtime V3 — async UART control plane.** ✅ hardware-verified. A dedicated async shell task
     polls PL011 RX every 25 ms and accepts line commands: `help`, `status`, `heap`, `queues`,
     `tasks`, and `reboot`; `r`/`R` remain watchdog-reset aliases for the netboot loop. Hardware proof:
     fresh netboot printed `shell ready commands=help,status,heap,queues,tasks,reboot`, and
     `serial-command.sh` produced `status`, `heap`, `queues`, and `tasks` response lines.
   - **Runtime V4 — IRQ-backed UART RX.** ✅ hardware-verified. PL011 RX/receive-timeout interrupts
     drain into a fixed C byte ring, route through GIC INTID 153 to CPU0, and wake a single Swift
     async shell waiter. Hardware proof: fresh netboot printed `runtime v4: irq-backed uart shell`;
     `serial-command.sh status`, `heap`, `queues`, and `tasks` returned response lines while
     `rtv2 fast/slow/long` cadences continued; `serial-reset.sh` rebooted back into netboot.
   - **Runtime V5 — diagnostics and fault/IRQ self-inspection.** ✅ hardware-verified. The UART
     shell now accepts `diag`, `irqs`, `timers`, `memcheck`, `faults`, `panic-test`, and
     `fault-test` in addition to the V3 commands. Safe proof commands returned `diag version=v5`,
     `irqs total=`, `timers now=`, `memcheck ok=1`, and `faults seen=0` while async cadences
     continued. `panic-test` and `fault-test` are intentionally destructive and are not part of
     the normal liveness proof.
   - **Runtime V6 — retained panic/fault records across watchdog reset.** ✅ hardware-verified.
     A fixed retained record page below the heap stores panic/fault kind, sequence, ESR/ELR/FAR,
     and a short reason. Because RAM is cacheable after MMU bring-up, retained writes explicitly
     clean their D-cache lines (`dc cvac` + `dsb sy`) before watchdog reset. Hardware proof:
     `panic-test` rebooted and the next boot reported `retained valid=1 kind=panic ... reason=panic-test`;
     `fault-test` rebooted and the next boot reported `retained valid=1 kind=fault esr=0xf20000a5 ... reason=sync-fault`.
   - **Runtime V7 — memory map and frame allocator invariants.** ✅ hardware-verified.
     The kernel now exposes a fixed low-memory ownership map, reserves the retained page and existing
     heap window explicitly, and manages a conservative 4 KiB physical-frame window from `0x00800000`
     to `0x04000000` with fixed bitmap storage. Hardware proof: fresh netboot printed
     `runtime v7: memory map + frame allocator`; `memmap` returned `valid=1 regions=7 page_size=4096`;
     `frames` returned `total=14336 free=14336 used=0 reserved=0 base=0x800000 limit=0x4000000 selftest=1`;
     a 3-cycle `net-iterate.sh` loop passed.
   - **Runtime V8 — allocator and frame guardrails.** ✅ hardware-verified.
     Real allocator misuse now fails loudly before metadata mutation: `free`/`realloc` validate
     heap pointers, detect double frees, preserve stable `HEAP_GUARD_*` reason codes, count
     invalid/double/corruption events, and poison freed payloads. The frame allocator tracks
     bad frees and double frees and exposes a fixed-storage stress selftest that allocates and
     returns four frames. Hardware proof: fresh netboot printed `runtime v8: allocator guardrails`;
     `heapcheck` returned `ok=1 error=0 invalid_frees=0 double_frees=0 corruptions=0`;
   `framecheck` returned `ok=1 total=14336 free=14336 used=0 bad_frees=0 double_frees=0 error=0 stress=1`;
   a 3-cycle `net-iterate.sh` loop passed.
  - **Runtime V9 — bounded memory pressure self-tests.** ✅ hardware-verified.
    The shell `stress` command runs fixed-size heap and frame pressure loops, records peak/leak counters,
    and avoids dynamic allocation in the test harness. Hardware proof: `stress ok=1 heap=1 frames=1
    heap_peak=62928 frame_peak=16 heap_leak=0 frame_leak=0`.
  - **Runtime V10 — explicit guard probes.** ✅ hardware-verified.
    Non-destructive `frameprobe` verifies bad-frame and double-frame frees are counted without changing
    final frame ownership. Destructive heap guard commands intentionally panic so retained records prove
    the allocator fails loudly. Hardware proof: `frameprobe ok=1 last_ok=1 ...`; `heap-invalid-free-test`
    rebooted and retained `reason=heap-invalid-free`.
  - **Runtime V11 — boot and soak invariants.** ✅ hardware-verified.
    Startup and shell `bootcheck` report memory-map, heap, frame, and retained-record health; `soak` runs
    repeated bounded pressure rounds. `net-iterate.sh` now probes `status`, `bootcheck`, `stress`, and
    `soak` by default. During V11 proof, retained read/clear exposed a Swift embedded heap-object
    deallocation mismatch: this toolchain's `_swift_allocObject` calls `posix_memalign` with an 8-byte
    floor and later calls `free(object)` directly. The allocator now accepts 8-byte-aligned
    `posix_memalign` payloads while keeping header/footer validation strict. Hardware proof after the fix:
    `retained clear ok=1`, `retained valid=0`, and `bootcheck ok=1 ... retained_valid=0`.
  - **Runtime V12 — kernel object table and task registry.** ✅ hardware-verified.
    A fixed C-owned object table names runtime/driver/task records, and a fixed cooperative task
    registry tracks demo task state, period, object id, and tick counters. Hardware proof:
    `kobjects count=7 capacity=16 active=7 selftest=1` and
    `tasks2 count=4 capacity=8 selftest=1 task index=0 name=fast`.
  - **Runtime V13 — bounded mailbox message queues.** ✅ hardware-verified.
    Fixed C-owned UInt64 mailbox queues register as kernel objects and expose queue depth, sent,
    received, drop, and error counters. Two Swift async demo tasks exchange values through the
    demo mailbox and print `rtv13 mail tx/rx`; shell commands `mailboxes` and `sendtest` provide
    machine-checkable proof. Hardware proof: `mailboxes count=2 capacity=4 queue_capacity=8
    selftest=1` and `sendtest ok=1 mailbox=1 sent=1 received=1`.
  - **Runtime V14 — deterministic task supervisor.** ✅ hardware-verified.
    A fixed C-owned supervisor table watches V12 task IDs, tracks heartbeat deadlines/misses, and
    exposes observe/panic policy fields. The normal proof loop uses observe-mode records and checks
    `supervisor` plus `health`; panic policy is available for future destructive tests. Hardware
    proof: `supervisor count=6 capacity=8 unhealthy=0 total_missed=0 selftest=1` and
    `health ok=1 supervised=6 unhealthy=0 total_missed=0`.
  - **Runtime V15 — capability-tagged kernel handles.** ✅ hardware-verified.
    Kernel objects now expose raw 64-bit handles encoding slot, generation, kind, and granted
    capability mask. Lookups reject stale generations and denied capabilities with stable error
    codes; `kobjects` prints handles/generations/cap masks and `capcheck` proves inspect,
    denied-control, and stale-handle paths. Hardware proof: `handlecheck ok=1`,
    `kobjects count=11 capacity=16 active=11 selftest=1 handle_selftest=1 cap_selftest=1`,
    `object index=0 ... handle=0x0000000103000101 generation=1`, and
    `capcheck ok=1 inspect=1 denied=1 stale=1 last_error=2`.
  - **Runtime V16 — fixed event log ring.** ✅ hardware-verified.
    A fixed 64-record C ring stores coarse subsystem events with monotonic sequence,
    CNTP ticks, stable kind names, three raw args, and an overwrite lost counter.
    The shell `events` command exposes recent boot, supervisor, handle, task, timer,
    mailbox, shell, and selftest events. Hardware proof: `events count=11 capacity=64
    lost=0 sequence=11 selftest=1`.
  - **Runtime V17 — deterministic boot certificate.** ✅ hardware-verified.
    The shell `bootcert` command aggregates the live memory-map, heap guard, frame
    allocator, object/task registry, mailbox, supervisor, and event-log selftests
    into one machine-checkable line. Retained-record validity is reported but does
    not fail the certificate, because destructive retained diagnostics are allowed
    to leave a prior reset record. Hardware proof: `bootcert ok=1 version=17
    memmap=1 heap=1 frames=1 retained_valid=0 kobjects=1 tasks=1 mailboxes=1
    supervisor=1 events=1 events_lost=0`, and a 3-cycle `net-iterate.sh` loop
    passed with events still reporting `lost=0`.
  - **Runtime V18 — cooperative cancellation tokens.** ✅ hardware-verified.
    A fixed 16-record C-owned token table exposes generation-tagged cancellation
    tokens with active, cancelled, and completed states. The normal proof path
    runs a deterministic `canceltest` selftest without heap allocation, registers
    the cancellation subsystem in the task/supervisor surfaces, and extends
    `bootcert` with `cancellations=1`. Hardware proof: `bootcert ok=1 version=18
    ... cancellations=1 ... events_lost=0`, `canceltest ok=1 capacity=16 active=0
    requested=1 completed=1`, supervisor count `7`, `events count=15 capacity=64
    lost=0 sequence=15 selftest=1`, and a 3-cycle `net-iterate.sh` loop passed.
  - **Runtime V19 — structured Aether task spawn.** ✅ hardware-verified.
    `AetherTask.swift` is now the Swift-owned spawn boundary: task registration
    records parent IDs, supervisor policy, and fixed spawn/completion counters
    before launching Embedded Swift `Task`s. The shell `tasks2` output includes
    parent, handle, spawn, and completion metadata, and `taskcheck` summarizes
    the current task substrate. Hardware proof: `bootcert ok=1 version=19 ...
    taskspawns=1 cancellations=1 ... events_lost=0`, `taskcheck ok=1 count=7
    capacity=8 spawns=6 completions=0`, `events count=16 capacity=64 lost=0
    sequence=16 selftest=1`, and a 3-cycle `net-iterate.sh` loop passed
    (cycle 1 attempt 2 after stale SD fallback, cycles 2 and 3 attempt 1).
  - **Runtime V20 — bounded async channels.** ✅ hardware-verified.
    `AetherChannel.swift` wraps the fixed C-owned mailbox queues in a small
    Swift `AetherChannelU64` API with nonblocking send/tryReceive and an async
    receive loop. The demo mail producer/consumer now use the wrapper instead
    of direct mailbox calls, and the shell adds `channeltest` plus bootcert
    `channels=1`. Hardware proof: single `net-iterate.sh` passed attempt 1,
    then a 3-cycle loop passed attempt 1 for all cycles. Proof lines included
    `runtime v20: bounded async channels`, `bootcert ok=1 version=20 ...
    channels=1 taskspawns=1 cancellations=1 ... events_lost=0`,
    `channeltest ok=1 mailbox=1 sent=1 received=1 value=0x000000000000c020`,
    `kobjects count=12 capacity=16 active=12`, and `events count=17 capacity=64
    lost=0 sequence=17 selftest=1`.
  - **Runtime V21 — MMU ownership boundary.** ✅ hardware-verified.
    `MMU_OWNERSHIP.md` records the current EL1 stage-1 identity map and the
    future remap invariants before adding isolation. `mmu.c` now exposes
    read-only region/table introspection: four live 1 GiB L1 block entries
    cover the low 4 GiB address window, with the final block marked Device for
    BCM2711 low peripherals; entries 4-511 remain faults. The shell adds `mmu`,
    and `bootcert` reports `mmu=1`. Hardware proof: a single `net-iterate.sh`
    run passed, then a clean `set -e` 3-cycle loop passed all cycles. Proof
    lines included `runtime v21: mmu ownership boundary`, `bootcert ok=1
    version=21 ... mmu=1 ... channels=1 ... events_lost=0`, and `mmu ok=1
    regions=4 entries=512 block_size=0x40000000 ... selftest=1`.

  - **Runtime V22 — guarded typed pools.** ✅ hardware-verified.
    `kernel_pool.c` adds fixed C-owned typed pools beside the heap, with guard
    words, generation counters, high-water/counter telemetry, deterministic
    bad-free/double-free/overflow selftests, and shell visibility via `pools`
    and `poolcheck`. Hardware proof: a single `net-iterate.sh` run passed,
    then a clean `set -e` 3-cycle loop passed all cycles. Proof lines included
    `runtime v22: guarded typed pools`, `bootcert ok=1 version=22 ... pools=1
    ... mmu=1 ... events_lost=0`, `poolcheck ok=1 ... bad_frees=1
    double_frees=1`, and `pools count=3 capacity=4 selftest=1`.

  - **Runtime V23 — allocator/pool pressure telemetry.** ✅ hardware-verified.
    `alloc.c` now exposes heap free/allocated block counts, smallest/largest
    free block size, fragmentation permille, and last pressure-run snapshots.
    `kernel_pool.c` adds aggregate pool slot pressure counters. The shell adds
    `heapfrag` and `poolstats`, and `bootcert` reports `pressure=1`. Hardware
    proof: a single `net-iterate.sh` run passed, then a clean `set -e` 3-cycle
    loop passed all cycles. Proof lines included `runtime v23: allocator and
    pool pressure telemetry`, `bootcert ok=1 version=23 ... pressure=1 pools=1
    ... events_lost=0`, `heapfrag ok=1 ... fragmentation_permil=0 ...
    pressure_largest_free=4184112`, and `poolstats ok=1 ... total_slots=24 ...
    failed_allocs=1`.

  - **Runtime V24 fixed driver registry.** ✅ hardware-verified.
    `kernel_driver.c` adds a fixed C-owned registry for the current UART0,
    CNTP, GIC, and watchdog drivers. It registers each as a kernel object and
    exposes stable IDs, names, handles, INTIDs, base addresses, capabilities,
    IRQ counters, error counters, and operation counters. The shell adds
    `drivers` and `drivercheck`, and `bootcert` reports `drivers=1`.
    Hardware proof: a single `net-iterate.sh` run passed, then a clean
    `set -e` 3-cycle loop passed all cycles. Proof lines included
    `runtime v24: fixed driver registry`, `bootcert ok=1 version=24 ...
    drivers=1 pressure=1 pools=1 ... events_lost=0`, `drivers count=4
    capacity=4 selftest=1`, and `drivercheck ok=1 ... uart_irq=16
    timer_irq=689 gic_total=705 watchdog_resets=0 unknown_irq=0 selftest=1`.

  - **Runtime V25 scriptable command protocol v2.** ✅ hardware-verified.
    The UART shell keeps legacy human commands, but also accepts request-wrapped
    commands shaped as `req id=<n> cmd=<command>` and emits stable begin/end or
    error response envelopes for agent control. The `protocol` command describes
    the wire contract, and `bootcert` reports `protocol=1`. Hardware proof: a
    single `net-iterate.sh` run passed, then a clean `set -e` 3-cycle loop passed
    all cycles. Proof lines included `runtime v25: scriptable command protocol
    v2`, `protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1
    errors=1 max_line=80`, `bootcert ok=1 version=25 protocol=1 ... drivers=1
    pressure=1 pools=1 ... events_lost=0`, and
    `resp id=25 ok=1 cmd=status end`.

  - **Runtime V26 host soak harness.** ✅ hardware-verified.
    This is host-side hardening, not a kernel bootcert bump: the Pi still boots
    the Runtime V25 image and reports `bootcert ok=1 version=25`. The new
    `soak-loop.sh` script repeats `net-iterate.sh`, then sends request-wrapped
    `status`, `bootcert`, `stress`, `soak`, and `events` commands with unique
    IDs and appends machine-checkable summaries to a log. Hardware proof:
    `AETHER_SOAK_CYCLES=3 ./soak-loop.sh /Users/hansaxelsson/aether-tftp`
    completed all cycles. Proof lines included `soak summary cycle=3
    command=bootcert id=2622 line=bootcert ok=1 version=25 ... events_lost=0`,
    `soak summary cycle=3 command=stress ... heap_leak=0 frame_leak=0`,
    `soak summary cycle=3 command=soak ... failures=0 heap_leak=0 frame_leak=0`,
    `soak summary cycle=3 command=events ... lost=0 ... selftest=1`, and
    `soak result ok=1 cycles=3 completed=3 log=/tmp/aether-soak-v26.log`.

  - **Runtime V27 panic taxonomy and symbolic retained records.** ✅ hardware-verified.
    Retained panic/fault records now carry stable numeric taxonomy beside the
    historical reason text: `kind_id`, `category`, and `reason_id`. `bootcert`
    reports `bootcert ok=1 version=27 taxonomy=1 ... events_lost=0`, and
    `symbolicate-retained.sh` maps retained ELR-style addresses against the
    built Mach-O. Hardware proof: `panic-test` rebooted and `retained` reported
    `retained valid=1 kind=panic kind_id=1 category=1 reason_id=1 ... reason=panic-test`;
    `fault-test` rebooted and `retained` reported `kind=fault kind_id=2
    category=2 reason_id=2 esr=0xf20000a5 elr=0x92968 ... reason=sync-fault`;
    `./symbolicate-retained.sh 0x92968 .build/release/Application` returned
    `symbol address=0x92968 symbol_name=_kernel_trigger_sync_fault ...`; a clean
    `set -e` 3-cycle netboot loop then passed.

  - **Runtime V28 Swift runtime dependency audit.** ✅ hardware-verified.
    V28 makes the Swift runtime boundary explicit: Aether owns 10 source-level
    Swift concurrency hooks, the linked Mach-O currently carries 2 load-bearing
    hook symbols, and the heap shim contract is audited separately. Hardware
    proof: `net-iterate.sh` passed on real Pi 4, `bootcert` reported
    `bootcert ok=1 version=28 runtime=1 taxonomy=1 ... events_lost=0`, the
    shell `runtime` command returned `runtime ok=1 version=28 swift=6.3.2
    source_hooks=10 linked_hooks=2 heap_shims=5 linked_heap_shims=3
    required_symbols=5 audit=1`, and host `./runtime-audit.sh
    .build/release/Application` returned `runtime-audit ok=1 ... missing=none`.

  - **Runtime V29 agent-oriented control session.** ✅ hardware-verified.
    The UART shell now exposes a one-line `agent` health session surface for
    host agents. It aggregates the boot certificate, runtime audit, protocol
    version, queue pressure, sleeper count, heap free space, and event loss into
    one parseable line while preserving the V25 request envelope. Hardware
    proof: `net-iterate.sh` passed on real Pi 4, `bootcert` reported
    `bootcert ok=1 version=29 agent=1 runtime=1 taxonomy=1 ... events_lost=0`,
    the shell `agent` command returned `agent ok=1 version=29 health=green
    bootcert=1 runtime=1 protocol=2 agent=1 events_lost=0`, and
    `agent-session.sh` reported `agent-session ok=1 version=29 health=green
    bootcert=1 runtime=1 stress=1 soak=1 events_lost=0`. A clean `set -e`
    3-cycle netboot + agent-session loop passed.

  - **Runtime V30 Swift-native kernel substrate certificate.** ✅ hardware-verified.
    The UART shell now exposes a one-line `certificate` command that aggregates
    the substrate invariants built through V12-V29: object table, task registry,
    mailboxes, supervisor, handles, event log, cancellations, channels, drivers,
    allocator pressure, pools, MMU boundary, bootcert, agent surface, and runtime
    audit. Hardware proof: `net-iterate.sh` passed on real Pi 4, `bootcert`
    reported `bootcert ok=1 version=30 certificate=1 agent=1 runtime=1 taxonomy=1
    ... events_lost=0`, the shell `certificate` command returned `certificate ok=1
    version=30 substrate=1 bootcert=1 agent=1 runtime=1 memory=1 objects=1 tasks=1
    mailboxes=1 supervisor=1 handles=1 events=1 cancellations=1 channels=1
    drivers=1 pressure=1 pools=1 mmu=1 ... events_lost=0`, and
    `certificate-loop.sh` reported `certificate-loop ok=1 version=30 cycles=3
    completed=3 substrate=1 bootcert=1 agent=1 runtime=1 events_lost=0`.

  - **Runtime V31 preemptive scheduler substrate.** ✅ hardware-verified.
    The C substrate now owns a fixed scheduler timer client and bounded core-0
    run queue surface above the cooperative executor. The IRQ handler records
    CNTP-driven scheduler ticks and preemption accounting before servicing the
    existing sleep/executor timer clients, and the UART shell exposes a one-line
    `sched` proof command. Hardware proof: `net-iterate.sh` passed on real Pi 4,
    `bootcert` reported `bootcert ok=1 version=31 scheduler=1 certificate=1
    agent=1 runtime=1 ... events_lost=0`, the shell `certificate` command
    returned `certificate ok=1 version=31 substrate=1 bootcert=1 scheduler=1
    agent=1 runtime=1 ... events_lost=0`, and `sched` returned
    `sched ok=1 version=31 active=1 cores=1 core=0 interval_ticks=2700000
    ticks=152 irq_ticks=152 preemptions=152 runqueue=0/8 enqueues=4 dequeues=4
    selftest=1`. A clean 3-cycle live netboot repeat passed.

  - **Runtime V32 SMP secondary-core bring-up.** ✅ hardware-verified.
    Primary core 0 now releases cores 1-3 by writing `_start` into the Raspberry
    Pi armstub8 64-bit spin-table slots at `0xe0`, `0xe8`, and `0xf0`, cleaning
    those slots to memory, and issuing `sev`. Secondary cores re-enter `boot.S`,
    take private 4 KiB stacks, enable EL1/MMU state, and stay in a C-only
    heartbeat/accounting loop without touching Swift runtime state. Hardware
    proof: `net-iterate.sh` passed on real Pi 4, `bootcert` reported `bootcert
    ok=1 version=32 smp=1 scheduler=1 certificate=1 agent=1 runtime=1
    ... events_lost=0`, `certificate` returned `certificate ok=1 version=32
    substrate=1 bootcert=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`,
    and `cores` returned `cores ok=1 version=32 capacity=4 online=4 mask=0xf
    primary=0 release=0xe selftest=1 core0=1 entries0=1 heartbeat0=1 core1=1
    entries1=1 heartbeat1=395336 core2=1 entries2=1 heartbeat2=395376 core3=1
    entries3=1 heartbeat3=396081`. A clean 3-cycle live netboot repeat passed,
    and paired `cores` samples showed secondary heartbeats advancing.

  - **Runtime V33 atomics, spinlocks, and per-core run queues.** ✅ hardware-verified.
    The C substrate now owns the first bounded cross-core synchronization layer:
    compiler atomic wrappers, spinlock selftests, and four fixed scheduler run
    queues protected by per-core locks. Swift execution still stays on the
    cooperative executor while the new surface is proven through shell counters.
    Hardware proof: `net-iterate.sh` passed on real Pi 4, `bootcert` reported
    `bootcert ok=1 version=33 atomics=1 locks=1 queues=1 smp=1 scheduler=1
    certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate` returned
    `certificate ok=1 version=33 substrate=1 bootcert=1 atomics=1 locks=1
    queues=1 smp=1 scheduler=1 agent=1 runtime=1 ... events_lost=0`, `locks`
    returned `locks ok=1 version=33 atomics=1 spinlocks=1 acquisitions=2
    contentions=0 selftest=1`, and `runqueues` returned `runqueues ok=1
    version=33 cores=4 capacity=8 total=0 core0=0 core1=0 core2=0 core3=0
    enqueues0=8 dequeues0=8 selftest=1`. A clean 3-cycle live netboot repeat
    passed.

  - **Runtime V34 timer-driven SMP scheduler dispatch.** ✅ hardware-verified.
    The scheduler tick now routes bounded dispatch tokens through each online
    per-core queue and records dispatch/fairness counters without moving Swift
    task execution off the cooperative executor. Hardware proof: `net-iterate.sh`
    passed on real Pi 4, `bootcert` reported `bootcert ok=1 version=34
    preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
    certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate` returned
    `certificate ok=1 version=34 substrate=1 bootcert=1 preemptive=1
    smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 agent=1
    runtime=1 ... events_lost=0`, and `sched2` returned `sched2 ok=1 version=34
    preemptive=1 smp_scheduler=1 active=1 cores=4 online=4 dispatches=548
    routes=548 min=137 max=137 imbalance=0 core0=137 core1=137 core2=137
    core3=137 selftest=1`. The 3-cycle repeat proved balanced dispatch at
    `186/186/186/186`, `160/160/160/160`, and `157/157/157/157`.

  - **Runtime V35 secondary-owned scheduler workers.** ✅ hardware-verified.
    Secondary cores still do not execute Swift tasks; they now run a C-only
    scheduler worker hook that drains V35 worker tokens from each core's own
    bounded queue. Hardware proof: `net-iterate.sh` passed on real Pi 4,
    `bootcert` reported `bootcert ok=1 version=35 secondary_workers=1
    preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
    certificate=1 agent=1 runtime=1 ... events_lost=0`, `certificate` returned
    `certificate ok=1 version=35 substrate=1 bootcert=1 secondary_workers=1
    preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
    agent=1 runtime=1 ... events_lost=0`, and `sched3` returned `sched3 ok=1
    version=35 secondary_workers=1 active=1 cores=4 online=4 worker_drains=3
    worker_idles=1396994 min=1 max=1 imbalance=0 core0=0 core1=1 core2=1
    core3=1 selftest=1`. The 3-cycle repeat kept `core0=0` and cores 1-3 at
    `1/1/1` while V34 `sched2` stayed balanced.

  - **Runtime V36 timer-fed secondary scheduler workers.** ✅ hardware-verified.
    The core-0 scheduler timer now feeds bounded worker tokens into cores 1-3
    queues while Swift task execution remains on the cooperative executor.
    Hardware proof: `net-iterate.sh` passed on real Pi 4, `bootcert` reported
    `bootcert ok=1 version=36 worker_feed=1 secondary_workers=1 preemptive=1
    smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1
    agent=1 runtime=1 ... events_lost=0`, `certificate` returned `certificate
    ok=1 version=36 substrate=1 bootcert=1 worker_feed=1 secondary_workers=1
    preemptive=1 smp_scheduler=1 atomics=1 locks=1 queues=1 smp=1 scheduler=1
    agent=1 runtime=1 ... events_lost=0`, and `sched4` returned `sched4 ok=1
    version=36 worker_feed=1 secondary_workers=1 feeds=708 drains=711 drops=0
    gap=0 feed_imbalance=0 drain_imbalance=0 core0_feed=0 core1_feed=236
    core2_feed=236 core3_feed=236 core0_drain=0 core1_drain=237
    core2_drain=237 core3_drain=237 selftest=1`. The 3-cycle repeat kept
    feed/drain pairs at `693/696`, `684/687`, and `684/687`, all with
    `drops=0 gap=0`, while V33 `runqueues` remained `total=0`.

  - **Runtime V37 timer-fed secondary C scheduler jobs.** ✅ hardware-verified.
    V37 turns the V36 timer-fed secondary tokens into typed C-only scheduler
    jobs and keeps Swift execution on core 0. Hardware proof: `net-iterate.sh`
    passed on real Pi 4, `bootcert` reported `bootcert ok=1 version=37
    job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1
    atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1
    runtime=1 ... events_lost=0`, `certificate` returned `certificate ok=1
    version=37 substrate=1 bootcert=1 job_exec=1 worker_feed=1
    secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`, and
    `sched5` returned `sched5 ok=1 version=37 job_exec=1 worker_feed=1
    secondary_workers=1 executions=756 completions=756 noops=0
    checksum=698517273110 gap=0 imbalance=0 core0_exec=0 core1_exec=252
    core2_exec=252 core3_exec=252 core0_done=0 core1_done=252 core2_done=252
    core3_done=252 selftest=1`. The 3-cycle repeat kept execution/completion
    pairs at `738/738`, `699/699`, and `699/699`, all with
    `noops=0 gap=0 imbalance=0`, while V33 `runqueues` remained `total=0`.

  - **Runtime V38 secondary scheduler wake protocol.** ✅ hardware-verified.
    V38 adds bounded SEV signals on secondary job enqueue and WFE waits in the
    C-only secondary scheduler loops. Hardware proof: `net-iterate.sh` passed
    on real Pi 4, `bootcert` reported `bootcert ok=1 version=38 wake=1
    job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1 smp_scheduler=1
    atomics=1 locks=1 queues=1 smp=1 scheduler=1 certificate=1 agent=1
    runtime=1 ... events_lost=0`, `certificate` returned `certificate ok=1
    version=38 substrate=1 bootcert=1 wake=1 job_exec=1 worker_feed=1
    secondary_workers=1 preemptive=1 smp_scheduler=1 ... events_lost=0`, and
    `sched6` returned `sched6 ok=1 version=38 wake=1 job_exec=1 worker_feed=1
    signals=825 mask=0xe targets=825 waits=84020214 wakes=84028069 gap=1
    imbalance=6818555 core0_wait=0 core1_wait=30418819
    core2_wait=23601879 core3_wait=30047539 core0_wake=0
    core1_wake=30432773 core2_wake=23612725 core3_wake=30060963
    selftest=1`. The 3-cycle repeat kept `sched6 ok=1`, `wake=1`,
    `runqueues total=0`, and `events_lost=0`. V38 treats WFE imbalance as
    telemetry because WFE can resume for architectural events beyond scheduler
    SEV; the correctness gate is positive core1-3 acknowledgement plus bounded
    gaps.

  - **Runtime V39 secondary scheduler handoff protocol.** ✅ hardware proof
    accepted 2026-06-06. V39 records bounded cross-core scheduler handoff
    issue/completion counters for timer-fed C-only jobs from core 0 to cores
    1-3. The accepted image hash was
    `f24f26c85da4058853e5c7ec4af1822b7a77545259e492786c233421a45b831f`.
    Proof lines included `bootcert ok=1 version=39 handoff=1 wake=1
    job_exec=1 worker_feed=1 secondary_workers=1 preemptive=1
    smp_scheduler=1 ... events_lost=0`, `certificate ok=1 version=39
    substrate=1 bootcert=1 handoff=1 wake=1 job_exec=1 worker_feed=1
    secondary_workers=1 ... events_lost=0`, and `sched7 ok=1 version=39
    handoff=1 wake=1 job_exec=1 issued=885 completed=885 gap=0 imbalance=0
    core0_issue=0 core1_issue=295 core2_issue=295 core3_issue=295
    core0_done=0 core1_done=295 core2_done=295 core3_done=295 selftest=1`.
    The 3-cycle repeat kept `resp id=40 ok=1 cmd=sched7 end`,
    `runqueues total=0`, and `events_lost=0`.

  - **Runtime V40 scheduler backpressure protocol.** ✅ hardware proof accepted
    2026-06-06. V40 records per-core runqueue high-water and overflow counters,
    then uses a fixed C selftest to saturate each bounded queue, reject one
    over-capacity enqueue, and drain back to zero. The accepted image hash was
    `46bc501a6513cf8a2187203c216be0dd6e54cf49223db3547f4d01578ca78372`.
    Proof lines included `bootcert ok=1 version=40 backpressure=1 handoff=1
    wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`,
    `certificate ok=1 version=40 substrate=1 bootcert=1 backpressure=1
    handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ...
    events_lost=0`, and `sched8 ok=1 version=40 backpressure=1 handoff=1
    wake=1 high_water=8 overflows=16 total=0 capacity=8 core0_high=8
    core1_high=8 core2_high=8 core3_high=8 core0_overflow=4 core1_overflow=4
    core2_overflow=4 core3_overflow=4 selftest=1`. The 3-cycle repeat kept
    `sched7 ok=1`, `runqueues total=0`, and `events_lost=0`.

  - **Runtime V41 secondary scheduler work-stealing protocol.** ✅ hardware
    proof closed 2026-06-06. V41 lets idle C-only secondary scheduler workers
    steal bounded steal-job tokens from another secondary queue, execute them
    locally, and drain every queue back to zero. Proof: `bootcert ok=1
    version=41 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1
    worker_feed=1 secondary_workers=1 ... events_lost=0`, `certificate ok=1
    version=41 substrate=1 bootcert=1 stealing=1 backpressure=1 handoff=1
    wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
    `sched9 ok=1 version=41 stealing=1 backpressure=1 handoff=1 wake=1
    steals=4 completions=4 total=0 capacity=8 source_core1=4 source_core2=0
    source_core3=0 dest_core1=0 dest_core2=2 dest_core3=2 selftest=1`. The
    3-cycle repeat kept `certificate ok=1`, `sched9 ok=1`, `runqueues total=0`,
    and `events_lost=0`.

  - **Runtime V42 secondary scheduler load-balancing protocol.** ✅ hardware
    proof closed 2026-06-06. V42 lets underloaded C-only secondary scheduler
    workers pull bounded balance-job tokens from an overloaded peer queue,
    execute them locally, and record per-core fairness counters while draining
    every queue back to zero. Proof: `bootcert ok=1 version=42 fairness=1
    stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1
    secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=42
    substrate=1 bootcert=1 fairness=1 stealing=1 backpressure=1 handoff=1
    wake=1 job_exec=1 worker_feed=1 secondary_workers=1 ... events_lost=0`, and
    `sched10 ok=1 version=42 fairness=1 stealing=1 backpressure=1 handoff=1
    wake=1 balances=3 completions=3 total=0 capacity=8 source_core1=3
    source_core2=0 source_core3=0 dest_core1=0 dest_core2=2 dest_core3=1
    queue_imbalance=0 selftest=1`. The 3-cycle repeat kept `certificate ok=1`,
    `sched10 ok=1`, `runqueues total=0`, and `events_lost=0`.

  - **Runtime V43 secondary scheduler priority/preemption protocol.** ✅ hardware
    proof closed 2026-06-07. V43 adds bounded high-priority scheduler lanes on
    secondary cores with preempt/yield counters while keeping Swift execution on
    core 0. Proof: `bootcert ok=1 version=43 priority=1 fairness=1 stealing=1
    backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1
    ... events_lost=0`, `certificate ok=1 version=43 substrate=1 bootcert=1
    priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1
    worker_feed=1 secondary_workers=1 ... events_lost=0`, and `sched11 ok=1
    version=43 priority=1 fairness=1 stealing=1 backpressure=1 handoff=1 wake=1
    preemptions=2 yields=2 completions=4 total=0 capacity=8 low_core1=2
    high_core1=2 preempt_core1=2 yield_core1=2 selftest=1`. `net-iterate.sh`
    passed all shell probes after a power-cycle recovery on the live Pi bench.

  - **Runtime V44 bounded SMP concurrency soak protocol.** ✅ hardware
    proof closed 2026-06-07. V44 keeps SMP dispatch and timer-fed secondary
    workers active while proving three bounded soak rounds drain every per-core
    queue back to zero. Proof: `bootcert ok=1 version=44 concurrency=1 priority=1
    fairness=1 stealing=1 backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1
    secondary_workers=1 ... events_lost=0`, `certificate ok=1 version=44
    substrate=1 bootcert=1 concurrency=1 priority=1 fairness=1 stealing=1
    backpressure=1 handoff=1 wake=1 job_exec=1 worker_feed=1 secondary_workers=1
    ... events_lost=0`, and `sched12 ok=1 version=44 concurrency=1 rounds=3
    completions=3 failures=0 dispatches=9 soak_core1=3 soak_core2=3 soak_core3=3
    selftest=1`. Wemo cold-cycle `netboot-auto.sh` passed all shell probes and
    `soak-loop.sh` ended `soak result ok=1 cycles=3 completed=3`.

## Provenance

Built from research by GPT-5.5 Pro (architecture dossiers) and Grok (iterative
skeleton), reconciled against Apple's `swift-embedded-examples/rpi-4b-blink`
(the verified base). The boot path was written and reviewed line-by-line rather
than transliterated — bare-metal punishes confident-but-wrong.
