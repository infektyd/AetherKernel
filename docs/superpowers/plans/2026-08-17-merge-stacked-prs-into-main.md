# Merge 71 Open PRs Into `main`

> **For agentic workers:** This is an integration plan, not a feature build.
> Do **not** merge to `main` from an agent session (`AGENTS.md`: the human merges).
> Do **not** squash the stack. Do **not** rebase it onto a moving `main` unless
> the preflight ancestor check fails.

**Goal:** Land every currently open PR onto `main` without rewriting the
already-linear stack, then close the leftover GitHub PR objects.

**Architecture:** PRs **#1** and **#3–#70** are one stacked branch chain whose
tip already contains **#2**. `origin/main` is a strict ancestor of that tip, so
the whole stack is a fast-forward. PR **#71** is independent (`AGENTS.md` only)
and merges cleanly onto the tip.

**Tech Stack:** GitHub stacked PRs, `gh`, git fast-forward, host `pytest`.
Metal proof stays on the Mac + Pi 4 loop (`./netboot-auto.sh` / `./net-iterate.sh`).

## Global Constraints

- Human merges to `main`. Agents commit on a working branch only.
- Do not squash. The stack is 155 commits of versioned, bisectable slices (V58–V135 plus foundation).
- Do not retarget the Swift triple or claim metal proof without a serial capture from the merge session.
- Boot certificate must keep reporting `swift=6.3.2`.
- If `main` gains any commit before the land, stop and re-check ancestry. Do not force-push.
- `feat/macho-concurrency` is **not** on the remote. The live integration tip is `pr/genet31-v135`.

---

## Snapshot (verified 2026-08-17)

| Item | Value |
| --- | --- |
| Open PRs | **71**, all `MERGEABLE` / `CLEAN`, no CI rollup |
| `origin/main` | `e57a68b35f3a0f9a53aa13daa126f7aaacd05baf` (`chore: add Apache 2.0 license`) |
| Stack tip | `origin/pr/genet31-v135` = `0c26173bb48e6c5fde172aac4cfac2ee22cfa1e6` |
| Commits `main..tip` | **155** (146 first-parent) |
| Commits `tip..main` | **0** |
| Fast-forward possible | **yes** (`main` is an ancestor of the tip) |
| Merge commits in stack | **1**: `6cd238a` *merge: stack PR2 event-log attestation into shell/VMM branch* |
| Tip vs `main` | **214 files**, `+21087 / -567` |
| PR #71 onto tip | clean tree `64d50fe5d8f265aea3b09f4aaece1ec7dfb01e5b` |

```
main ─────────────────────────────────────────────► e57a68b
  │
  ├── PR#1  pr/sched-selftest-honesty ────────────► 655cffa
  │         │
  │         └── PR#3 … PR#70  (linear stack) ─────► 0c26173  pr/genet31-v135
  │                    ▲
  │                    │ merge 6cd238a
  └── PR#2  pr/event-log-attestation ─────────────► e4131b9
  │
  └── PR#71 agent/cloud-dev-env-01f1 ─────────────► da21e33  (AGENTS.md only)
```

PR #2 is **not** an ancestor of PR #1. It **is** an ancestor of PR #3 and of
the tip. Landing the tip lands #1 and #2 together. Do not also merge #2 onto
`main` first — that would only create a second merge of the same commits.

---

## Approaches

### A. Fast-forward `main` to the tip, then merge #71 (recommended)

One local FF (or one GitHub merge of an umbrella PR whose head is the tip).
Same tree as merging #1 then #3…#70 in order. Preserves all 155 commits.
Then merge #71 with a normal merge commit (or rebase-and-FF). Close #1–#70.

**Why this wins:** the stack is already the integration. Seventy GitHub merge
clicks do not add safety. Solo-builder bar: one ancestor check, one FF, one
docs PR.

### B. Sequential GitHub merge of each stacked PR

Merge #1 into `main`, retarget #3 to `main`, merge #3, … through #70, then #71.
Skip merging #2 onto `main` (already inside #3). ~69 GitHub operations.
Use only if you want every PR to show a GitHub "Merged" event.

### C. Squash the tip into one commit on `main`

**Reject.** Destroys bisect to a given V-increment and the recorded proof
commits. 21k lines in one squash is not reviewable later.

**Recommendation: A.** Optional wrapper: open one umbrella PR
`main ← pr/genet31-v135` so the land has a GitHub review surface, then
**FF-only** merge it. Do not squash-merge that umbrella.

---

## Wave catalog (what the stack actually is)

Use this to review or to run Approach B. Approach A lands every wave at once.

