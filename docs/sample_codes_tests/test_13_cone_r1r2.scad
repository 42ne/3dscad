// TEST 13: Cone (cylinder with r1/r2 or d1/d2)
// Expected: 4 cone variants side by side.
// Check: r1/r2 shows R1/R2 labels in card; d1/d2 shows D1/D2.
// Check: roundtrip d1= back to code outputs d1= not r1=.

$fn = 32;

// r1/r2 notation
translate([0, 0, 0]) {
    cylinder(h=15, r1=8, r2=3);
}

// d1/d2 notation — card should show D1=16, D2=6
translate([25, 0, 0]) {
    cylinder(h=15, d1=16, d2=6);
}

// r1=0 = pointed cone
translate([50, 0, 0]) {
    cylinder(h=15, r1=8, r2=0);
}

// r2=0, flipped (bigger top)
translate([75, 0, 0]) {
    cylinder(h=15, r1=0, r2=8);
}
