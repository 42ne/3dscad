module blade(angle=0) {
  rotate([0,0,angle])
    translate([3,0,0])
      cube([5,0.6,0.4], center=true);
}

module windmill_pump() {
  union() {
    cylinder(h=12, r=0.5, center=true);
    translate([0,0,6])
      sphere(r=1);
    translate([0,0,6])
      blade(0);
    translate([0,0,6])
      blade(90);
    translate([0,0,6])
      blade(180);
    translate([0,0,6])
      blade(270);
    translate([0,0,-6])
      cube([4,4,0.7], center=true);
  }
}

windmill_pump();
