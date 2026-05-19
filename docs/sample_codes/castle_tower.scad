// 3DScad sample: medieval castle tower
// Uses only the currently round-trippable subset.

module scene_model() {
    tower_r = 30;
    tower_h = 80;
    wall_t = 8;
    battlement_count = 8;
    battlement_w = 10;
    battlement_h = 14;
    slit_count = 4;

    difference() {
        union() {
            difference() {
                cylinder(h=tower_h, r=tower_r, center=true);
                cylinder(h=tower_h + 2, r=tower_r - wall_t, center=true);
            }
            translate([0, 0, -tower_h / 2 - 5]) {
                cylinder(h=10, r=tower_r + 10, center=true);
            }
            for (i = [0 : 1 : battlement_count - 1]) {
                rotate([0, 0, i * 360 / battlement_count]) {
                    translate([0, tower_r - wall_t / 2, tower_h / 2 + battlement_h / 2]) {
                        cube([battlement_w, wall_t, battlement_h], center=true);
                    }
                }
            }
        }
        for (i = [0 : 1 : slit_count - 1]) {
            rotate([0, 0, i * 360 / slit_count]) {
                translate([0, tower_r, tower_h / 5]) {
                    cube([4, wall_t + 4, tower_h / 3], center=true);
                }
            }
        }
    }
}

scene_model();
