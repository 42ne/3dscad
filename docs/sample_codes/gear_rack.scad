// 3DScad sample: linear gear rack
// Uses only the currently round-trippable subset.

module scene_model() {
    rack_l = 120;
    rack_w = 20;
    rack_h = 12;
    tooth_count = 10;
    tooth_w = 5;
    tooth_gap = 7;
    tooth_h = 7;
    hole_r = 4;

    difference() {
        union() {
            cube([rack_l, rack_w, rack_h], center=true);
            for (i = [0 : 1 : tooth_count - 1]) {
                translate([-(tooth_count - 1) * (tooth_w + tooth_gap) / 2 + i * (tooth_w + tooth_gap), 0, rack_h / 2 + tooth_h / 2]) {
                    cube([tooth_w, rack_w - 6, tooth_h], center=true);
                }
            }
        }
        for (i = [0 : 1 : 3]) {
            translate([-(rack_l / 2 - 10) + i * (rack_l - 20) / 3, 0, 0]) {
                cylinder(h=rack_h + 2, r=hole_r, center=true);
            }
        }
    }
}

scene_model();
