// 3DScad sample: bolted pipe flange
// Uses only the currently round-trippable subset.

module scene_model() {
    flange_r = 52;
    flange_h = 14;
    pipe_r = 22;
    pipe_h = 44;
    bore_r = 18;
    bolt_r = 4;
    bolt_count = 6;
    bolt_circle_r = 40;
    boss_r = 28;
    boss_h = 6;

    difference() {
        union() {
            cylinder(h=flange_h, r=flange_r, center=true);
            translate([0, 0, flange_h / 2 + boss_h / 2]) {
                cylinder(h=boss_h, r=boss_r, center=true);
            }
            translate([0, 0, flange_h / 2 + boss_h + pipe_h / 2]) {
                cylinder(h=pipe_h, r=pipe_r, center=true);
            }
        }
        cylinder(h=flange_h + boss_h + pipe_h + 2, r=bore_r, center=true);
        for (i = [0 : 1 : bolt_count - 1]) {
            rotate([0, 0, i * 360 / bolt_count]) {
                translate([bolt_circle_r, 0, 0]) {
                    cylinder(h=flange_h + 2, r=bolt_r, center=true);
                }
            }
        }
    }
}

scene_model();
