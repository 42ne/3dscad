// 3DScad sample: parametric robot
// Uses only the currently round-trippable subset.

module scene_model() {
    body_w = 42;
    body_d = 24;
    body_h = 56;
    limb_r = 6;

    union() {
        translate([0, 0, 22]) {
            difference() {
                cube([body_w, body_d, body_h], center=true);
                translate([0, -body_d/2 - 1, 36]) {
                    cube([18, 8, 8], center=true);
                }
            }
        }

        translate([0, 0, 62]) {
            difference() {
                cube([34, 28, 26], center=true);
                translate([-8, -16, 4]) {
                    sphere(r=4);
                }
                translate([8, -16, 4]) {
                    sphere(r=4);
                }
            }
        }

        for (side = [-1 : 2 : 1]) {
            translate([side*(body_w/2 + 9), 0, 32]) {
                rotate([0, 18*side, 0]) {
                    cylinder(h=42, r=limb_r, center=true);
                }
            }
            translate([side*12, 0, -24]) {
                cylinder(h=38, r=limb_r + 1, center=true);
            }
            translate([side*12, -2, -46]) {
                cube([24, 18, 8], center=true);
            }
        }

        translate([0, 0, 82]) {
            cylinder(h=18, r=3, center=true);
        }
        translate([0, 0, 94]) {
            sphere(r=6);
        }
    }
}

scene_model();
