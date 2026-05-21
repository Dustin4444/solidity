# Demand-driven phi pre-image spill — design plan

Successor / refinement to the conservative cascade currently in
`CodeTransform::run` (between Pass 1 and the spill-region reservation).
The cascade unconditionally spills every non-literal upsilon input of every
spilled phi. This document describes a more precise alternative that only
spills `v_i` when it would actually be unreachable for the per-edge MSTORE.

## Motivation

When a phi `v_k` is in the spill set, codegen emits at each predecessor edge
`A → B(v_k)`:

```
DUP_n / PUSH lit / MLOAD     ; materialize v_i (the upsilon input from A)
PUSH addr(v_k)
MSTORE
```

The `DUP_n` path requires `v_i` to be at depth `≤ 16` at that emit moment.
The layout generator's `ReachableStackDepth = 16` only constrains the slots
the shuffler manipulates (top region) — the *committed* layout can leave
`v_i` at depth > 16 if it was already there before the shuffle began. Today
the conservative cascade closes this hazard by force-spilling every
non-literal `v_i`, which is correct but wastes spill slots and pays an extra
`DUP+MSTORE` at every `v_i` def site.

The demand-driven post-pass spills `v_i` only when its actual depth at the
predecessor's exit truly exceeds the DUP range.

## Pipeline placement

Insert between Pass 1 (per-CFG `StackLayoutGenerator::generate`) and the
spill-region reservation in `CodeTransform::run`. Replaces the conservative
cascade loop currently at that point. After the post-pass, every spilled
phi has at every edge a `v_i` that is one of:

- a literal (codegen pushes the constant);
- already in `spillSet` (codegen `MLOAD`s);
- on stack at depth `≤ 16` (codegen `DUP_n`s).

So `emitSpilledPhiMStores`'s `assert(dupDepth ≤ 16)` becomes a hard
invariant guaranteed by construction.

## Per-edge simulation

For each CFG, for each spilled phi `v_k`, for each upsilon edge `A → B(v_k)`:

1. Reconstruct the symbolic stack the codegen will see at the moment
   `prepareBlockExitStack(B)` is invoked — it's
   `pulledBackTarget = stackPreImage(layouts[B].stackIn, PhiInverse(A, B))`,
   the same value the codegen computes later. The shuffler will land the
   stack on this exact layout.
2. Locate `v_i`'s shallowest occurrence in `pulledBackTarget`. Note that
   `stackPreImage` already replaced phi `v_k` with `v_i` at v_k's offset,
   but `v_i` may also appear elsewhere if it was already in liveOut(A);
   pick the smallest depth.
3. `depth = pulledBackTarget.size() − offset`.
4. Decide:
   - `v_i` is a literal → no action.
   - `v_i` is already in `spillSet` → no action.
   - `depth ≤ 16` → no action.
   - else → `spillSet.spill(v_i)`; if `v_i` is itself a phi, queue it.

## Iteration

Spilling `v_i` may make `v_i` itself a phi whose own upsilons need
depth-checking transitively:

```
queue ← every phi currently in spillSet
while queue not empty:
    phi ← queue.pop()
    for each edge A → B containing phi:
        let pulledBack = stackPreImage(layouts[B].stackIn, PhiInverse(A, B))
        for each (phi_k, v_i) in PhiInverse(A, B) where phi_k is spilled:
            decide as above
            if a new phi was spilled: queue.push(new_phi)
```

Termination: each iteration either makes no change or strictly grows
`spillSet`; `spillSet` is bounded by the number of values in the CFG.

## What about back-edges?

The layout generator's proposal-merging at loop heads picks a single
`stackIn` per block; predecessors' exits target it. So
`stackPreImage(layouts[B].stackIn, PhiInverse(A, B))` is well-defined for
every `(A, B)` edge — including back-edges — after Pass 1 has finished.

## Cost model

The current `findOptimalTarget` cost is `numOps + 1000 · numSpilled`. The
post-pass adds spills but doesn't re-run the size search; the post-pass
spills are mandatory rather than chosen. This is a deliberate compromise:
re-doing the size search would compound complexity and the cost search
isn't needed because we'd be spilling for correctness, not cost.

Optional future work: after the post-pass commits new spills, optionally
re-run `visitBlock` on affected blocks so they can produce looser layouts
now that more values are freely-loadable. Probably overkill for v1.

## Replaces / changes

- **Replaces** the unconditional cascade in `CodeTransform::run` (between
  Pass 1 and the reservation).
- **Tightens** `emitSpilledPhiMStores`'s `dupDepth ≤ 16` assertion to a
  guaranteed invariant.
- **No change** to `StackLayoutGenerator` proper — it keeps its single-pass
  shape.

## Function-arg pre-images (shared concern)

Same hazard as the conservative cascade: spilling a function-arg `v_i`
inherits the pre-existing behavior that function-arg spills don't get an
MSTORE at function entry, so an `MLOAD` reads stale memory. The demand-
driven post-pass should either:

- (a) emit `MSTORE` at function entry for every spilled function arg —
  closes the latent bug as well as enabling cascade through args;
- (b) refuse to spill function args and surface a clearer "stack too deep"
  diagnostic than today's silent `MLOAD`-of-zero.

(a) is preferred. It's separate from the post-pass mechanics but blocks
correctness when a function-arg is the spill candidate.

## Testing

Same test design as the conservative cascade — the only observable
difference is the size of `spillSet` on the same inputs. A useful probe
test asserts that the post-pass *doesn't* over-spill: a phi with one
literal upsilon and one shallow on-stack upsilon should add zero `v_i`s
to the spill set. The conservative cascade adds the on-stack `v_i`; the
demand-driven post-pass leaves it alone.
