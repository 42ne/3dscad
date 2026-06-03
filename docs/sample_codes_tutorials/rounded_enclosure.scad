// 3DScad tutorial example: rounded enclosure
// Technique from MCAD/boxes.scad by Elmo Mäntynen
// https://github.com/openscad/MCAD  (LGPL 2.1)
// Uses only the currently round-trippable subset.

$fn = 32;

box_x  = 70;
box_y  = 50;
box_z  = 28;
corner = 5;
wall   = 3;
hole_r = 3;

module scene_model() {
    hole_h   = wall + 2;
    hole_pad = corner * 2;
    inner_x  = box_x - wall * 2;
    inner_y  = box_y - wall * 2;

    difference() {
        // Outer shell: hull of 8 corner spheres gives fully rounded edges.
        hull() {
            translate([corner,         corner,         corner        ]) {
                sphere(r=corner);
            }
            translate([box_x - corner, corner,         corner        ]) {
                sphere(r=corner);
            }
            translate([corner,         box_y - corner, corner        ]) {
                sphere(r=corner);
            }
            translate([box_x - corner, box_y - corner, corner        ]) {
                sphere(r=corner);
            }
            translate([corner,         corner,         box_z - corner]) {
                sphere(r=corner);
            }
            translate([box_x - corner, corner,         box_z - corner]) {
                sphere(r=corner);
            }
            translate([corner,         box_y - corner, box_z - corner]) {
                sphere(r=corner);
            }
            translate([box_x - corner, box_y - corner, box_z - corner]) {
                sphere(r=corner);
            }
        }

        // Hollow interior (same hull, shifted inward; open top).
        translate([wall, wall, wall]) {
            hull() {
                translate([corner,           corner,           corner        ]) {
                    sphere(r=corner);
                }
                translate([inner_x - corner, corner,           corner        ]) {
                    sphere(r=corner);
                }
                translate([corner,           inner_y - corner, corner        ]) {
                    sphere(r=corner);
                }
                translate([inner_x - corner, inner_y - corner, corner        ]) {
                    sphere(r=corner);
                }
                translate([corner,           corner,           box_z - corner]) {
                    sphere(r=corner);
                }
                translate([inner_x - corner, corner,           box_z - corner]) {
                    sphere(r=corner);
                }
                translate([corner,           inner_y - corner, box_z - corner]) {
                    sphere(r=corner);
                }
                translate([inner_x - corner, inner_y - corner, box_z - corner]) {
                    sphere(r=corner);
                }
            }
        }

        // Four corner mounting holes punched from below.
        translate([hole_pad,         hole_pad,         -1]) {
            cylinder(h=hole_h, r=hole_r, center=false);
        }
        translate([box_x - hole_pad, hole_pad,         -1]) {
            cylinder(h=hole_h, r=hole_r, center=false);
        }
        translate([hole_pad,         box_y - hole_pad, -1]) {
            cylinder(h=hole_h, r=hole_r, center=false);
        }
        translate([box_x - hole_pad, box_y - hole_pad, -1]) {
            cylinder(h=hole_h, r=hole_r, center=false);
        }
    }
}

scene_model();
