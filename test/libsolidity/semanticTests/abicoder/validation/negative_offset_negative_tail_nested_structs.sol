// Adapted example from a bug report we received.

contract SolidityCalldataAccess {
    event LogBytes(bytes);
    event LogUint256(uint256);

    struct InnerStruct {
        uint256 innerField1;
        bytes innerField2;
        uint256 innerField3;
    }

    struct OuterStruct {
        InnerStruct outerField1;
    }

    function accessAndReturn(OuterStruct calldata s) external returns (uint256, bytes memory, uint256) {
        return (                       // With InnerStruct at offset -100 these map to:
            s.outerField1.innerField1, // -96..-64
            s.outerField1.innerField2, // -66..0
            s.outerField1.innerField3  //   0..32 (which includes function selector)
        );
    }

    function accessAndEmit(OuterStruct calldata s) external {
        emit LogUint256(s.outerField1.innerField1);
        emit LogBytes(s.outerField1.innerField2);
        emit LogUint256(s.outerField1.innerField3); // Note that this value is not the same as in accessAndReturn() due to selector
    }

    function f(uint256, bytes memory, uint256) external {}

    function accessAndPassIntoCall(OuterStruct calldata s) external {
        this.f(
            s.outerField1.innerField1,
            s.outerField1.innerField2,
            s.outerField1.innerField3
        );
    }
}
// NOTE: evmasm behavior differs from IR - IR does report a validation error in accessAndReturn()
// ====
// revertStrings: debug
// compileViaYul: false
// ----
// accessAndReturn(((uint256,bytes,uint256))): 32, -100 -> 0, 0x60, 0x72cf883500000000000000000000000000000000000000000000000000000000, 0
// accessAndEmit(((uint256,bytes,uint256))): 32, -100 ->
// ~ emit LogUint256(uint256): 0x00
// ~ emit LogBytes(bytes): 0x20, 0x00
// ~ emit LogUint256(uint256): 0x185a652e00000000000000000000000000000000000000000000000000000000
// accessAndPassIntoCall(((uint256,bytes,uint256))): 32, -100 ->