### Wave 0 — two roots onto `main` (already joined at #3)

| PR | Head | What it is |
| --- | --- | --- |
| #1 | `pr/sched-selftest-honesty` @ `655cffa` | Honest scheduler selftests; `schedselftest` banners v31–v44 |
| #2 | `pr/event-log-attestation` @ `e4131b9` | Event-log spinlock, lost-count, attestation order |

### Wave 1 — shell, netboot contracts, HDMI, USB hubwalk (#3–#9)

| PR | Head | What it is |
| --- | --- | --- |
| #3 | `pr/shell-vmm-surfaces` @ `070e3d1` | Sealed shell/VMM surfaces; **includes merged #2** |
| #4 | `pr/netboot-metal-proof` @ `f6a4352` | Netboot metal proof bound to live shell schemas through v66 |
| #5 | `pr/contracts-boot-grep` @ `e2e2f41` | `COMMANDS_V` subsequence + v46–v63 boot grep gate |
| #6 | `pr/docs-v64-v66-markers` @ `ea99d06` | Operator docs aligned to v64–v66 |
| #7 | `pr/shelly-serial-sysping` @ `06f57c9` | Shelly Gen4 cold-cycle; in-repo serial capture |
| #8 | `pr/s69-hdmi-drain` @ `dc6fc48` | S69 lockstep, HDMI UART mirror, drain catch-up |
| #9 | `pr/hubwalk` @ `c55c3d5` | `hubwalk` token after VL805 downstream probe |

### Wave 2 — GENET host-path through bounded TCP (#10–#14)

| PR | Head | What it is |
| --- | --- | --- |
| #10 | `pr/genet-v67-v70` @ `94e2f82` | V67–V70 probe, leftover RX MIB, mailbox MAC/serial |
| #11 | `pr/genet-v71-v74` @ `8e98a20` | RX ring, GPIO, UMAC MAC, TX ARP, ring-16 |
| #12 | `pr/genet-v75-v77` @ `b249e2d` | GENET v4 PROD doorbell, ICMP reply, host ping poll |
| #13 | `pr/genet-v78-udp` @ `86e9b8a` | Bounded UDP echo port 7 |
| #14 | `pr/genet-v79-tcp` @ `e99b45a` | Bounded TCP echo port 7 |

### Wave 3 — on-board peripherals (#15–#19)

| PR | Head | What it is |
| --- | --- | --- |
| #15 | `pr/i2c-spi-v80` @ `018e7ad` | I2C BSC1 + SPI0 probe |
| #16 | `pr/pwm-v81` @ `9b2fc65` | PWM0/PWM1 probe |
| #17 | `pr/i2c-nack-v82` @ `6b12b73` | NACK one vacant I2C address |
| #18 | `pr/spi-xfer-v83` @ `46932ba` | One SPI0 byte (no loopback claim) |
| #19 | `pr/stimer-v84` @ `740dc46` | System timer probe without GPU compares |

### Wave 4 — FAT32 read extras after GENET (#20–#25)

| PR | Head | What it is |
| --- | --- | --- |
| #20 | `pr/sdload-v85` @ `b27a38d` | Reload `config.txt` |
| #21 | `pr/sdls-v86` @ `87fdfeb` | List FAT32 root |
| #22 | `pr/sdfile-v87` @ `75e4c29` | Load a second root file |
| #23 | `pr/sdovl-v88` @ `9282f9b` | Walk `overlays/` |
| #24 | `pr/sdovf-v89` @ `a6f7588` | Load one overlays file |
| #25 | `pr/sdiss-v90` @ `7ae7daa` | Load `issue.txt` by name |

### Wave 5 — GPIO / timer / PWM / mailbox / wdog / RNG / DMA (#26–#36)

| PR | Head | What it is |
| --- | --- | --- |
| #26 | `pr/gpio2-v91` @ `0f85b0c` | GPIO42 SET/CLR via GPLEV |
| #27 | `pr/stimer2-v92` @ `77d8df1` | System timer C1 match |
| #28 | `pr/pwm2-v93` @ `cd9c345` | PWM clock enable |
| #29 | `pr/gpio3-v94` @ `bd5fffe` | GPIO26 PUP_PDN write+readback |
| #30 | `pr/stimer3-v95` @ `8704cc8` | System timer C3 match |
| #31 | `pr/mboxt-v96` @ `8afa4e8` | Mailbox `GET_TEMPERATURE` |
| #32 | `pr/mboxc-v97` @ `3fc208a` | Mailbox `GET_CLOCK_RATE` |
| #33 | `pr/wdog2-v98` @ `4fde602` | PM watchdog remaining-tick |
| #34 | `pr/mboxv-v99` @ `7f9a354` | Mailbox `GET_VOLTAGE` |
| #35 | `pr/rng-v100` @ `acb99c6` | BCM2711 RNG200 |
| #36 | `pr/dma2-v101` @ `7a858e6` | DMA engine memcpy |

