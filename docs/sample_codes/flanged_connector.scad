// Flanged pipe connector with counterbored bolt holes

flange_outer_r = 60;
flange_h = 12;
pipe_outer_r = 25;
pipe_inner_r = 18;
pipe_h = 38;
neck_r = 32;
neck_h = 16;
bolt_circle_r = 46;
bolt_hole_r = 4.5;
bolt_counter_r = 8;
bolt_counter_h = 4;

module through_hole(r = 4.5) {
    cylinder(h=80, r=r, center=true);
}

module counterbore(r = 8) {
    translate([0, 0, flange_h / 2 - bolt_counter_h / 2]) {
        cylinder(h=bolt_counter_h + 1, r=r, center=true);
    }
}

difference() {
    union() {
        cylinder(h=flange_h, r=flange_outer_r, center=true);
        translate([0, 0, flange_h / 2 + neck_h / 2]) {
            cylinder(h=neck_h, r=neck_r, center=true);
        }
        translate([0, 0, flange_h / 2 + pipe_h / 2]) {
            cylinder(h=pipe_h, r=pipe_outer_r, center=true);
        }
    }
    cylinder(h=120, r=pipe_inner_r, center=true);
    for (i = [0 : 7]) {
        rotate([0, 0, i * 45]) {
            translate([bolt_circle_r, 0, 0]) {
                through_hole(r=bolt_hole_r);
            }
        }
    }
    for (i = [0 : 7]) {
        rotate([0, 0, i * 45]) {
            translate([bolt_circle_r, 0, 0]) {
                counterbore(r=bolt_counter_r);
            }
        }
    }
}
