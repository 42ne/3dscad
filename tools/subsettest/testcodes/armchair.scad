// 3DScad sample: upholstered armchair
// Uses only the currently round-trippable subset.

module scene_model() {
    seat_w = 72;
    seat_d = 68;
    seat_h = 16;
    leg_h = 40;
    leg_r = 5;
    back_h = 62;
    back_t = 12;
    arm_t = 10;
    arm_h = 34;
    cushion_h = 10;

    union() {
        for (sx = [-1 : 2 : 1]) {
            for (sy = [-1 : 2 : 1]) {
                translate([sx * (seat_w / 2 - leg_r - 2), sy * (seat_d / 2 - leg_r - 2), leg_h / 2]) {
                    cylinder(h=leg_h, r=leg_r, center=true);
                }
            }
        }
        translate([0, 0, leg_h + seat_h / 2]) {
            cube([seat_w, seat_d, seat_h], center=true);
        }
        translate([0, 0, leg_h + seat_h + cushion_h / 2]) {
            scale([1, 1, 0.5]) {
                sphere(r=seat_w / 2);
            }
        }
        translate([0, seat_d / 2 - back_t / 2, leg_h + seat_h + back_h / 2]) {
            cube([seat_w, back_t, back_h], center=true);
        }
        translate([0, seat_d / 2 - back_t / 2, leg_h + seat_h + back_h]) {
            scale([1, 0.6, 0.35]) {
                sphere(r=seat_w / 2);
            }
        }
        for (side = [-1 : 2 : 1]) {
            translate([side * (seat_w / 2 + arm_t / 2), 0, leg_h + seat_h + arm_h / 2]) {
                cube([arm_t, seat_d, arm_h], center=true);
            }
            translate([side * (seat_w / 2 + arm_t / 2), -seat_d / 4, leg_h + seat_h + arm_h]) {
                scale([0.6, 1, 0.3]) {
                    sphere(r=arm_t + 4);
                }
            }
        }
    }
}

scene_model();
