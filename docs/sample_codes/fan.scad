// 3DScad sample: axial cooling fan with swept blades
// Uses only the currently round-trippable subset.

module scene_model() {
    hub_r = 16;
    hub_h = 22;
    blade_count = 7;
    blade_l = 54;
    blade_w = 20;
    blade_h = 4;
    blade_pitch = 28;
    bore_r = 5;

    difference() {
        union() {
            cylinder(h=hub_h, r=hub_r, center=true);
            for (i = [0 : 1 : blade_count - 1]) {
                rotate([0, 0, i * 360 / blade_count]) {
                    translate([hub_r + blade_l / 2, blade_l / 6, 0]) {
                        rotate([blade_pitch, 0, 15]) {
                            cube([blade_l, blade_w, blade_h], center=true);
                        }
                    }
                }
            }
        }
        cylinder(h=hub_h + 2, r=bore_r, center=true);
    }
}

scene_model();
