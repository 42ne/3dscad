// 3DScad tutorial example: simple car
// Adapted from the OpenSCAD User Manual, Chapter 5
// https://github.com/openscad/documentation (public domain)
// Uses only the currently round-trippable subset.

$fn = 40;

// Parameters — global so module thumbnails resolve correctly.
wheel_r = 8;
wheel_w = 4;
axle_r  = 2;

// A single wheel: a flat cylinder rotated so it faces along Y.
module wheel(r = wheel_r, w = wheel_w) {
    rotate([90, 0, 0]) {
        cylinder(h=w, r=r, center=true);
    }
}

// A thin axle rod spanning the track width.
module axle(len = 32) {
    rotate([90, 0, 0]) {
        cylinder(h=len, r=axle_r, center=true);
    }
}

module scene_model() {
    body_l   = 60;
    body_w   = 20;
    base_h   = 10;
    top_h    = 10;
    top_l    = 30;
    top_dx   = 5;
    track    = 32;
    front_x  = -20;
    rear_x   = 20;
    half_t   = track / 2;

    // Body base
    cube([body_l, body_w, base_h], center=true);
    // Cabin on top
    translate([top_dx, 0, base_h / 2 + top_h / 2 - 0.001]) {
        cube([top_l, body_w, top_h], center=true);
    }

    // Front left wheel
    translate([front_x, -half_t, 0]) {
        wheel();
    }
    // Front right wheel
    translate([front_x, half_t, 0]) {
        wheel();
    }
    // Front axle
    translate([front_x, 0, 0]) {
        axle(len=track);
    }

    // Rear left wheel
    translate([rear_x, -half_t, 0]) {
        wheel();
    }
    // Rear right wheel
    translate([rear_x, half_t, 0]) {
        wheel();
    }
    // Rear axle
    translate([rear_x, 0, 0]) {
        axle(len=track);
    }
}

scene_model();
