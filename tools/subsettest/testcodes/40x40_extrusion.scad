// T-Slot Extrusion

// Object Type
object_type = "extrusion"; // ["extrusion", "cap"]

// Extrusion Length
length = 1;

// Filling type -  E = Ultralight, L = Light, S = Heavy
fill_type = "E"; // ["E","L","S"]

// Slot Type
slot_type = "I"; // ["B","I"]

// The corner radius in mm
corner_radius = 3;

// Nut Size - The size of screw hole at the ends
nut_size = 7.5;

// Slot Opening 
slot_opening = 8;

// End Threads - Not Implemented
thread = false;

// Flat sides options
flat_sides = "None"; // ["None","A","AB","ABC","ABCD", "AC"] 

// Thickness of the profile face , This will control how 'deep' the slot sits inside the profile
profile_face_thickness = 3;

profile_x = 40;
profile_y = 40;
$fn=128;

module slot() {
    outer_radius = corner_radius;
    r = outer_radius/3;
   
    slot_width = 20;
    slot_depth = 10;

    wing_length = (slot_width - slot_opening) / 2;
    
    difference() {
        translate([-slot_opening/2, -profile_face_thickness]){
            square([slot_opening,profile_face_thickness]);
            hull() {
                translate([-wing_length + r, -r])circle(r);
                translate([slot_opening + wing_length -r, -r])circle(r);
                translate([slot_opening + wing_length -r, -4])circle(r);
                translate([slot_opening - 0.5, -slot_depth+r])circle(r);
                translate([0.5, -slot_depth+r])circle(r);
                translate([-5, -slot_depth+r +5])circle(r);            
            }
       }
       
       if (slot_type == "I") {
           translate([4,-5])square([2.5,2]);
           translate([-6.5,-5])square([2.5,2]);
       }
   }
}

module ultra_light_hole() {
   
    light_hole();
    
    a = 0.7;
    hull() {
        rotate([0,0,45])translate([17,-a])circle(0.5);
        rotate([0,0,45])translate([17,a])circle(0.5);
        rotate([0,0,45])translate([22,-a])circle(0.5);
        rotate([0,0,45])translate([22,a])circle(0.5);
    }
    
    rotate([0,0,45])minkowski() {
        polygon([[15.3,a], [15.3,-a],[10,-0.7], [6.2,-1.0], [6.2,1.0], [10,0.7] ]);
        circle(r=0.3);
    }
         
    // rounded 1/4 arc
    minkowski(){
        difference(){
            difference(){
                circle(r=6);
                circle(r=5.7);
            }
            translate([-8,0])square([9,9]);
            translate([0,-8])square([9,9]);
            translate([-10,-10])square([10,10]);
        }
        circle(r=0.5);
    }
}

module light_hole() {
    outer_radius = corner_radius;
    r = outer_radius;
   
    translate([profile_x/2-profile_face_thickness-1,profile_y/2-profile_face_thickness-1]) {
        hull() {
            polygon(points=[[3,-4],[-4,-4],[-4,3]]);
            translate([3-r,3-r])circle(r);
        }
    }
}

module smooth_face() {
    translate([profile_x/2-profile_face_thickness,-slot_opening/2])square([profile_face_thickness,slot_opening+2]);
}

module profile() {
    r = corner_radius;
    x_max = profile_x/2 -r;
    y_max = -profile_y/2 +r;
    difference() {
        
        // Basic Square Frame
        hull() {
            translate([x_max, y_max])circle(r);
            translate([x_max, -y_max])circle(r); 
            translate([-x_max, y_max])circle(r); 
            translate([-x_max, -y_max])circle(r); 
        }

        // Add Slots
        for (side_index = [0 : 3] ){
            rotate([0,0,90 * side_index])translate([0,profile_y/2])slot();
        }

       // Center Hole
        circle(r=nut_size/2);

        if (fill_type == "L") {
            // add holes to make light "L"
            rotate([0,0,0])light_hole();
            rotate([0,0,90])light_hole();
            rotate([0,0,180])light_hole();
            rotate([0,0,270])light_hole();
        }
        if (fill_type == "E") {
            // add holes to make ultra light "E"
            rotate([0,0,0])ultra_light_hole();
            rotate([0,0,90])ultra_light_hole();
            rotate([0,0,180])ultra_light_hole();
            rotate([0,0,270])ultra_light_hole();
        }
    }
    
    // Flat sides
    if (len(search("A", flat_sides))) {
        rotate([0,0,0])smooth_face();
    }
    if (len(search("B", flat_sides))) {
        rotate([0,0,90])smooth_face();
    }
    if (len(search("C", flat_sides))) {
        rotate([0,0,180])smooth_face();
    }
    if (len(search("D", flat_sides))) {
        rotate([0,0,270])smooth_face();
    }
    
}

module cap_slot_leg() {
    sr = 0.90; // Scale Ratio
    translate([0,20,0])scale(sr)difference(){
            slot(); 
            translate([-slot_opening/2,-profile_face_thickness,0])square([slot_opening,profile_face_thickness]);
        }
}
module cap() {
    cap_height = 3;
    r = corner_radius;
   
    smooth = 2;
    x_max = profile_x/2 -r -smooth;
    y_max = -profile_y/2 +r +smooth;
    // Basic Square Frame
    
    difference() {
        minkowski(){
            linear_extrude(height=cap_height-smooth)hull() {
                translate([x_max, y_max])circle(r);
                translate([x_max, -y_max])circle(r); 
                translate([-x_max, y_max])circle(r); 
                translate([-x_max, -y_max])circle(r); 
            }
            sphere(smooth);
        }
        
    translate([0,0,-smooth])linear_extrude(height=smooth)hull() {
            translate([x_max*2, y_max*2])circle(r);
            translate([x_max*2, -y_max*2])circle(r); 
            translate([-x_max*2, y_max*2])circle(r); 
            translate([-x_max*2, -y_max*2])circle(r); 
        }
    
    
    thickness = 1;
    x = x_max + thickness;
    y = y_max + thickness;
    translate([0,0,-0])linear_extrude(height=cap_height-thickness)hull() {
        translate([x, y])circle(r);
        translate([x, -y])circle(r); 
        translate([-x, y])circle(r); 
        translate([-x, -y])circle(r); 
        }
    }
    
    // Center Hole
    cetner_pin_height = 4;
    translate([0,0,-cetner_pin_height])linear_extrude(cetner_pin_height+cap_height)circle(r=nut_size/2 - 1);
    
    slot_legs_height = 2;
    translate([0,0,-slot_legs_height])linear_extrude(slot_legs_height+cap_height)  {
        rotate([0,0,0])cap_slot_leg();
        rotate([0,0,90])cap_slot_leg();
        rotate([0,0,180])cap_slot_leg();
        rotate([0,0,270])cap_slot_leg();
    }
}

if (object_type == "extrusion") {
    linear_extrude(height=length)profile();
}

if (object_type == "cap") {
    cap();
}
