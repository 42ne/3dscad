// Snap-in round hole plug, nominal hole diameter 40 mm

hole_r = 20;
top_r = 24;
lip_r = 21.2;
top_h = 3;
stem_h = 10;
lip_h = 2.5;
slot_w = 3;
slot_d = 16;

module spring_slot() {
    cube([slot_w, slot_d, stem_h * 3], center=true);
}

difference() {
    union() {
        translate([0, 0, stem_h / 2]) {
            cylinder(h=stem_h, r=hole_r - 0.4, center=true);
        }
        translate([0, 0, stem_h + lip_h / 2]) {
            cylinder(h=lip_h, r=lip_r, center=true);
        }
        translate([0, 0, stem_h + lip_h + top_h / 2]) {
            cylinder(h=top_h, r=top_r, center=true);
        }
    }

    for (i = [0 : 3]) {
        rotate([0, 0, i * 90]) {
            translate([hole_r - 5, 0, stem_h / 2]) {
                spring_slot();
            }
        }
    }

    translate([0, 0, stem_h + lip_h + top_h / 2]) {
        cylinder(h=top_h * 3, r=5, center=true);
    }
}