### Wave 6 — SD write + SDHCI + FAT reserved area (#37–#51)

| PR | Head | What it is |
| --- | --- | --- |
| #37 | `pr/sdwr-v102` @ `f80224b` | CMD24 write of a free cluster |
| #38 | `pr/sdmk-v103` @ `00e45c7` | Create/link `AETHER.TMP` |
| #39 | `pr/sdrd-v104` @ `9479efb` | Named `AETHER.TMP` reread |
| #40 | `pr/sdst-v105` @ `79ec95d` | CMD13 card status |
| #41 | `pr/sdscr-v106` @ `52f8542` | ACMD51 SEND_SCR |
| #42 | `pr/sdss-v107` @ `e8b7a8f` | ACMD13 SD_STATUS |
| #43 | `pr/sdbus-v108` @ `cce2c4d` | ACMD6 4-bit bus |
| #44 | `pr/sdmb-v109` @ `25aadf3` | CMD18 multi-block |
| #45 | `pr/sdsw-v110` @ `7aee79d` | CMD6 SWITCH_FUNC check |
| #46 | `pr/sdmw-v111` @ `e376da5` | CMD25 multi-block write |
| #47 | `pr/sdbc-v112` @ `72ab464` | CMD23 SET_BLOCK_COUNT |
| #48 | `pr/sdfi-v113` @ `30b3d8b` | FAT32 FSInfo sector |
| #49 | `pr/sdfb-v114` @ `37daaf1` | FAT32 backup boot sector |
| #50 | `pr/sdfm-v115` @ `9d9ebb5` | FAT-mirror compare |
| #51 | `pr/sdrm-v116` @ `fc66918` | Scratch unlink |

### Wave 7 — GENET originated + MAC/PHY finish (#52–#70)

| PR | Head | What it is |
| --- | --- | --- |
| #52 | `pr/genet13-v117` @ `2269074` | Originated ARP + ICMP echo |
| #53 | `pr/genet14-v118` @ `677e984` | Originated UDP echo |
| #54 | `pr/genet15-v119` @ `b36efe3` | Originated TCP echo |
| #55 | `pr/genet16-v120` @ `40f39c4` | Originated TFTP get |
| #56 | `pr/genet17-v121` @ `669fc07` | Originated mDNS query |
| #57 | `pr/genet18-v122` @ `4d5d3dd` | Originated HTTP get |
| #58 | `pr/genet19-v123` @ `678676a` | Originated SNTP client |
| #59 | `pr/genet20-v124` @ `062e078` | Originated SSDP search |
| #60 | `pr/genet21-v125` @ `dcc2898` | UMAC TX MIB delta |
| #61 | `pr/genet22-v126` @ `447d229` | TCP helper liveness |
| #62 | `pr/genet23-v127` @ `17905d9` | INTRL2 TXDMA_DONE |
| #63 | `pr/genet24-v128` @ `98577c0` | UMAC station filter |
| #64 | `pr/genet25-v129` @ `8f529b9` | MDIO PHY identifier |
| #65 | `pr/genet26-v130` @ `95dcf06` | UMAC max-frame writeback |
| #66 | `pr/genet27-v131` @ `26ccf71` | MDIO write path |
| #67 | `pr/genet28-v132` @ `248a5ff` | UMAC MDF |
| #68 | `pr/genet29-v133` @ `a7c31b8` | RBUF RXCHK enable |
| #69 | `pr/genet30-v134` @ `5ceae29` | TBUF EEE enable |
| #70 | `pr/genet31-v135` @ `0c26173` | HFB filter-0 enable |

PR #70 already records a metal proof in its body:
`kernel8.img` sha256 `4bdf9c664d4e9ec023816ef836e018d29cf15066508cc7502fcae375da462d48`,
serial `genet31 ok=1 version=135 hfb=1 restore=1`. Re-prove on the merge
machine if you want a this-session marker; do not invent a new one.

### Wave 8 — independent Cloud docs (#71)

| PR | Head | What it is |
| --- | --- | --- |
| #71 | `agent/cloud-dev-env-01f1` @ `da21e33` | Cursor Cloud Linux verification notes in `AGENTS.md` |

