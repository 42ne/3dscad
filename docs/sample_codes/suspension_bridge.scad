module tower(x=0) {
  translate([x,0,3])
    cube([1.5,1.5,6], center=true);
}

module cable_post(x=0, z=7) {
  translate([x,0,z])
    sphere(r=0.45);
}

module suspension_bridge() {
  union() {
    cube([30,3,0.8], center=true);
    tower(-10);
    tower(10);
    cable_post(-10,7);
    cable_post(-5,6);
    cable_post(0,5.5);
    cable_post(5,6);
    cable_post(10,7);
  }
}

suspension_bridge();
