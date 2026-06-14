// offset() demo — rounded grow, straight delta, chamfered, and shrink.
linear_extrude(height=4) {
    offset(r=3) {
        square([20, 10], center=true);
    }
}

translate([40, 0, 0]) {
    offset(delta=3, chamfer=true) {
        square([20, 10], center=true);
    }
}

translate([0, 30, 0]) {
    offset(r=-2) {
        square([20, 10], center=true);
    }
}
