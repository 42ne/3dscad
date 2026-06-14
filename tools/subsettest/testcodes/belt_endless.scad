//
// B E L T
//
// By Sylvain Rochette (sylvain.rochette@gmail.com)
// Modified by Jon Tindall (renderranch@yahoo.com)to generate endless gt2 belts
//
// Helper to generate a belt, actually you can generate 
// all types of belt (rounded type) if you know the basic values needed...
//
// Supported Modules:
//
// belt(width, length, pitch, height, teeth_height, radius)
// gt2_belt(width, length, thickness)



module teeth(width, pitch, radius, inner_radius)
{
    circle(r = radius, $fn = 32);
    
    for (x = [-1, 1])
    {
        translate([x * (radius + inner_radius), 0])
        intersection()
        {
            translate([x * -inner_radius, -inner_radius])
            square([inner_radius * 2, inner_radius * 2], center = true);
            
            difference()
            {
                circle(r = inner_radius + 1, $fn = 32);

                translate([0, 0])
                circle(r = inner_radius, $fn = 32);
            }
        }
    }
}

module belt(width, length, thickness, pitch, height, teeth_height, radius)
{
    teeth_count = ceil(length / pitch);
    echo("teeth = ",teeth_count);
    dia = (teeth_count*pitch)/3.14;
    rad = dia*.5;
    echo("diameter = ", dia);
    pitchreduction = pitch * .15;
    difference(){
    cylinder(width, rad+thickness+pitchreduction, rad+thickness+pitchreduction,  true, $fn = teeth_count);
    cylinder(width, rad+pitchreduction, rad+pitchreduction,  true, $fn = teeth_count);
    
    }
    
    linear_extrude(height = width, center = true, convexity = 10)
    {


//        // Teeths
        for (i = [0:teeth_count])
        {
           rotate(i*(360/(teeth_count)),[0, 0,1])  
           translate([dia/2,0 ,0])
            rotate(90,[0,0,1])    
            teeth(
                width = width, 
                pitch = pitch, 
                radius = radius, 
                inner_radius = radius * 0.4,
                teeth_height = teeth_height);
            
        }
    }
}


module gt2_belt(width, length, thickness)
{
    echo("length = ", length);
    belt(
        width = width, 
        length = length, 
        thickness = thickness, 
        pitch = 2, 
        height = 1.52, 
        teeth_height = 0.76,
        radius = 0.555);
}



gt2_belt(6, 100, 1);

