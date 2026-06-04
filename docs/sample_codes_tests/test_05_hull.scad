// TEST 05: hull()
// Expected: hull of 4 spheres = rounded tetrahedron-ish shape.
// Check: CSG mesh is non-empty and looks convex.
// Check: editing a translate inside hull updates mesh in real time.

$fn = 20;

hull() {
    translate([0,  0,  0 ]) { sphere(r=3); }
    translate([20, 0,  0 ]) { sphere(r=3); }
    translate([10, 15, 0 ]) { sphere(r=3); }
    translate([10, 7,  12]) { sphere(r=3); }
}
