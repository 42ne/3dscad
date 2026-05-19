// 3DScad sample: orbital space station
// Uses only the currently round-trippable subset.

module scene_model() {
    hub_r = 18;
    hub_h = 24;
    arm_l = 90;
    arm_r = 5;
    panel_l = 44;
    panel_w = 62;
    panel_t = 2;
    ring_r = 58;

    union() {
        cylinder(h=hub_h, r=hub_r, center=true);
        translate([0, 0, hub_h / 2 + 8]) {
            cylinder(h=16, r=10, center=true);
        }
        translate([0, 0, -hub_h / 2 - 5]) {
            cylinder(h=10, r=13, center=true);
        }
        for (i = [0 : 1 : 3]) {
            rotate([0, 0, i * 90]) {
                translate([arm_l / 2 + hub_r, 0, 0]) {
                    rotate([0, 90, 0]) {
                        cylinder(h=arm_l, r=arm_r, center=true);
                    }
                }
                translate([arm_l + hub_r, 0, panel_l / 2 + arm_r]) {
                    cube([panel_t, panel_w, panel_l], center=true);
                }
                translate([arm_l + hub_r, 0, -(panel_l / 2 + arm_r)]) {
                    cube([panel_t, panel_w, panel_l], center=true);
                }
            }
        }
        for (j = [0 : 1 : 11]) {
            rotate([0, 0, j * 30]) {
                translate([ring_r, 0, 0]) {
                    cube([14, 10, 10], center=true);
                }
                translate([ring_r + 8, 0, 0]) {
                    rotate([0, 90, 0]) {
                        cylinder(h=8, r=3, center=true);
                    }
                }
            }
        }
    }
}

scene_model();
