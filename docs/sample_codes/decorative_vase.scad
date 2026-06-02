// 3DScad sample: parametric decorative vase
// Features: boolean operations, tapered form, flared rim, ornamental rings

module scene_model() {
    body_h = 70;
    body_r = 28;
    rim_r = 20;
    base_h = 4;
    base_r = 30;
    neck_h = 6;
    wall = 2;

    union() {
        // Base disc (below body)
        translate([0, 0, -body_h / 2 - base_h / 2]) {
            cylinder(h=base_h, r=base_r, center=true);
        }

        // Hollow tapered body (centered at origin)
        difference() {
            cylinder(h=body_h, r1=body_r, r2=rim_r, center=true);
            cylinder(h=body_h + 1, r1=body_r - wall, r2=rim_r - wall, center=true);
        }

        // Neck ring (above body)
        translate([0, 0, body_h / 2 + neck_h / 2]) {
            cylinder(h=neck_h, r1=rim_r, r2=rim_r + 3, center=true);
        }

        // Lower ornamental ring
        translate([0, 0, -body_h * 0.3]) {
            cylinder(h=1.5, r=body_r + 2, center=true);
        }

        // Upper ornamental ring
        translate([0, 0, body_h * 0.3]) {
            cylinder(h=1.5, r=body_r + (rim_r - body_r) * 0.4 + 2, center=true);
        }
    }
}

scene_model();
