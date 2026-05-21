// Recursive call prevents inlining, so f keeps its 18 args. The discovery loop spills the
// deepest unreachable parameters; codegen bumps memoryGuard from 0x80 by `32 * numSpilled`
// bytes and emits DUP+MSTORE/MLOAD for spilled values. The mstore(0x80, ...) ensures
// the optimizer can't elide the memoryguard call.
object "C" {
    code {
        let g := memoryguard(0x80)
        mstore(0x40, g)
        sstore(123, g)
        f(calldataload(0), calldataload(0x20), calldataload(0x40), calldataload(0x60), calldataload(0x80), calldataload(0xa0), calldataload(0xc0), calldataload(0xe0), calldataload(0x100), calldataload(0x120), calldataload(0x140), calldataload(0x160), calldataload(0x180), calldataload(0x1a0), calldataload(0x1c0), calldataload(0x1e0), calldataload(0x200), calldataload(0x220))
        function f(b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18) {
            sstore(b18, b1)
            sstore(b17, b2)
            sstore(b16, b3)
            sstore(b15, b4)
            sstore(b14, b5)
            sstore(b13, b6)
            sstore(b12, b7)
            sstore(b11, b8)
            sstore(b10, b9)
            if gt(b1, 0) {
                f(sub(b1, 1), b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18)
            }
            sstore(b9, b10)
            sstore(b8, b11)
            sstore(b7, b12)
            sstore(b6, b13)
            sstore(b5, b14)
            sstore(b4, b15)
            sstore(b3, b16)
            sstore(b2, b17)
            sstore(b1, b18)
        }
    }
}
