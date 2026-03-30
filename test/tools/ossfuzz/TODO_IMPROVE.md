# Fuzzer Improvement TODO

## Category 1: Comparison Logic Bugs

### 1.1 Transient storage is never compared
**Files:** `solProtoFuzzer2.cpp:108-111`, `yulProtoFuzzerEvmone.cpp:240-243`

Both fuzzers generate TSTORE/TLOAD operations but the `RunResult` struct only captures
`account.storage`, never `account.transient_storage`. A miscompilation that corrupts
transient storage would be invisible. Fix: capture and compare `transient_storage`
alongside persistent storage.

### 1.2 Solidity fuzzer silently skips revert mismatches (viaIR mode)
**File:** `solProtoFuzzer2.cpp:235-236`

In viaIR mode, if runA (legacy unoptimized) doesn't return `EVMC_SUCCESS`, the fuzzer
returns immediately without running runB at all. This means: if legacy succeeds but
viaIR would have reverted (or vice versa), this mismatch is never detected. The default
mode (line 268) has the same pattern. Compare this to the Yul fuzzer which correctly
compares status codes even for EVMC_REVERT (line 316-317 allows REVERT through, then
line 356-361 asserts status equality).

**Concrete fix:** Run both configurations, then assert status codes match (like the Yul
fuzzer does), filtering only gas-related cases.

### 1.3 Positional storage comparison is fragile
**Files:** `solProtoFuzzer2.cpp:167-183`, `yulProtoFuzzerEvmone.cpp:171-187`

Storage is compared positionally (1st account vs 1st account by sorted address). If
optimization changes account creation order or causes a different number of intermediate
CREATE calls, the comparison silently matches wrong accounts. This is a design choice,
not necessarily a bug — but it could mask real issues where storage ends up on the wrong
account.

## Category 2: Missing Language Features in Solidity Fuzzer

### 2.1 User-Defined Value Types (UDVT) — not generated at all
`type MyUint is uint256;` with `wrap()`/`unwrap()` — has dedicated IR codegen paths.
These are a relatively new feature with complex type conversion logic.

### 2.2 `abi.encodeCall` — not generated
Has significant IR codegen logic for encoding function calls with type-safe arguments.
Not in the proto schema. `abi.decode` with complex types is also absent.

### 2.3 User-defined operators — not generated
`using {f as +} for MyType global;` — allows custom operator overloading for UDVTs.
Complex type checking and binary operator resolution.

### 2.4 Named parameters in error/revert — not generated
The proto has `RevertStmt` with positional args only. A real bug was found in Feb 2026
where named parameter encoding order was broken in IR codegen. The fuzzer couldn't have
found this.

### 2.5 `abi.decode` — not generated
Decoding arbitrary bytes to types exercises complex type handling. Not in proto.

### 2.6 `delegatecall` / `staticcall` — not generated in Solidity fuzzer
Only `.call()` is used. `delegatecall` is fundamental to proxy patterns and has distinct
codegen paths.

### 2.7 Direct typed external calls — not generated
All cross-contract calls use low-level `.call(abi.encodeWithSignature(...))`. The
compiler's high-level external call codegen (with automatic ABI encoding/decoding, error
propagation) is never exercised.

### 2.8 Constructor parameters — not generated
All constructors are called with zero arguments (`new C()`). Constructor parameter
handling, especially with inheritance chains, is untested.

## Category 3: Structural Improvements to Generated Code

### 3.1 Increase complexity limits (DEFERRED)
Current limits are very conservative. We'll revisit this later.

### 3.2 Cross-contract state interaction
Currently each contract's functions are called independently. Contract A never passes
Contract B's address to a function, and there are no callbacks or multi-contract state
flows.

### 3.3 Value transfers with calls
No `.call{value: ...}()` is generated. Payable functions and ETH handling are never
tested despite the proto supporting payable constructors/functions.

## Category 4: Yul Fuzzer Improvements

### 4.1 Objects are completely skipped
`yulProtoFuzzerEvmone.cpp:253-254`: `if (_input.has_obj()) return;` — Yul objects with
sub-objects are never tested. This skips all code paths related to `datasize()`,
`dataoffset()`, `datacopy()`, and nested deployment patterns.

### 4.2 Optimizer sequence exploration is limited
Bracket placement is deterministic (middle half bracketed when >=4 steps). No nested
brackets are ever generated. Empty cleanup sequences can't be generated (falls back to
default).

## Category 5: Harness-Level Improvements

### 5.1 No EVM version variation (INTENTIONAL — we only test latest)

### 5.2 Solidity fuzzer doesn't fuzz optimizer sequences
Unlike the Yul fuzzer which has `buildOptimizerSequence()` for custom optimizer step
ordering, the Solidity fuzzer only tests `OptimiserSettings::minimal()` vs
`OptimiserSettings::standard()`. Adding custom Yul optimizer sequences to the Solidity
proto would test more optimizer interactions in the context of Solidity-generated Yul.

### 5.3 No comparison of revert data
When both runs revert (`EVMC_REVERT`), the revert reason data is not compared. A bug
that changes the revert reason (wrong error selector, wrong parameters) would be missed.

## Priority Ranking (effort vs impact)

| # | Improvement | Effort | Bug-finding potential |
|---|-------------|--------|---------------------|
| 1.1 | Compare transient storage | Low | High |
| 1.2 | Fix revert status comparison in Sol fuzzer | Low | High |
| 5.3 | Compare revert data | Low | Medium |
| 2.4 | Named params in error/revert | Medium | High |
| 2.1 | User-Defined Value Types | Medium | High |
| 2.2 | `abi.encodeCall` | Medium | High |
| 2.6 | delegatecall/staticcall | Medium | Medium |
| 4.1 | Don't skip Yul objects | Medium | Medium |
| 2.7 | Typed external calls | Medium | Medium |
| 5.2 | Custom optimizer seq for Solidity | Low-Medium | Medium |
| 3.2 | Cross-contract interactions | High | Medium |
| 2.3 | User-defined operators | Medium | Medium |
| 3.3 | Value transfers | Low-Medium | Low-Medium |
| 2.8 | Constructor parameters | Low | Low-Medium |
