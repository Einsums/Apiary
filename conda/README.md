# conda-forge recipe for Apiary

This directory holds the conda recipe for Apiary, kept here for convenience and
local builds. The **canonical** home for a published recipe is the
`conda-forge/apiary-feedstock` repository (created after the staged-recipes PR
below is merged).

## Files

- `meta.yaml` — package metadata, dependencies, and tests.
- `build.sh` — out-of-source CMake configure → build → install into `$PREFIX`.

## Build locally (sanity check before submitting)

```bash
# from the repo root, with conda-build available
conda build conda/ -c conda-forge
```

## Publish on conda-forge

1. Fork [`conda-forge/staged-recipes`](https://github.com/conda-forge/staged-recipes).
2. Copy this directory in as `recipes/apiary/` (the two files above).
3. Open a PR. conda-forge CI builds linux-64 / osx-64 / osx-arm64.
4. Iterate on any LLVM-linkage or test-phase failures (see notes below).
5. A conda-forge member reviews and merges; the bot then creates
   `conda-forge/apiary-feedstock` and adds the listed `recipe-maintainers`.

After that, version bumps and LLVM migrations are PRs against the feedstock
(the autotick bot opens most of them automatically).

## Notes / known tuning points

- **LLVM major.** apiary links libTooling, whose API shifts each LLVM major.
  `meta.yaml` pins `llvmdev`/`clangdev` to the major the source is developed
  against (`llvm_major`). Because apiary is a *tool* (nothing links its LLVM),
  pinning its own toolchain is safe; bump `llvm_major` when the source moves.
- **Windows** is skipped for now (`skip: true  # [win]`) — libTooling on Windows
  is a later effort.
- **Tests** are intentionally light (`apiary --help` + installed-layout checks).
  The golden/`.pyi` suites need a clang resource-dir and SDK set up, which the
  repo's own CI (`.github/workflows/ci.yml`) covers; reproducing that inside the
  conda-build test phase is not worth the fragility.
