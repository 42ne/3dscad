// TEST 15: Round-trip fidelity
// Load this file, then copy the generated code and load it again.
// Expected: both loads produce identical trees and meshes.
// Tests: all main node types in one scene.

$fn = 20;

size = 12;
gap  = 18;

// cube
translate([0, 0, 0]) {
    cube([size, size, size]);
}

// sphere
translate([gap, 0, 0]) {
    sphere(r=size/2);
}

// cylinder r
translate([gap*2, 0, 0]) {
    cylinder(h=size, r=size/2);
}

// cylinder d — round-trip must keep d=
translate([gap*3, 0, 0]) {
    cylinder(h=size, d=size);
}

// difference
translate([gap*4, 0, 0]) {
    difference() {
        cube([size, size, size], center=true);
        sphere(r=size/2 - 1);
    }
}

// linear_extrude circle
translate([gap*5, 0, 0]) {
    linear_extrude(height=size) {
        circle(r=size/2);
    }
}

// rotate_extrude
translate([gap*6, 0, 0]) {
    rotate_extrude(angle=360) {
        translate([size/2, 0, 0]) {
            circle(r=3);
        }
    }
}
