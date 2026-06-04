// TEST 02: center=true vs center=false
// Expected: left column = default (false), right column = centered.
// Check: the "c" checkbox reflects each shape's center flag.
// Check: the mesh origin matches — centered shape straddles Z=0.

$fn = 16;

// --- center=false (default) ---
translate([0, 0, 0]) {
    cube([10, 10, 10]);           // bottom face at Z=0
}
translate([15, 0, 0]) {
    cylinder(h=10, r=4);          // bottom at Z=0
}
translate([30, 0, 0]) {
    sphere(r=5);                  // center=false has no effect on sphere; still centered
}

// --- center=true ---
translate([0, 20, 0]) {
    cube([10, 10, 10], center=true);   // straddles Z=0
}
translate([15, 20, 0]) {
    cylinder(h=10, r=4, center=true);  // straddles Z=0
}
translate([30, 20, 0]) {
    sphere(r=5);                       // same as above; sphere always centered
}
