// TEST 06: linear_extrude with 2D shapes
// Expected: extruded circle = cylinder-like; extruded square = box.
// Check: height parameter in card matches code value.
// Check: twist/scale parameters in card (if shown).

$fn = 32;

// circle extruded — effectively a cylinder
translate([0, 0, 0]) {
    linear_extrude(height=15) {
        circle(r=6);
    }
}

// square extruded
translate([20, 0, 0]) {
    linear_extrude(height=10) {
        square([12, 8]);
    }
}

// circle extruded with twist
translate([40, 0, 0]) {
    linear_extrude(height=20, twist=90) {
        circle(r=5);
    }
}

// square extruded with scale taper
translate([60, 0, 0]) {
    linear_extrude(height=15, scale=0.3) {
        square([10, 10], center=true);
    }
}
