module lighthouse_beacon() {
  union() {
    cylinder(h=8, r=3, center=true);
    translate([0,0,5])
      cylinder(h=2, r=2, center=true);
    translate([0,0,6.4])
      sphere(r=1.2);
    translate([0,0,7.4])
      cylinder(h=0.5, r=2.4, center=true);
  }
}

lighthouse_beacon();
