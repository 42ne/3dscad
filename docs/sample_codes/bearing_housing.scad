// Pillow block bearing housing with mounting holes

base_x = 96;
base_y = 36;
base_z = 10;
body_r = 24;
body_w = 28;
bearing_r = 14;
shaft_r = 9;
mount_r = 4.5;
mount_x = 34;

module mount_hole(r = 4.5) {
    cylinder(h=30, r=r, center=true);
}

module shaft_hole(r = 9) {
    rotate([90, 0, 0]) {
        cylinder(h=80, r=r, center=true);
    }
}

module bearing_socket(r = 14) {
    rotate([90, 0, 0]) {
        cylinder(h=body_w + 4, r=r, center=true);
    }
}

difference() {
    union() {
        cube([base_x, base_y, base_z], center=true);
        translate([0, 0, body_r]) {
            rotate([90, 0, 0]) {
                cylinder(h=body_w, r=body_r, center=true);
            }
        }
        translate([0, 0, body_r / 2]) {
            cube([body_r * 2, body_w, body_r], center=true);
        }
    }
    translate([0, 0, body_r]) {
        bearing_socket(r=bearing_r);
    }
    translate([0, 0, body_r]) {
        shaft_hole(r=shaft_r);
    }
    translate([-mount_x, 0, 0]) {
        mount_hole(r=mount_r);
    }
    translate([mount_x, 0, 0]) {
        mount_hole(r=mount_r);
    }
}
