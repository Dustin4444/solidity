# New EVMOne Fuzzer Proposals

## Current EVMOne Differential Coverage

| Fuzzer | Config A | Config B | What it isolates |
|--------|----------|----------|-----------------|
| `sol_proto_ossfuzz_evmone` | noOpt, viaIR=X | opt, viaIR=X | Solidity optimizer (same codegen) |
| `sol_proto_ossfuzz_evmone_viair` | noOpt, legacy | opt, viaIR | Codegen + optimizer (conflated!) |
| `yul_proto_ossfuzz_evmone` | noOpt, legacy | opt, legacy | Yul optimizer (legacy codegen) |
| `yul_proto_ossfuzz_evmone_ssacfg` | noOpt, legacy | opt, SSA CFG | SSA CFG backend (Yul level) |
| `yul_proto_ossfuzz_evmone_check_stack_alloc` | opt, stackAlloc=off | opt, stackAlloc=on | Stack allocation pass |

All fuzzers use latest EVM version. Comparison checks: output data, logs
(ignoring creator address), storage (non-zero slots), status codes. Gas-related
mismatches are skipped.

## Gaps in Coverage

1. **SSA CFG never tested from Solidity level.** The SSA CFG backend is the
   newest codegen path but only fuzzed via the Yul proto generator, which
   produces simpler patterns than Solidity-generated Yul. Solidity-level
   fuzzing would exercise ABI encoding, storage layout, inheritance dispatch,
   and other complex Yul IR patterns that the Yul proto cannot produce.

2. **The viair fuzzer conflates two variables.** It compares `noOpt+legacy` vs
   `opt+viaIR` — both optimization level and codegen backend change. When a
   mismatch is found, you cannot tell which change caused it. Clean
   single-variable comparisons are missing:
   - noOpt legacy vs noOpt viaIR (isolates codegen)
   - opt legacy vs opt viaIR (isolates codegen under optimization)

3. **No dedicated viaIR optimizer fuzzer.** `sol_proto_ossfuzz_evmone` tests
   noOpt-vs-opt, but viaIR is proto-controlled (random). Half the corpus tests
   legacy optimizer, half tests IR optimizer. A dedicated always-viaIR variant
   would double coverage of IR optimizer paths.

4. **Yul SSA CFG fuzzer also conflates variables.** Current
   `yul_proto_ossfuzz_evmone_ssacfg` compares unoptimized legacy vs optimized
   SSA CFG — both optimization and backend differ.

## Proposed Fuzzers

### Tier 1 — Highest impact

#### A. `sol_proto_ossfuzz_evmone_ssacfg` — Solidity + SSA CFG backend

- **Config A:** noOpt, viaIR=true, legacy codegen
- **Config B:** opt, viaIR=true, SSA CFG codegen
- **Why:** SSA CFG is the future default and the most bug-prone new component.
  Testing from Solidity level generates far richer Yul IR than the Yul proto
  fuzzer can. Probably the single highest-value fuzzer to add.
- **Implementation:** New `FUZZER_MODE_SSACFG` in `solProtoFuzzer2.cpp`. Force
  viaIR=true for both configs. Config B uses SSA CFG assembly. Need to wire
  SSA CFG codegen through `SolidityEvmoneInterface` (currently only
  `YulEvmoneInterface` supports it). Add CMake target.

#### B. `sol_proto_ossfuzz_evmone_codegen` — Clean codegen differential (noOpt)

- **Config A:** noOpt, legacy (viaIR=false)
- **Config B:** noOpt, viaIR=true
- **Why:** Isolates codegen bugs with zero optimizer noise. The existing viair
  fuzzer conflates optimizer + codegen, so mismatches are harder to diagnose.
  This gives clean signal.
- **Implementation:** New `FUZZER_MODE_CODEGEN` in `solProtoFuzzer2.cpp`. Both
  use `OptimiserSettings::minimal()`, only viaIR differs. Add CMake target.

### Tier 2 — High impact

#### C. `sol_proto_ossfuzz_evmone_codegen_opt` — Clean codegen differential (optimized)

- **Config A:** opt, legacy (viaIR=false)
- **Config B:** opt, viaIR=true
- **Why:** Tests optimizer+codegen interaction. Different from (B) because
  optimizer transforms can expose codegen bugs that only manifest on optimized
  IR. Different from existing viair because both sides are optimized — clean
  single-variable comparison.
- **Implementation:** New `FUZZER_MODE_CODEGEN_OPT` in `solProtoFuzzer2.cpp`.
  Both use `OptimiserSettings::standard()`, only viaIR differs. Add CMake
  target.

#### D. `yul_proto_ossfuzz_evmone_ssacfg_clean` — Clean SSA CFG comparison

- **Config A:** opt, legacy codegen
- **Config B:** opt, SSA CFG codegen
- **Why:** Current `yul_proto_ossfuzz_evmone_ssacfg` compares unoptimized
  legacy vs optimized SSA CFG — two variables changed. This holds optimization
  constant and only varies the backend. Cleaner signal, easier triage.
- **Implementation:** New `FUZZER_MODE_SSACFG_CLEAN` in
  `yulProtoFuzzerEvmone.cpp`. Both configs optimized, only codegen backend
  differs. Add CMake target.

### Tier 3 — Worth considering later

#### E. Solidity ABI round-trip fuzzer

Generate contracts that call each other with complex types (structs, dynamic
arrays, nested types), comparing ABI encoding correctness across opt/noOpt and
legacy/viaIR. Currently sol2Proto generates mostly single-contract tests.
Multi-contract interaction with external calls would stress ABI encode/decode
paths much harder. Requires significant proto schema extensions.

#### F. Cross-EVM-version fuzzer

Compile same source for two EVM versions (e.g. Shanghai vs Cancun) and compare
semantics on opcodes that exist in both. Would catch version-gating bugs.
Requires careful filtering of version-specific features.

## Implementation Plan

All Tier 1 and Tier 2 fuzzers reuse existing infrastructure
(`solProtoFuzzer2.cpp`, `yulProtoFuzzerEvmone.cpp`, EvmoneUtility, proto
schemas). The work is primarily:

1. Add new `#ifdef` modes to the existing fuzzer `.cpp` files
2. For (A): wire SSA CFG codegen through `SolidityEvmoneInterface` — currently
   only `YulEvmoneInterface` supports the `_viaSSACFG` flag. This is the main
   new code.
3. Add CMake targets in `test/tools/ossfuzz/CMakeLists.txt` with the
   appropriate compile definitions
4. Add `.options` files for oss-fuzz configuration
5. Update `README.md` with the new executables
6. Update `project.yaml` / `build_ossfuzz.sh` if needed for oss-fuzz
   integration

### Recommended order

1. **(B) codegen** — simplest, no new infra needed, just a new `#ifdef` mode
2. **(C) codegen_opt** — same as (B) but with `standard()` settings
3. **(D) ssacfg_clean** — simple Yul-side addition
4. **(A) ssacfg** — most impactful but needs SSA CFG wiring in
   SolidityEvmoneInterface

Start with B because it validates the pattern (new `#ifdef` + CMake target)
with minimal risk, then proceed to the others.
