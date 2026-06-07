# AetherKernel — Agent Rules

Bare-metal kernel in **Embedded Swift** for Raspberry Pi 4 (BCM2711, Cortex-A72).
Target triple `arm64-apple-none-macho` (Mach-O embedded), toolchain **Swift 6.3.2**.
Working branch: `feat/macho-concurrency`.

## Hard constraints (never violate)

- **Local only.** Do NOT `git push` and do NOT merge to `main`. The human merges. Commit to the working branch only.
- **Proof, not green builds.** A compiler "build succeeded" is NOT evidence the kernel works. Hardware proof = serial markers from the real board via `net-iterate.sh` (TFTP flash + boot output). Never claim a runtime version proven without a matching serial marker and the kernel8.img sha256.
- **Netboot is cold-start only.** The Pi4 bootloader re-arms netboot/TFTP only on a *cold* power-up; a warm `reboot` or watchdog reset does NOT. For unattended loops the power-cycle backend must be set (see below) — a warm reset will silently stall the loop.
- **Don't invent proof strings.** Every `sched*/bootcert/certificate` line you record must come from an actual board run captured this session.
- **Toolchain is pinned: Swift 6.3.2-RELEASE.** Always build via `./build.sh` (which forces `~/Library/Developer/Toolchains/swift-6.3.2-RELEASE.xctoolchain`) and the `Toolsets/rpi4-macho.json` toolset. Do NOT switch toolchains, retarget the triple away from `arm64-apple-none-macho`, or change the force-loaded Embedded `_Concurrency` archive. The boot certificate MUST keep reporting `swift=6.3.2`; if it ever doesn't, treat it as a regression, not a new baseline.

## Build & hardware-proof loop

- Flash + prove on metal: `./net-iterate.sh` (TFTP `kernel8.img` → serial markers).
- Unattended cold-cycle (Wemo WSP080): run via `./netboot-auto.sh` (exports `AETHER_POWER_BACKEND=wemo HOST=192.168.179.222 WEMO_PORT=49152`), which `exec`s `net-iterate.sh`. Without `AETHER_POWER_BACKEND` set, the loop does a warm reset only (does not re-arm netboot).
- Power-cycle plumbing: `./power-cycle.sh {off|on|cycle}` (pluggable backend; see script `--help`).

## Version control

- Conventional-commit messages. One accepted slice per commit where practical.
- Record hardware proofs in the commit/notes with: runtime version, kernel8.img sha256, and the literal serial marker lines.

## Cross-agent planning (Minni)

- Work is organized as a **Minni plan** with an evidence gate. Each slice must reach an **accepted** state backed by real evidence before moving on.
- When given a `plan_id`, treat the plan as the source of truth for "what's next." Pick the lowest-numbered slice that is not yet accepted; do not skip ahead.

---

## Autonomous mode (only when explicitly activated)

These rules apply **only** when the session is started in autonomous mode — i.e. the
prompt or `--rules` contains the activation token **`AUTODRIVE`**. In a normal
interactive session, ignore this section and behave normally (stop and ask when a
unit of work is done).

When `AUTODRIVE` is active, work like a self-driving engineer — do not wait to be
told to continue:

1. **Orient.** Read the active Minni plan (`plan_id` is supplied). Identify the lowest-numbered slice that is not yet `accepted`.
2. **Execute one slice fully.** Implement → build → prove on metal (cold-cycle as needed) → capture the real serial markers + kernel8.img sha256 → update the slice to `accepted` with that evidence → commit on the working branch.
3. **Advance, don't stop.** Immediately move to the next unaccepted slice and repeat. Do not end your turn just because one slice is done — only the conditions below end the run.
4. **When the active plan is fully accepted, do NOT stop — climb the roadmap.** Read `ROADMAP.md`, pick the next unbuilt increment whose dependencies are met (lowest epic first; never jump ahead to USB/networking before user-mode/processes exist), create a NEW Minni plan for that milestone (`minni_plan_create`), decompose it into small slices you can each prove on metal, then continue from step 1. The roadmap is the far-horizon source of "what's next"; the Minni plan is the near-horizon execution state.
5. **Stop conditions (end the turn and print the matching final line):**
   - The roadmap's North Star is reached / no roadmap increment remains → print exactly `AUTODRIVE_DONE` on the last line.
   - You are genuinely blocked (hardware unreachable after a cold-cycle retry, ambiguous spec, missing credential, repeated identical failure, or a roadmap increment too large to scope safely without human input) → print exactly `AUTODRIVE_BLOCKED: <one-line reason>` on the last line. Do not loop on the same failing action.
6. **Never** push, merge to `main`, fabricate proof, or mark a slice accepted without a captured board marker — autonomy does not relax the hard constraints above.
7. **Make forward progress every turn.** Each turn should end with either a new commit, a slice/plan state change, or a stop line. A turn that did none of these is a stall; say so via `AUTODRIVE_BLOCKED` rather than spinning.
8. **Keep the roadmap honest.** When a milestone completes, update `ROADMAP.md`'s "Where we are now" line and check off the increment in the same commit.
