// 3DScad sample: dining table with turned legs
// Uses only the currently round-trippable subset.

module scene_model() {
    top_w = 160;
    top_d = 90;
    top_h = 6;
    leg_h = 72;
    leg_r_top = 7;
    leg_r_mid = 4;
    leg_r_bot = 6;
    inset = 16;
    apron_h = 12;
    apron_t = 5;

    union() {
        translate([0, 0, leg_h + top_h / 2]) {
            cube([top_w, top_d, top_h], center=true);
        }
        translate([0, 0, leg_h - apron_h / 2]) {
            cube([top_w - inset * 2, apron_t, apron_h], center=true);
        }
        translate([0, 0, leg_h - apron_h / 2]) {
            cube([apron_t, top_d - inset * 2, apron_h], center=true);
        }
        for (sx = [-1 : 2 : 1]) {
            for (sy = [-1 : 2 : 1]) {
                translate([sx * (top_w / 2 - inset), sy * (top_d / 2 - inset), leg_h * 3 / 4]) {
                    cylinder(h=leg_h / 2, r=leg_r_top, center=true);
                }
                translate([sx * (top_w / 2 - inset), sy * (top_d / 2 - inset), leg_h / 4]) {
                    cylinder(h=leg_h / 2, r=leg_r_mid, center=true);
                }
                translate([sx * (top_w / 2 - inset), sy * (top_d / 2 - inset), leg_r_bot / 2]) {
                    cylinder(h=leg_r_bot, r=leg_r_bot, center=true);
                }
            }
        }
    }
}

scene_model();
