==== Source: A ====
uint constant x = 77;

==== Source: B ====
import "A" as M;
contract C layout at M.x{ }
// ----
