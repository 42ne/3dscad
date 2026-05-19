// 3DScad sample: snowflake crystal (6-fold symmetry)
// Uses only the currently round-trippable subset.

module scene_model() {
    arm_r = 4;
    arm_l = 52;
    branch_count = 4;
    branch_l = 18;
    branch_r = 2;
    center_r = 7;
    thickness = 5;

    union() {
        scale([1, 1, 0.5]) {
            sphere(r=center_r);
        }
        for (i = [0 : 1 : 5]) {
            rotate([0, 0, i * 60]) {
                translate([arm_l / 2, 0, 0]) {
                    rotate([0, 90, 0]) {
                        cylinder(h=arm_l, r=arm_r, center=true);
                    }
                }
                for (j = [1 : 1 : branch_count]) {
                    translate([j * arm_l / (branch_count + 1), 0, 0]) {
                        rotate([0, 90, 60]) {
                            cylinder(h=branch_l, r=branch_r, center=true);
                        }
                        rotate([0, 90, -60]) {
                            cylinder(h=branch_l, r=branch_r, center=true);
                        }
                    }
                }
            }
        }
    }
}

scene_model();
