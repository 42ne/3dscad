// 3DScad sample: Japanese pagoda
// Uses only the currently round-trippable subset.

module scene_model() {
    tiers = 5;
    base_r = 34;
    tier_h = 16;
    wall_h = 10;
    shrink = 6;
    overhang = 9;

    union() {
        translate([0, 0, -8]) {
            cylinder(h=16, r=base_r + 14, center=true);
        }
        for (i = [0 : 1 : tiers - 1]) {
            translate([0, 0, i * tier_h + wall_h / 2]) {
                cylinder(h=wall_h, r=base_r - i * shrink, center=true);
            }
            translate([0, 0, i * tier_h + wall_h]) {
                scale([1, 1, 0.22]) {
                    cylinder(h=overhang * 2, r=base_r - i * shrink + overhang, center=true);
                }
            }
        }
        translate([0, 0, tiers * tier_h + wall_h + 4]) {
            cylinder(h=tiers * tier_h / 2, r=3, center=true);
        }
        translate([0, 0, tiers * tier_h + wall_h + 4 + tiers * tier_h / 2 / 2]) {
            sphere(r=5);
        }
    }
}

scene_model();
