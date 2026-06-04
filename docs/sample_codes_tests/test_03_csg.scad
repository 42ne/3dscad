// TEST 03: CSG — union, difference, intersection
// Expected tree: 3 operations, each with 2 children.
// Check: mesh is correct — difference cuts hole, intersection is overlap only.

$fn = 24;

// union of two overlapping spheres
translate([0, 0, 0]) {
    union() {
        sphere(r=8);
        translate([6, 0, 0]) {
            sphere(r=8);
        }
    }
}

// difference: cube minus cylinder hole
translate([30, 0, 0]) {
    difference() {
        cube([16, 16, 16], center=true);
        cylinder(h=20, r=5, center=true);
    }
}

// intersection: cube and sphere
translate([60, 0, 0]) {
    intersection() {
        cube([14, 14, 14], center=true);
        sphere(r=9);
    }
}
