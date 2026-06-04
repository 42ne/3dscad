// TEST 04: Transforms — translate, rotate, scale, mirror
// Expected tree: each transform wraps a cube child.
// Check: editing a translate value in the card updates the code vector.
// Check: editing a rotate angle updates the code.

cube_s = 10;

// translate: cube moved to [20, 5, 3]
translate([20, 5, 3]) {
    cube([cube_s, cube_s, cube_s]);
}

// rotate: 45 degrees around Z
translate([40, 0, 0]) {
    rotate([0, 0, 45]) {
        cube([cube_s, cube_s, cube_s]);
    }
}

// scale: non-uniform stretch
translate([60, 0, 0]) {
    scale([2, 1, 0.5]) {
        cube([cube_s, cube_s, cube_s]);
    }
}

// mirror: flip across XZ plane (Y axis)
translate([80, 0, 0]) {
    mirror([0, 1, 0]) {
        cube([cube_s, cube_s, cube_s]);
    }
}
