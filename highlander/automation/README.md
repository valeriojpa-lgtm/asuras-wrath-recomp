# Highlander THESEUS PC — Automation branch

This directory is an isolated automation workspace for the Highlander PC reconstruction project.

## Preservation rules

- The seven source builds are treated as **read-only**.
- Every experiment is performed on a regenerated `WORK\BASELINE`.
- `CookedXenon` is never renamed to or passed off as `CookedPC`.
- Diagnostic executable patches are applied only to the working copy and are hash-gated.
- Runtime logs are **not uploaded to GitHub** because they can contain local machine/user/network details.

## One-click workflow

Keep a single `HIGHLANDER_THESEUS_AUTO.cmd` beside (or below) the Highlander build folders.

Each run:

1. downloads the latest runner + experiment manifest from this branch;
2. validates the diagnostic bridge hashes;
3. rebuilds the hybrid Build02 + Build07 PC baseline;
4. runs every current experiment variant automatically;
5. snapshots `Launch.log` even while open;
6. terminates only the processes started by the harness;
7. creates one `HIGHLANDER_THESEUS_AUTO_RESULTS_*.zip`.

The only manual handoff should be uploading that one ZIP back to ChatGPT.

## Current experiment

`T01.7_UMAKE_MATRIX`

It runs four controlled variants in one session:

- `A_REPRO_T016` — reproduces the known GPF.
- `B_MOD_HIGHLANDERGAME` — makes `[ModPackages]` non-empty.
- `C_UMAKE_SKIP_FALSE` — bypasses the development UMake call and forces false.
- `D_UMAKE_SKIP_TRUE` — bypasses the same call and forces true.

This is diagnostic only. No variant is treated as a final fix merely because it boots farther.
