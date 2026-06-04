// TEST 14: Nested module calls and complex expressions
// Tests: sin/cos in translate, nested calls, convexity= ignored.
// Expected: ring of 6 teeth, each a difference of cylinder minus slot.

$fn = 32;

teeth   = 6;
ring_r  = 25;
tooth_r = 4;
tooth_h = 8;
slot_w  = 2;

module tooth(r=4, h=8) {
    difference() {
        cylinder(h=h, r=r, center=true);
        cube([slot_w, r*2+1, h+1], center=true);
    }
}

for (i = [0:teeth-1]) {
    translate([cos(i*360/teeth)*ring_r,
               sin(i*360/teeth)*ring_r,
               0]) {
        tooth(r=tooth_r, h=tooth_h);
    }
}
