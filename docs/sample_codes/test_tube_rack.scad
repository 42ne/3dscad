// 3DScad sample: laboratory test tube rack (5 x 3 wells)
// Uses only the currently round-trippable subset.

module scene_model() {
    cols = 5;
    rows = 3;
    tube_pitch = 22;
    tube_r = 7.5;
    block_h = 58;
    top_h = 14;

    difference() {
        union() {
            cube([cols * tube_pitch, rows * tube_pitch, block_h], center=true);
            translate([0, 0, block_h / 2 + top_h / 2]) {
                cube([cols * tube_pitch, rows * tube_pitch, top_h], center=true);
            }
        }
        for (ix = [0 : 1 : cols - 1]) {
            for (iy = [0 : 1 : rows - 1]) {
                translate([-(cols - 1) * tube_pitch / 2 + ix * tube_pitch, -(rows - 1) * tube_pitch / 2 + iy * tube_pitch, block_h / 4]) {
                    cylinder(h=block_h + top_h + 2, r=tube_r, center=true);
                }
            }
        }
    }
}

scene_model();
