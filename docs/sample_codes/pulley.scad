// 3DScad sample: step pulley (3-speed belt drive)
// Uses only the currently round-trippable subset.

module scene_model() {
    step_count = 3;
    step_r_min = 20;
    step_r_inc = 12;
    step_h = 12;
    flange_h = 5;
    flange_extra = 6;
    hub_r = 10;
    bore_r = 5;
    tier_h = step_h + flange_h * 2;
    total_h = step_count * tier_h;

    difference() {
        union() {
            cylinder(h=total_h, r=hub_r, center=true);
            for (i = [0 : 1 : step_count - 1]) {
                translate([0, 0, -total_h / 2 + i * tier_h + flange_h / 2]) {
                    cylinder(h=flange_h, r=step_r_min + i * step_r_inc + flange_extra, center=true);
                }
                translate([0, 0, -total_h / 2 + i * tier_h + flange_h + step_h / 2]) {
                    cylinder(h=step_h, r=step_r_min + i * step_r_inc, center=true);
                }
                translate([0, 0, -total_h / 2 + i * tier_h + flange_h + step_h + flange_h / 2]) {
                    cylinder(h=flange_h, r=step_r_min + i * step_r_inc + flange_extra, center=true);
                }
            }
        }
        cylinder(h=total_h + 2, r=bore_r, center=true);
    }
}

scene_model();
