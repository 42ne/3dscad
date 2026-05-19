// 3DScad sample: parametric bookshelf
// Uses only the currently round-trippable subset.

module scene_model() {
    shelf_w = 120;
    shelf_d = 32;
    shelf_h = 160;
    thickness = 6;
    num_shelves = 5;

    union() {
        translate([-shelf_w / 2 + thickness / 2, 0, 0]) {
            cube([thickness, shelf_d, shelf_h], center=true);
        }
        translate([shelf_w / 2 - thickness / 2, 0, 0]) {
            cube([thickness, shelf_d, shelf_h], center=true);
        }
        translate([0, shelf_d / 2 - thickness / 2, 0]) {
            cube([shelf_w - thickness * 2, thickness, shelf_h], center=true);
        }
        for (i = [0 : 1 : num_shelves - 1]) {
            translate([0, 0, -shelf_h / 2 + thickness / 2 + i * (shelf_h - thickness) / (num_shelves - 1)]) {
                cube([shelf_w - thickness * 2, shelf_d - thickness, thickness], center=true);
            }
        }
    }
}

scene_model();
