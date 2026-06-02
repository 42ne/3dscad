box_x = 120;
box_y = 80;
box_z = 50;

frame_w = 10;
thickness = 3;

bottom_angle = 45;
sides_angle = 45;

pad_height = 0.5;
pad_radius = 10;

struts_number_x = 7;
struts_number_y = 7;
struts_number_z = 7;
struts_width = 3;

bo_thickness = thickness;
bo_frame = frame_w;
bo_1_struts_number = struts_number_x;
bo_1_struts_angle = bottom_angle;
bo_2_struts_number = struts_number_y;
bo_2_struts_angle = bottom_angle + 90;

le_thickness = thickness;
le_frame = frame_w;
le_1_struts_number = struts_number_z;
le_1_struts_angle = sides_angle;
le_2_struts_number = struts_number_y;
le_2_struts_angle = sides_angle + 90;

ri_thickness = thickness;
ri_frame = frame_w;
ri_1_struts_number = struts_number_z;
ri_1_struts_angle = sides_angle;
ri_2_struts_number = struts_number_y;
ri_2_struts_angle = sides_angle + 90;

fr_thickness = thickness;
fr_frame = frame_w;
fr_1_struts_number = struts_number_x;
fr_1_struts_angle = sides_angle;
fr_2_struts_number = struts_number_z;
fr_2_struts_angle = sides_angle + 90;

ba_thickness = thickness;
ba_frame = frame_w;
ba_1_struts_number = struts_number_x;
ba_1_struts_angle = sides_angle;
ba_2_struts_number = struts_number_z;
ba_2_struts_angle = sides_angle + 90;

module corner_pad(x = 0, y = 0, h = pad_height, r = pad_radius) {
    translate([x, y, h / 2]) {
        cylinder(h = h, r = r, center = true);
    }
}

module panel_pads(w = 100, d = 60, h = pad_height, r = pad_radius) {
    if (h > 0) {
        union() {
            corner_pad(x = -w / 2, y = -d / 2, h = h, r = r);
            corner_pad(x =  w / 2, y = -d / 2, h = h, r = r);
            corner_pad(x =  w / 2, y =  d / 2, h = h, r = r);
            corner_pad(x = -w / 2, y =  d / 2, h = h, r = r);
        }
    }
}

module frame_panel(w = 100, d = 60, t = 3, fw = 8) {
    union() {
        translate([0, -d / 2 + fw / 2, 0]) { cube([w, fw, t], center = true); }
        translate([0, d / 2 - fw / 2, 0]) { cube([w, fw, t], center = true); }
        translate([-w / 2 + fw / 2, 0, 0]) { cube([fw, d, t], center = true); }
        translate([w / 2 - fw / 2, 0, 0]) { cube([fw, d, t], center = true); }
    }
}

module diagonal_struts(w = 80, d = 40, t = 3, count = 5, sw = 3, angle = 45) {
    inner_diag = sqrt(w * w + d * d) + 20;
    if (count > 0) {
        intersection() {
            cube([w, d, t], center = true);
            union() {
                for (i = [1 : count]) {
                    translate([-w / 2 + i * w / (count + 1), 0, 0]) {
                        rotate([0, 0, angle]) { cube([sw, inner_diag, t], center = true); }
                    }
                }
            }
        }
    }
}

module panel_xy(w = 100, d = 60, t = 3, fw = 8, count_a = 5, angle_a = 45, count_b = 5, angle_b = 135, use_pads = 0) {
    inner_w = w - 2 * fw;
    inner_d = d - 2 * fw;
    union() {
        frame_panel(w = w, d = d, t = t, fw = fw);
        translate([0, 0, 0]) { diagonal_struts(w = inner_w, d = inner_d, t = t, count = count_a, sw = struts_width, angle = angle_a); }
        translate([0, 0, 0]) { diagonal_struts(w = inner_w, d = inner_d, t = t, count = count_b, sw = struts_width, angle = angle_b); }
        if (use_pads > 0) {
            panel_pads(w = w, d = d, h = pad_height, r = pad_radius);
        }
    }
}

module bottom_side() {
    translate([0, 0, bo_thickness / 2]) {
        panel_xy(w = box_x, d = box_y, t = bo_thickness, fw = bo_frame, count_a = bo_1_struts_number, angle_a = bo_1_struts_angle, count_b = bo_2_struts_number, angle_b = bo_2_struts_angle, use_pads = 1);
    }
}

module front_side() {
    translate([0, -box_y / 2 + fr_thickness / 2, box_z / 2]) {
        rotate([90, 0, 0]) {
            panel_xy(w = box_x, d = box_z, t = fr_thickness, fw = fr_frame, count_a = fr_1_struts_number, angle_a = fr_1_struts_angle, count_b = fr_2_struts_number, angle_b = fr_2_struts_angle, use_pads = 0);
        }
    }
}

module back_side() {
    translate([0, box_y / 2 - ba_thickness / 2, box_z / 2]) {
        rotate([90, 0, 0]) {
            panel_xy(w = box_x, d = box_z, t = ba_thickness, fw = ba_frame, count_a = ba_1_struts_number, angle_a = ba_1_struts_angle, count_b = ba_2_struts_number, angle_b = ba_2_struts_angle, use_pads = 0);
        }
    }
}

module right_side() {
    translate([box_x / 2 - ri_thickness / 2, 0, box_z / 2]) {
        rotate([0, -90, 0]) {
            panel_xy(w = box_z, d = box_y, t = ri_thickness, fw = ri_frame, count_a = ri_1_struts_number, angle_a = ri_1_struts_angle, count_b = ri_2_struts_number, angle_b = ri_2_struts_angle, use_pads = 0);
        }
    }
}

module left_side() {
    translate([-box_x / 2 + le_thickness / 2, 0, box_z / 2]) {
        rotate([0, -90, 0]) {
            panel_xy(w = box_z, d = box_y, t = le_thickness, fw = le_frame, count_a = le_1_struts_number, angle_a = le_1_struts_angle, count_b = le_2_struts_number, angle_b = le_2_struts_angle, use_pads = 0);
        }
    }
}

module lattice_box() {
    union() {
        bottom_side();
        front_side();
        back_side();
        right_side();
        left_side();
    }
}

lattice_box();