`AGENTS.md` is also touched on the tip (Wemo → Shelly + worktree rule). The
hunks do not overlap. `git merge-tree --write-tree` of tip + #71 succeeds.

---

### Task 1: Preflight (abort if any check fails)

**Files:** none (read-only git/GitHub).

**Interfaces:**
- Consumes: `origin/main`, `origin/pr/genet31-v135`, `origin/agent/cloud-dev-env-01f1`
- Produces: go / no-go for Task 2

- [ ] **Step 1: Fetch the three refs**

```bash
git fetch origin main pr/genet31-v135 agent/cloud-dev-env-01f1
```

Expected: fetch succeeds; no new unknown commits required.

- [ ] **Step 2: Confirm `main` is still a strict ancestor of the tip**

```bash
git merge-base --is-ancestor origin/main origin/pr/genet31-v135 && echo FF_OK
git rev-parse origin/main origin/pr/genet31-v135
git rev-list --count origin/main..origin/pr/genet31-v135
git rev-list --count origin/pr/genet31-v135..origin/main
```

Expected:

```
FF_OK
e57a68b35f3a0f9a53aa13daa126f7aaacd05baf
0c26173bb48e6c5fde172aac4cfac2ee22cfa1e6
155
0
```

If `main` moved: stop. Either reset the plan to the new `main` (rebase the
whole stack — expensive) or merge `main` into the tip and re-prove. Do not FF.

If the tip SHA moved: re-read this plan against the new tip before landing.

- [ ] **Step 3: Confirm #2 is inside the tip and #71 still merges clean**

```bash
git merge-base --is-ancestor origin/pr/event-log-attestation origin/pr/genet31-v135 && echo PR2_IN_TIP
git merge-tree --write-tree origin/pr/genet31-v135 origin/agent/cloud-dev-env-01f1
```

Expected: `PR2_IN_TIP` and a tree SHA (no conflict). Snapshot tree was
`64d50fe5d8f265aea3b09f4aaece1ec7dfb01e5b`.

- [ ] **Step 4: Host pytest on the tip (not a metal claim)**

```bash
git switch --detach origin/pr/genet31-v135
python3 -m pytest tests/ -q
```

Expected: current-version and tooling tests pass. Older `test_runtime_vN_contract.py`
pins may be red (historical exact-version snapshots). Serial-dongle tests fail
without a USB-TTL adapter. Do not "fix" those to make the land look greener.

- [ ] **Step 5: Optional metal proof of the tip (Mac + Pi 4 only)**

```bash
./netboot-auto.sh
```

Expected serial includes `genet31 ok=1 version=135 hfb=1 restore=1` and
`swift=6.3.2` on the boot certificate. Record the live `kernel8.img` sha256.
Skip this step only if you explicitly accept PR #70's already-recorded proof.

---

### Task 2: Land the stack on `main` (Approach A)

