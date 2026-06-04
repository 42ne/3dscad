// TEST 12: echo() and assert() — must be silently skipped
// Expected: only the cube is visible; echo/assert lines are ignored.
// Check: no parse error, tree contains exactly one cube.

$fn = 16;

r = 10;

echo("Test 12: echo and assert skip");
echo("r =", r, "  height =", r * 2);

assert(r > 0, "radius must be positive");
assert(r < 100);

cube([r, r, r]);

echo("done");
