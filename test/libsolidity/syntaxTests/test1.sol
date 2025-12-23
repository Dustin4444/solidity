contract Base {
    function fooBaseExternal() external {}
    function fooVirtual() public virtual {}
}

contract Test is Base
{
    Test t;
    function fooVirtual() public override {}
    function () external tFooVirtualPtr = t.fooVirtual;
    function () external thisFooVirtualPtr = this.fooVirtual;
    // function () external constant tFooVirtualConstPtr = t.fooVirtual;    // Initial value for constant variable has to be compile-time constant.
    // function () external constant fooVirtualConstPtr = this.fooVirtual; // Initial value for constant variable has to be compile-time constant.

    function () external tFooExternalPtr = t.fooBaseExternal;
    function () external thisFooExternalPtr = this.fooBaseExternal;
    // function () external constant tFooExternalConstPtr = t.fooBaseExternal;       // Initial value for constant variable has to be compile-time constant.
    // function () external constant thisFooExternalConstPtr = this.fooBaseExternal; // Initial value for constant variable has to be compile-time constant.

    bytes4 tFooVirtualPtrSelector = tFooVirtualPtr.selector;
    bytes4 thisFooVirtualPtrSelector = thisFooVirtualPtr.selector;
    // bytes4 constant tFooVirtualPtrSelector = tFooExternalConstPtr.selector;       // Initial value for constant variable has to be compile-time constant.
    // bytes4 constant thisFooVirtualPtrSelector = thisFooExternalConstPtr.selector; // Initial value for constant variable has to be compile-time constant.

    //bytes4 constant fooConstPtrSelector = fooConstPtr.selector;
    //bytes4 constant thisFooSelector = this.foo.selector;
    //bytes4 constant fooSelector = t.foo.selector;

    //function () external baseFooPtr = Base.f;
    //function () external constant baseFooConstPtr = Base.f;

    // bytes4 constant baseFooPtrSelector = baseFooPtr.selector;
    // bytes4 constant baseFooConstPtrSelector = baseFooConstPtr.selector;
    // bytes4 constant superFooSelector = super.foo.selector; // Member "foo" not found or not visible after argument-dependent lookup in type(contract super Test).
    // bytes4 constant superFSelector = super.f.selector;
    //bytes4 constant fSelector = Base.f.selector;

    function test() private {
        //super.f();
        //super.foo();
    }
}
