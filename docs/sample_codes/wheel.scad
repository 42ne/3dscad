// 3DScad sample: spoked wheel
// Uses only the currently round-trippable subset.

module scene_model() {
    rim_r = 50;
    rim_h = 8;
    rim_t = 5;
    tire_t = 10;
    tire_w = 16;
    hub_r = 10;
    hub_h = 14;
    spoke_count = 6;
    spoke_r = 3;

    union() {
        cylinder(h=hub_h, r=hub_r, center=true);
        difference() {
            cylinder(h=rim_h, r=rim_r, center=true);
            cylinder(h=rim_h + 2, r=rim_r - rim_t, center=true);
        }
        difference() {
            cylinder(h=tire_w, r=rim_r + tire_t, center=true);
            cylinder(h=tire_w + 2, r=rim_r, center=true);
        }
        for (i = [0 : 1 : spoke_count - 1]) {
            rotate([0, 0, i * 360 / spoke_count]) {
                translate([(hub_r + rim_r - rim_t) / 2, 0, 0]) {
                    rotate([0, 90, 0]) {
                        cylinder(h=rim_r - rim_t - hub_r, r=spoke_r, center=true);
                    }
                }
            }
        }
    }
}

scene_model();