**Files:** none locally except git refs. After the FF, `main` == tip tree
(214 files vs today's `main`).

**Interfaces:**
- Consumes: Task 1 go
- Produces: `origin/main` at `0c26173` (or the re-checked tip SHA)

- [ ] **Step 1: Fast-forward `main` locally**

```bash
git switch main
git merge --ff-only origin/pr/genet31-v135
git log -1 --oneline
```

Expected: `0c26173 feat(net): prove GENET HFB filter-0 enable`

If git refuses `--ff-only`, `main` moved. Stop. Do not `--no-ff` a surprise
merge without re-reading Task 1.

- [ ] **Step 2: Push `main` (human only)**

```bash
git push origin main
```

Expected: fast-forward on GitHub. `main` HEAD is the tip.

- [ ] **Step 3: Alternative GitHub path (same tree)**

If you want a review surface instead of a local FF:

```bash
gh pr create --base main --head pr/genet31-v135 \
  --title "Land stacked PRs #1–#70 (FF to genet31 / v135)" \
  --body "Umbrella FF of the existing linear stack. Do not squash."
gh pr merge <N> --ff-only
```

Do **not** use `--squash` or the GitHub "Squash and merge" button.

---

### Task 3: Close stacked PR objects that will not auto-close

**Files:** none.

**Interfaces:**
- Consumes: `main` contains `0c26173`
- Produces: PRs #1–#70 closed

GitHub auto-closes PRs whose **head is in `main` and whose base is `main`**.
That is **#1** and **#2** only. #3–#70 target the previous stack branch, so
they stay open until closed.

- [ ] **Step 1: Confirm the tip commit is on `main`**

```bash
git fetch origin main
git merge-base --is-ancestor 0c26173 origin/main && echo TIP_ON_MAIN
```

Expected: `TIP_ON_MAIN`

- [ ] **Step 2: Close #3–#70 with a pointer at the land**

```bash
for n in $(seq 3 70); do
  gh pr close "$n" --comment "Closed: commits are on main via FF of pr/genet31-v135 (0c26173). Not a reject."
done
```

Expected: 68 PRs closed. #1 and #2 should already show merged/closed; if they
are still open, close them the same way.

- [ ] **Step 3: Do not delete stack branches until #71 is merged**

Keep `pr/genet31-v135` until Wave 8 is on `main`. Then:

```bash
# optional cleanup after Task 4
gh pr view 71 --json state,mergedAt
# only then delete pr/* stack branches if you want them gone
```

---

### Task 4: Land PR #71 onto the new `main`

**Files:**
- Modify: `AGENTS.md` (append the Cursor Cloud section; keep the tip's Shelly
  + worktree edits)

**Interfaces:**
- Consumes: `main` at stack tip
- Produces: `AGENTS.md` with both the Shelly/worktree edits and the Cloud section

- [ ] **Step 1: Retarget #71 to current `main` if GitHub still shows a clean merge**

```bash
gh pr view 71 --json baseRefName,mergeable,headRefOid
git fetch origin agent/cloud-dev-env-01f1
git merge-tree --write-tree origin/main origin/agent/cloud-dev-env-01f1
```

Expected: still a clean tree. If GitHub reports a conflict after the FF
(file-level "changed in both"), merge locally:

```bash
git switch -c agent/cloud-dev-env-onto-tip origin/agent/cloud-dev-env-01f1
git merge origin/main
# resolve AGENTS.md by keeping BOTH hunks:
#   - Shelly Gen4 + worktrees paragraph from the tip
#   - Cursor Cloud section from #71
git commit --no-edit   # if merge created a commit
git push -u origin agent/cloud-dev-env-onto-tip
```

Then point #71 at that branch, or open a tiny follow-up PR.

- [ ] **Step 2: Merge #71 (human)**

```bash
gh pr merge 71 --merge
```

`--merge` (merge commit) or `--ff-only` after a rebase are both fine. Squash
is acceptable here: it is one docs commit.

- [ ] **Step 3: Confirm `AGENTS.md` on `main` has both edits**

```bash
git fetch origin main
git show origin/main:AGENTS.md | rg -n "Shelly Plug|Cursor Cloud specific|Worktrees stay in-repo"
```

Expected: all three strings present.

---

### Task 5: Post-land honesty

**Files:**
- `docs/ROADMAP.md` on the tip already says **Where we are now (V66)** and
  records later GENET/SD/peripheral increments through V135. No extra ROADMAP
  edit is required for the FF itself.
- `README.md` on the tip still headlines Runtime V66 as the cold-boot floor.
  That is existing stack content, not something this land should rewrite.

- [ ] **Step 1: Spot-check `main`**

```bash
git log --oneline origin/main -5
git merge-base --is-ancestor e57a68b origin/main && echo OLD_MAIN_STILL_IN_HISTORY
```

Expected: tip commit on top; old `main` still reachable (true FF).

- [ ] **Step 2: Do not claim a new runtime version**

The land does not create V136. It publishes V58–V135 that already exist on
the stack. Any new work starts at the next integer after a metal proof.

---

## Approach B (only if you refuse a single FF)

Merge in this exact order. After each merge, wait until GitHub retargets the
next PR or run `gh pr edit <next> --base main`.

1. Merge **#1** (`--merge` or `--ff-only`). Do **not** merge **#2** onto `main`.
2. Retarget **#3** to `main` (it already contains #2 via `6cd238a`). Merge #3.
3. For **n = 4..70**: retarget PR *n* to `main`, merge FF-only.
4. Then Task 4 (#71).

If you merge #2 onto `main` after #1, you create a second join of the same
two roots that #3 already resolved. Harmless tree-wise if the file merge is
clean (it is), but it adds a redundant merge commit and confuses the stack
retargets. Skip it.

---

## What this plan will not do

- Will not merge from an agent session.
- Will not squash #1–#70.
- Will not open USB/network work beyond what the stack already contains.
- Will not treat a green host pytest as metal proof.

---

## Spec coverage / self-review

- All 71 open PRs are named, SHA-pinned, and assigned a wave.
- #2 is handled as "already in the tip," not as a second land onto `main`.
- #71 is sequenced after the FF because it shares `AGENTS.md`.
- Abort conditions are explicit (moved `main`, moved tip, FF refused).
- No "TBD" merge order.
