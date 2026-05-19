// 3DScad sample: observatory dish
// Uses only the currently round-trippable subset.

module scene_model() {
    mast_height = 72;

    union() {
        translate([0, 0, -8]) {
            cylinder(h=16, r=28, center=true);
        }

        translate([0, 0, mast_height/2]) {
            cylinder(h=mast_height, r=7, center=true);
        }

        translate([0, 0, mast_height + 4]) {
            rotate([22, 0, 0]) {
                difference() {
                    scale([1.35, 1.35, 0.36]) {
                        sphere(r=32);
                    }
                    translate([0, 0, 7]) {
                        scale([1.25, 1.25, 0.34]) {
                            sphere(r=30);
                        }
                    }
                    translate([0, 0, -25]) {
                        cube([90, 90, 38], center=true);
                    }
                }
            }
        }

        translate([0, -18, mast_height + 34]) {
            cylinder(h=42, r=3, center=true);
        }

        for (leg = [0 : 1 : 3]) {
            rotate([0, 0, leg*90 + 45]) {
                translate([22, 0, 14]) {
                    rotate([0, 25, 0]) {
                        cube([6, 6, 58], center=true);
                    }
                }
            }
        }
    }
}

scene_model();
