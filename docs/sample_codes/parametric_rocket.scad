// 3DScad sample: parametric rocket
// Features: for-loop fins, conical nose, boolean operations, symmetrical design

module scene_model() {
    body_r = 10;
    body_h = 60;
    nose_h = 16;
    fin_count = 4;
    fin_w = 8;
    fin_h = 18;
    fin_t = 2;
    port_r = 3;
    port_h = 4;

    union() {
        // Main body
        cylinder(h=body_h, r=body_r, center=true);

        // Nose cone
        translate([0, 0, body_h / 2 + nose_h / 2]) {
            cylinder(h=nose_h, r1=body_r, r2=0, center=true);
        }

        // Engine nozzle
        translate([0, 0, -body_h / 2 - port_h / 2]) {
            difference() {
                cylinder(h=port_h, r1=port_r + 2, r2=port_r, center=true);
                cylinder(h=port_h + 1, r=port_r - 1, center=true);
            }
        }

        // Tail fins
        for (i = [0 : fin_count - 1]) {
            rotate([0, 0, i * 360 / fin_count]) {
                translate([body_r + fin_w / 2, 0, -body_h / 2 + fin_h / 2]) {
                    cube([fin_w, fin_t, fin_h], center=true);
                }
            }
        }

        // Decorative window band
        translate([0, 0, body_h * 0.2]) {
            cylinder(h=2, r=body_r + 1, center=true);
        }

        // Decorative nose band
        translate([0, 0, body_h / 2 - 2]) {
            cylinder(h=1.5, r=body_r + 0.5, center=true);
        }
    }
}

scene_model();
