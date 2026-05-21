// Daisy flower with petals and stem

petal_count = 12;
petal_len = 26;
petal_w = 9;
petal_h = 4;
center_r = 9;
stem_h = 45;

union() {
    translate([0, 0, -stem_h / 2]) {
        cylinder(h=stem_h, r=2, center=true);
    }

    for (i = [0 : petal_count - 1]) {
        rotate([0, 0, i * 30]) {
            translate([petal_len / 2, 0, 0]) {
                scale([1.7, 0.7, 0.25]) {
                    sphere(r=petal_w);
                }
            }
        }
    }

    sphere(r=center_r);
}
