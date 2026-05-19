// 3DScad sample: LEGO-compatible brick (4x2 studs)
// Uses only the currently round-trippable subset.

module scene_model() {
    nx = 4;
    ny = 2;
    pitch = 8;
    wall = 1.6;
    plate_h = 9.6;
    stud_r = 2.4;
    stud_h = 1.8;

    union() {
        difference() {
            cube([nx * pitch, ny * pitch, plate_h], center=true);
            translate([0, 0, wall]) {
                cube([nx * pitch - wall * 2, ny * pitch - wall * 2, plate_h], center=true);
            }
        }
        for (ix = [0 : 1 : nx - 1]) {
            for (iy = [0 : 1 : ny - 1]) {
                translate([-(nx - 1) * pitch / 2 + ix * pitch, -(ny - 1) * pitch / 2 + iy * pitch, plate_h / 2 + stud_h / 2]) {
                    cylinder(h=stud_h, r=stud_r, center=true);
                }
            }
        }
    }
}

scene_model();
