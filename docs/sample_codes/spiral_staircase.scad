// 3DScad sample: spiral staircase
// Uses only the currently round-trippable subset.

module scene_model() {
    steps = 12;
    step_rise = 8;
    step_run = 44;
    step_w = 28;
    step_h = 6;
    post_r = 7;

    union() {
        cylinder(h=steps * step_rise, r=post_r, center=true);
        for (i = [0 : 1 : steps - 1]) {
            translate([0, 0, -steps * step_rise / 2 + i * step_rise + step_h / 2]) {
                rotate([0, 0, i * 360 / steps]) {
                    translate([post_r + step_run / 2, 0, 0]) {
                        cube([step_run, step_w, step_h], center=true);
                    }
                }
            }
        }
    }
}

scene_model();
