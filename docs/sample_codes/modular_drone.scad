module motor_arm(x=10, y=10) {
  union() {
    translate([x,y,0])
      cylinder(h=1, r=2, center=true);
    translate([x/2,y/2,0])
      cube([12,1,1], center=true);
  }
}

module modular_drone() {
  union() {
    cube([5,5,1.5], center=true);
    motor_arm(10,10);
    motor_arm(-10,10);
    motor_arm(10,-10);
    motor_arm(-10,-10);
  }
}

modular_drone();
