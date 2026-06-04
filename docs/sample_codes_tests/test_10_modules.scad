// TEST 10: Module definitions and calls
// Expected tree: module definitions + 3 call instances.
// Check: positional args map correctly (bolt(5, 10) => r=5, h=10).
// Check: named args work (bolt(h=8, r=3)).
// Check: default params used when arg omitted.

module bolt(r=4, h=12) {
    cylinder(h=h, r=r);
    translate([0, 0, h]) {
        sphere(r=r*1.4);
    }
}

module bracket(w=20, d=10, t=3) {
    union() {
        cube([w, t, d]);
        cube([t, d, d]);
    }
}

// positional call
translate([0, 0, 0]) {
    bolt(5, 10);
}

// named call — different order
translate([20, 0, 0]) {
    bolt(h=8, r=3);
}

// defaults only
translate([40, 0, 0]) {
    bolt();
}

// bracket with partial named args
translate([0, 30, 0]) {
    bracket(w=30);
}
