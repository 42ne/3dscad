// TEST 01: Basic shapes — cube, sphere, cylinder, cone
// Expected tree: union of 6 shapes, all visible in viewport.
// Check: each shape has correct size label in the card.

union() {
    // cube: size vector, not centered
    translate([0, 0, 0]) {
        cube([10, 20, 5]);
    }

    // cube: scalar size, centered
    translate([20, 0, 0]) {
        cube(15, center=true);
    }

    // sphere: radius notation
    translate([40, 0, 0]) {
        sphere(r=8);
    }

    // sphere: diameter notation — card should show D=16
    translate([60, 0, 0]) {
        sphere(d=16);
    }

    // cylinder: r notation
    translate([80, 0, 0]) {
        cylinder(h=12, r=5);
    }

    // cylinder: d notation — card should show D=10
    translate([100, 0, 0]) {
        cylinder(h=12, d=10);
    }
}
