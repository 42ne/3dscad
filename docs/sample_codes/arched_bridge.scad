// 3DScad sample: arched bridge
// Uses only the currently round-trippable subset.

module scene_model() {
    deck_length = 180;
    deck_width = 34;
    deck_height = 10;

    difference() {
        union() {
            translate([0, 0, 30]) {
                cube([deck_length, deck_width, deck_height], center=true);
            }

            for (i = [-4 : 1 : 4]) {
                translate([i*20, -13, 15]) {
                    cylinder(h=28, r=5, center=true);
                }
                translate([i*20, 13, 15]) {
                    cylinder(h=28, r=5, center=true);
                }
            }

            for (i = [-3 : 1 : 3]) {
                translate([i*24, 0, 3]) {
                    cube([12, deck_width, 16], center=true);
                }
            }

            translate([-80, 0, 0]) {
                cube([10, deck_width + 10, 28], center=true);
            }
            translate([80, 0, 0]) {
                cube([10, deck_width + 10, 28], center=true);
            }
        }

        for (i = [-2 : 1 : 2]) {
            translate([i*28, 0, 2]) {
                scale([1.6, 1, 0.75]) {
                    cylinder(h=deck_width + 18, r=14, center=true);
                }
            }
        }
    }
}

scene_model();
