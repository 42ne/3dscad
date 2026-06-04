// TEST 11: Variables and expressions
// Expected: shapes whose sizes come from computed expressions.
// Check: changing a variable value in the tree updates all dependent shapes.
// Check: expressions like r*2, h+wall render correctly.

$fn = 24;

base   = 20;
height = 30;
wall   = 2;
ratio  = 0.6;

// cube sized by variables
translate([0, 0, 0]) {
    cube([base, base, height]);
}

// cylinder: radius derived from base, height from height
translate([base + 5, 0, 0]) {
    cylinder(h=height, r=base/2);
}

// nested expression: inner cylinder
translate([base*2 + 10, 0, 0]) {
    difference() {
        cylinder(h=height, r=base/2);
        cylinder(h=height + 2, r=base/2 - wall);
    }
}

// scaled by ratio
translate([base*3 + 15, 0, 0]) {
    scale([ratio, ratio, 1]) {
        cylinder(h=height, r=base/2);
    }
}
