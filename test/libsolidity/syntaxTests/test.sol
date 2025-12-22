library L {
    uint constant T = 0;
}

contract Other {
    uint public constant T = 0;
}

contract Base {
    uint public constant T = 0;
}

uint constant l = L.T;

contract Test is Base
// layout at Base.T // The base slot expression contains elements that are not yet supported by the internal constant evaluator and therefore cannot be evaluated at compilation time.
// layout at L.T    // The base slot expression contains elements that are not yet supported by the internal constant evaluator and therefore cannot be evaluated at compilation time.
// layout at l      // The base slot expression contains elements that are not yet supported by the internal constant evaluator and therefore cannot be evaluated at compilation time.
{
    Other constant other = Other(address(1));
    uint constant testBaseT = Base.T;
    // uint constant testSuperT = super.T;   // Member "T" not found or not visible after argument-dependent lookup in type(contract super Test).
    // uint constant testOtherT = Other.T;   // Member "T" not found or not visible after argument-dependent lookup in type(contract Other).
    uint constant testLibT = L.T;
    // uint constant testOtherT = other.T(); // Initial value for constant variable has to be compile-time constant.
}
