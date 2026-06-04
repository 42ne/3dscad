// TEST 09: for loops — range, step, list
// Expected tree: for node with children placed in a pattern.
// Check: viewport shows an evenly spaced row/grid of shapes.

$fn = 16;

// range loop: 5 spheres along X
for (i = [0:4]) {
    translate([i * 12, 0, 0]) {
        sphere(r=4);
    }
}

// range with step: 4 cylinders at Y=25 with step 2
for (i = [0:2:6]) {
    translate([i * 6, 25, 0]) {
        cylinder(h=8, r=3);
    }
}

// list loop: cubes at specific angles around a circle
for (a = [0, 60, 120, 180, 240, 300]) {
    translate([cos(a)*20, sin(a)*20 + 50, 0]) {
        cube([5, 5, 5], center=true);
    }
}
