# conda-forge recipe for Apiary

This directory holds the conda recipe for Apiary, kept here for convenience and
local builds. The **canonical** home for a published recipe is the
`conda-forge/apiary-feedstock` repository (created after the staged-recipes PR
below is merged).

## Files

- `recipe.yaml` — the recipe in the v1 (CEP-13 / rattler-build) format
  conda-forge now expects: package metadata, dependencies, inline build script,
  and tests.

## Build locally (sanity check before submitting)

```bash
# in a throwaway env with the build tooling + conda-forge variant pinning
conda create -n cf-build -c conda-forge rattler-build conda-forge-pinning
conda activate cf-build

# rattler-build (v1 engine). -m supplies the conda-forge variant config;
# on macOS also export CONDA_BUILD_SYSROOT to an installed SDK.
rattler-build build --recipe conda/recipe.yaml \
    -m "$CONDA_PREFIX/conda_build_config.yaml" -c conda-forge
```

## Publish on conda-forge

1. Fork [`conda-forge/staged-recipes`](https://github.com/conda-forge/staged-recipes).
2. Copy this directory in as `recipes/apiary/` (just `recipe.yaml`).
3. Open a PR. conda-forge CI builds linux-64 / osx-64 / osx-arm64.
4. Iterate on any LLVM-linkage or test-phase failures (see notes below).
5. A conda-forge member reviews and merges; the bot then creates
   `conda-forge/apiary-feedstock` and adds the listed `recipe-maintainers`.

After that, version bumps and LLVM migrations are PRs against the feedstock
(the autotick bot opens most of them automatically).

## Notes / known tuning points

- **LLVM major.** apiary links libTooling, whose API shifts each LLVM major.
  `recipe.yaml` pins `llvmdev`/`clangdev` to the major the source is developed
  against (the `llvm_major` context var). Because apiary is a *tool* (nothing
  links its LLVM), pinning its own toolchain is safe; bump `llvm_major` when the
  source moves.
- **Runtime libLLVM.** apiary dynamically links `@rpath/libLLVM.<major>.dylib`,
  which lives in the major-suffixed package `libllvm<major>`; it must be an
  explicit `run` dep (llvmdev's transitive run_export does not carry it).
- **Windows** is skipped for now — libTooling on Windows is a later effort.
- **Tests** are intentionally light (`apiary --help` + installed-layout checks
  via `package_contents`). The golden/`.pyi` suites need a clang resource-dir
  and SDK set up, which the repo's own CI (`.github/workflows/ci.yml`) covers;
  reproducing that inside the conda test phase is not worth the fragility.
