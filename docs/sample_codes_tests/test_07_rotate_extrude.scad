// TEST 07: rotate_extrude
// Expected: full ring (angle=360) and partial arc (angle=180).
// Check: angle parameter in card matches code value.
// Check: CSG mesh is non-empty torus-like shape.

$fn = 48;

// full 360 degree torus profile from a circle
translate([0, 0, 0]) {
    rotate_extrude(angle=360) {
        translate([10, 0, 0]) {
            circle(r=3);
        }
    }
}

// partial arc — 180 degrees
translate([40, 0, 0]) {
    rotate_extrude(angle=180) {
        translate([8, 0, 0]) {
            square([4, 6]);
        }
    }
}
