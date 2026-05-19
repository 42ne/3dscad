// 3DScad sample: Saturn-like ringed planet
// Uses only the currently round-trippable subset.

module scene_model() {
    planet_r = 32;
    ring_r = 80;
    ring_gap = 10;
    ring_tilt = 26;

    union() {
        scale([1, 1, 0.88]) {
            sphere(r=planet_r);
        }
        rotate([ring_tilt, 0, 0]) {
            difference() {
                scale([1, 1, 0.1]) {
                    sphere(r=ring_r);
                }
                cylinder(h=ring_r, r=planet_r + ring_gap, center=true);
            }
        }
        rotate([ring_tilt, 0, 0]) {
            difference() {
                scale([1, 1, 0.07]) {
                    sphere(r=ring_r - 14);
                }
                cylinder(h=ring_r, r=planet_r + ring_gap + 4, center=true);
            }
        }
    }
}

scene_model();
