// 3DScad sample: lantern tower
// Uses only the currently round-trippable subset.

module scene_model() {
    floor_gap = 28;
    tower_radius = 22;

    union() {
        for (level = [0 : 1 : 4]) {
            translate([0, 0, level*floor_gap]) {
                difference() {
                    cylinder(h=12, r=tower_radius, center=true);
                    cylinder(h=16, r=tower_radius - 6, center=true);
                }
            }

            translate([0, 0, level*floor_gap + 12]) {
                rotate([0, 0, level*18]) {
                    for (post = [0 : 1 : 3]) {
                        rotate([0, 0, post*90]) {
                            translate([tower_radius - 4, 0, 0]) {
                                cylinder(h=24, r=3, center=true);
                            }
                        }
                    }
                }
            }
        }

        translate([0, 0, 4*floor_gap + 26]) {
            difference() {
                scale([1, 1, 0.55]) {
                    sphere(r=26);
                }
                translate([0, 0, -18]) {
                    cube([70, 70, 24], center=true);
                }
            }
        }

        translate([0, 0, -8]) {
            cylinder(h=16, r=30, center=true);
        }
    }
}

scene_model();
