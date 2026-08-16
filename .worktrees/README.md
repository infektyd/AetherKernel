# Agent worktrees

Put isolated git worktrees **here**, not as siblings under `~/Projects`.

```bash
git worktree add .worktrees/s69-sch04 feat/s69-sch04
```

Remove with `git worktree remove .worktrees/<name>` when the slice is cherry-picked or abandoned.
The directory contents (except this README) are gitignored.
