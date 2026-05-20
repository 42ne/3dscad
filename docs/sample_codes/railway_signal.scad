module signal_light(z=0) {
  translate([0,0,z])
    sphere(r=1.2);
}

module railway_signal() {
  union() {
    cylinder(h=16, r=0.5, center=true);
    translate([0,0,5])
      cube([4,1,8], center=true);
    signal_light(7);
    signal_light(5);
    signal_light(3);
    translate([0,0,-8])
      cube([5,5,0.7], center=true);
  }
}

railway_signal();
