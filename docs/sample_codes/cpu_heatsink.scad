// 3DScad sample: CPU heatsink
// Uses only the currently round-trippable subset.

module scene_model() {
    base_w = 80;
    base_d = 60;
    base_h = 8;
    fin_count = 9;
    fin_spacing = 7;
    fin_t = 3;
    fin_h = 40;
    hole_r = 3;

    difference() {
        union() {
            cube([base_w, base_d, base_h], center=true);
            for (i = [0 : 1 : fin_count - 1]) {
                translate([-(fin_count - 1) * fin_spacing / 2 + i * fin_spacing, 0, base_h / 2 + fin_h / 2]) {
                    cube([fin_t, base_d - 4, fin_h], center=true);
                }
            }
        }
        for (sx = [-1 : 2 : 1]) {
            for (sy = [-1 : 2 : 1]) {
                translate([sx * (base_w / 2 - 8), sy * (base_d / 2 - 8), 0]) {
                    cylinder(h=base_h + 4, r=hole_r, center=true);
                }
            }
        }
    }
}

scene_model();
