// Broom with handle, grip, socket, head and bristles

handle_len = 170;
handle_r = 6;
grip_len = 30;
grip_r = 8;
hanger_r = 3;
hanger_offset = 12;
socket_h = 22;
socket_r = 11;
head_x = 110;
head_y = 24;
head_z = 14;
bristle_count = 13;
bristle_rows = 3;
bristle_r = 1.2;
bristle_len = 38;
bristle_x_gap = 8;
bristle_y_gap = 7;

module handle() {
    union() {
        translate([0, 0, head_z / 2 + socket_h + handle_len / 2]) {
            cylinder(h=handle_len, r=handle_r, center=true);
        }
        translate([0, 0, head_z / 2 + socket_h + handle_len - grip_len / 2]) {
            cylinder(h=grip_len, r=grip_r, center=true);
        }
    }
}

module hanging_hole() {
    translate([0, 0, head_z / 2 + socket_h + handle_len - hanger_offset]) {
        rotate([90, 0, 0]) {
            cylinder(h=grip_r * 4, r=hanger_r, center=true);
        }
    }
}

module socket() {
    translate([0, 0, head_z / 2 + socket_h / 2]) {
        cylinder(h=socket_h, r=socket_r, center=true);
    }
}

module broom_head() {
    cube([head_x, head_y, head_z], center=true);
}

module bristle() {
    cylinder(h=bristle_len, r=bristle_r, center=true);
}

module bristle_group() {
    for (x = [0 : bristle_count]) {
        for (y = [0 : bristle_rows]) {
            translate([x * bristle_x_gap - bristle_count * bristle_x_gap / 2, y * bristle_y_gap - bristle_rows * bristle_y_gap / 2, -head_z / 2 - bristle_len / 2]) {
                bristle();
            }
        }
    }
}

difference() {
    union() {
        broom_head();
        socket();
        handle();
        bristle_group();
    }
    hanging_hole();
}
