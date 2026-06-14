// text() demo — 2D glyphs, extruded, and offset.
linear_extrude(height=3) {
    text("Hello", size=12);
}

translate([0, 20, 0]) {
    linear_extrude(height=2) {
        text("Ag", size=20, halign="center", valign="center");
    }
}

translate([0, -20, 0]) {
    linear_extrude(height=2) {
        offset(r=1) {
            text("O", size=18);
        }
    }
}
