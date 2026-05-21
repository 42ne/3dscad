teeth = 18;
gear_radius = 18;
gear_height = 5;

tooth_w = 4;
tooth_l = 7;
tooth_h = 5;

module gear_body() {
    difference() {
        cylinder(h = gear_height, r = gear_radius, center = true);

        cylinder(h = gear_height + 1, r = 5, center = true);

        for (a = [0 : 60 : 300]) {
            rotate([0, 0, a]) {
                translate([10, 0, 0]) {
                    cylinder(h = gear_height + 1, r = 2, center = true);
                }
            }
        }
    }
}

module tooth(angle = 0) {
    rotate([0, 0, angle]) {
        translate([gear_radius + tooth_l / 2 - 1, 0, 0]) {
            cube([tooth_l, tooth_w, tooth_h], center = true);
        }
    }
}

module simple_gear() {
    union() {
        gear_body();

        for (a = [0 : 360 / teeth : 360 - 360 / teeth]) {
            tooth(a);
        }
    }
}

simple_gear();
