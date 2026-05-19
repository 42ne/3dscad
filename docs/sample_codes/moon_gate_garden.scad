// 3DScad sample: moon gate garden
// Uses only the currently round-trippable subset.

module scene_model() {
    gate_radius = 34;
    wall_thickness = 14;

    union() {
        difference() {
            cube([110, wall_thickness, 76], center=true);
            translate([0, 0, 4]) {
                cylinder(h=wall_thickness + 8, r=gate_radius, center=true);
            }
            translate([0, 0, -34]) {
                cube([90, wall_thickness + 10, 34], center=true);
            }
        }

        for (side = [-1 : 2 : 1]) {
            translate([side*70, 0, -16]) {
                cylinder(h=44, r=8, center=true);
            }
            translate([side*70, 0, 10]) {
                sphere(r=11);
            }
        }

        for (stone = [-3 : 1 : 3]) {
            translate([stone*18, -28, -36]) {
                scale([1.25, 0.75, 0.22]) {
                    sphere(r=8);
                }
            }
        }

        translate([0, 26, -38]) {
            cube([132, 22, 6], center=true);
        }
    }
}

scene_model();
