// Exercises the manual SAR fallback emitted by the IR codegen on pre-Constantinople
// EVMs (where the SAR instruction is not available). On Constantinople and later the
// helper uses sar(bits, value); on older EVMs it emits a bitwise xor/div trick that
// must still round toward negative infinity for negative values. This test pins down
// via-IR signed-shift-right semantics against expected SAR values for boundary
// inputs (sign bit set/unset, INT*_MIN, large shift amounts, etc.).
//
// For the int8 cases the byte is placed into the local via inline assembly so that
// Solidity's cleanup-on-read does the sign extension (matching the convention in
// shift_right_garbled_signed_v2.sol); this avoids the ABI v2 decoder rejecting
// non-sign-extended int8 calldata.
contract C {
    function shr_int8(uint256 val, uint8 shift) external pure returns (int256) {
        int8 a;
        assembly { a := val }
        return a >> shift;
    }
    function shr_int256(int256 val, uint256 shift) external pure returns (int256) {
        return val >> shift;
    }
}
// ====
// EVMVersion: <constantinople
// compileViaYul: also
// ----
// shr_int8(uint256,uint8): 0x99, 0x00 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff99
// shr_int8(uint256,uint8): 0x99, 0x01 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffcc
// shr_int8(uint256,uint8): 0x99, 0x07 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// shr_int8(uint256,uint8): 0x99, 0x08 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// shr_int8(uint256,uint8): 0x99, 0xff -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// shr_int8(uint256,uint8): 0x7f, 0x00 -> 0x7f
// shr_int8(uint256,uint8): 0x7f, 0x06 -> 0x01
// shr_int8(uint256,uint8): 0x7f, 0x07 -> 0x00
// shr_int8(uint256,uint8): 0x7f, 0xff -> 0x00
// shr_int8(uint256,uint8): 0x80, 0x00 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff80
// shr_int8(uint256,uint8): 0x80, 0x01 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc0
// shr_int8(uint256,uint8): 0x80, 0x07 -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// shr_int8(uint256,uint8): 0x80, 0xff -> 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// shr_int256(int256,uint256): -1, 0x00 -> -1
// shr_int256(int256,uint256): -1, 0x01 -> -1
// shr_int256(int256,uint256): -1, 0xff -> -1
// shr_int256(int256,uint256): -1, 0x100 -> -1
// shr_int256(int256,uint256): -3, 0x01 -> -2
// shr_int256(int256,uint256): -3, 0x02 -> -1
// shr_int256(int256,uint256): -57896044618658097711785492504343953926634992332820282019728792003956564819968, 0x01 -> -28948022309329048855892746252171976963317496166410141009864396001978282409984
// shr_int256(int256,uint256): -57896044618658097711785492504343953926634992332820282019728792003956564819968, 0xff -> -1
// shr_int256(int256,uint256): -57896044618658097711785492504343953926634992332820282019728792003956564819968, 0x100 -> -1
// shr_int256(int256,uint256): 57896044618658097711785492504343953926634992332820282019728792003956564819967, 0xff -> 0
// shr_int256(int256,uint256): 57896044618658097711785492504343953926634992332820282019728792003956564819967, 0x100 -> 0
