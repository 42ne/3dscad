//August 2013
//printable cain

// set resolution
$fa=360/100;	//max 100 fragments
$fs=0.3;			// 0.3mm per fragment


//################# example 1 ###############################################

/*
//easy assembly / disassembly chain hinge
//but not so strong like snap_in or "strong chain" 
make_chain(
				c_pieces=6,
				c_parallel=1,
				c_width=8,
				c_length=21,
				c_height=7,
				c_thickness=1.5,
				c_link_space=0.4,
				c_link_deep=2,
				c_link_degree=35,
				c_link_diameter_inside=6.3,
				c_link_diameter_outside=6.3,
				c_flat_foot=true
				);
*/

//################# example 2 ###############################################

//easy assembly / disassembly chain
//with a snap in hinge
make_chain(
				c_pieces=6,
				c_width=8,
				c_length=21,
				c_height=7,
				c_thickness=1.5,
				c_link_space=0.4,
				c_link_deep=2,
				c_link_degree=35,
				c_link_diameter_inside=3.9,
				c_link_diameter_outside=3.3,
				c_snap_fit=true,
				c_flat_foot=true
				);



//################# example 3 ###############################################

/*
// a real strong chain
make_chain(
				c_pieces=6,
				c_width=8,
				c_length=21,
				c_height=7,
				c_thickness=1.5,
				c_link_space=0.4,
				c_link_deep=8,
				c_link_degree=90,
				c_link_diameter_inside=3.9,
				c_link_diameter_outside=3.3,	
				c_flat_foot=true
				);
*/

//################# example 4 ###############################################

/*
// easy assembly / disassembly chain hinge
//	this chain is bigger then the other examples
make_chain(
				c_pieces=6,
				c_width=10,
				c_length=27,
				c_height=10,
				c_thickness=1.5,
				c_link_space=0.4,
				c_link_deep=8,
				c_link_degree=90,
				c_link_diameter_inside=4,
				c_link_diameter_outside=3.2,	
				c_link_entry=3.1,
				c_flat_foot=true
				);
*/

//###########################################################################

/*
//all parameters
make_chain(
				c_pieces=6,
				c_parallel=1,
				c_width=8,
				c_length=21,
				c_height=7,
				c_thickness=1.5,
				c_link_space=0.4,
				c_link_deep=2,
				c_link_hole_deep=0,
				c_link_degree=35,
				c_link_diameter_inside=6.3,
				c_link_diameter_outside=6.3,
				c_link_entry=0,
				c_snap_fit=false,
				c_flat_foot=true
				);
*/


//################# example gear ############################################

/*
gear_gap=0.4;

translate([0,30,0])
	gear(
		teeth=9,
		teeth_extra_length=1,
		gear_thickness=width-thickness*2-gear_gap,
		chain_length=length,
		chain_height=height,
		hub_thickness=7,
		hub_diameter = 13,
		bore_diameter=4.5,
		hex_nut_width=7.7,			//Distance between flats
		hex_nut_height=4,
		rim_width=4,
		circles=7,
		clearance=0.2
		);

*/


//##### GEAR #####

module gear(
						teeth = 10,
						teeth_extra_length = 0,
						gear_thickness = 4.5,
						chain_length = 21,
						chain_height = 7,
						hub_diameter = 0,
						hub_thickness = 0,
						bore_diameter = 4.5,
						hex_nut_width = 0,		//Distance between flats
						hex_nut_height = 0,
						rim_width = 4,
						circles = 3,
						clearance = 0
){

	dist=(((chain_length-chain_height)/2/sin(360/teeth/2))-hub_diameter/2-rim_width*2-chain_height/2);
	echo(str("gear diameter : ",(chain_length-chain_height)/sin(360/teeth/2)," mm"));

	difference(){
		union(){

			//base
			cylinder(h=gear_thickness,r=(chain_length-chain_height)/2/sin(360/teeth/2),$fn=teeth);
			//hub
			cylinder(h=hub_thickness,r=hub_diameter/2);
			//teeth
			for(i=[0.5:1:teeth-0.5])
				rotate([0,0,(360/teeth)*i])
					translate([(chain_length-chain_height)/2/tan(360/teeth/2),0,gear_thickness/2])
							tooth(
									t_width = chain_length-(chain_height+clearance)*2,
									t_thickness = gear_thickness,
									t_length = chain_height/2+teeth_extra_length
									);
		}
		union(){
			for(i=[1:1:teeth])
				rotate([0,0,(360/teeth)*i])
					translate([(chain_length-chain_height)/2/sin(360/teeth/2),0,-1])
						cylinder(h=gear_thickness+2,r=chain_height/2+clearance);
		//bore
		translate([0,0,-1])
			cylinder(h=max(hub_thickness+2,gear_thickness+2),r=bore_diameter/2);
		//hex_nut
		translate([0,0,hub_thickness-hex_nut_height])
			hex(hex_nut_width,hex_nut_height+1);
		//circles
		for(i=[1:1:circles])
			rotate([0,0,360/circles*i])
				translate([0,dist/2+hub_diameter/2+rim_width,-1])
					cylinder(h=gear_thickness+2,r=dist/2);
		}
	}
}

//###########################################################################

module hex(hex_length,hex_h){
	cylinder(h=hex_h,r=hex_length/2/sin(60),$fn=6);
}


module tooth_simple(ts_width=1,ts_thickness=1,ts_length=1,ts_res=4){
res=(ts_width+ts_length)/2*ts_res;
scale([ts_width/(sqrt(0.6*0.6*2)+0.4),ts_thickness,ts_length/(sqrt(0.6*0.6*2)+0.4)*2])
rotate([90,0,0])
	difference(){
		minkowski(){
			translate([0,-sqrt(2)/2,0])
				rotate([0,0,45])
					translate([0.2,0.2,-0.5])
						cube(1-0.4);
				cylinder(r=0.2,h=0.4,$fn=res);
		}
		translate([-1,-2,-0.5])
			cube(2);
	}
}

module tooth(t_width=1,t_thickness=1,t_length=1,t_res=4){
res=(t_width+t_length)/2*max(t_res,$fn/10);
rotate([-90,0,-90])
scale([t_width,t_thickness,t_length])
	hull(){
		tooth_simple(1,0.2,1,res);
		tooth_simple(1,1,0.55,res);
	}
}

//##### END GEAR #####


//###########################################################################

module chain(
				width=8,
				length=21,
				height=7,
				thickness=1.5,
				link_space=0.4,
				link_deep=2,
				link_hole_deep,
				link_degree=35,
				link_diameter_inside,
				link_diameter_outside,
				link_entry=0,
				snap_fit=false,
				flat_foot=true
){

	//block
	render()
	difference(){
		union(){
			translate([0,height/2,-width]) {
				if (flat_foot){
					translate([-height/2,-height/2,0])
						cube([height,height/2,width]);
				}
				cylinder(r=height/2,h=width);
			}

			translate([-length+height/2,0,0]){
				//left side
				chain_side(
								s_length=length,
								s_height=height,
								s_thickness=thickness,
								s_link_space=link_space,
								s_link_deep=link_deep,
								s_link_degree=link_degree,
								s_link_diameter_outside=link_diameter_outside,
								s_snap_fit=snap_fit,
								s_flat_foot=flat_foot
								);

				//right side
				translate([0,0,-width])
					mirror([0,0,1])
						chain_side(
										s_length=length,
										s_height=height,
										s_thickness=thickness,
										s_link_space=link_space,
										s_link_deep=link_deep,
										s_link_degree=link_degree,
										s_link_diameter_outside=link_diameter_outside,
										s_snap_fit=snap_fit,
										s_flat_foot=flat_foot
										);
			}
		}
		union(){
			translate([0,height/2,-width])
				//left
				if (snap_fit){
					cylinder(h=max(link_deep,link_hole_deep), r=link_diameter_inside/2);
				}else{
					cylinder(h=max(link_deep,link_hole_deep),
								r1=link_diameter_inside/2,
								r2=max(link_diameter_inside/2-(max(link_deep,link_hole_deep)/tan(link_degree)),0));
				}
			translate([-link_entry/2,height/2,-width])
				//left link entry
				cube([link_entry,height/2,max(link_deep,link_hole_deep)]);

			translate([0,height/2,-max(link_deep,link_hole_deep)])
				//right
				if (snap_fit){
					cylinder(h=max(link_deep,link_hole_deep), r=link_diameter_inside/2);
				}else{
					cylinder(h=max(link_deep,link_hole_deep),
								r1=max(link_diameter_inside/2-(max(link_deep,link_hole_deep)/tan(link_degree)),0),
								r2=link_diameter_inside/2);
				}
			translate([-link_entry/2,height/2,-max(link_deep,link_hole_deep)])
				//right link entry
				cube([link_entry,height/2,max(link_deep,link_hole_deep)]);
		}
	}
}

//###########################################################################

module chain_side(
						s_length,
						s_height,
						s_thickness,
						s_link_space,
						s_link_deep,
						s_link_degree,
						s_link_diameter_outside,
						s_snap_fit,
						s_flat_foot
){

	translate([s_height/2,s_height/2,s_link_space]){
			cylinder(h=s_thickness,r=s_height/2);
			if (s_flat_foot){
				translate([-s_height/2,-s_height/2,0])
					cube([s_height,s_height/2,s_thickness]);
			}
	}
	translate([s_height/2,s_height/2,-s_link_deep+s_link_space])
		if (s_snap_fit){
			//snap fit
			difference(){
				cylinder(h=s_link_deep,r=s_link_diameter_outside/2);
				union(){
					translate([-s_link_diameter_outside/2,-s_link_diameter_outside/2,s_link_deep])
						rotate([0,180-s_link_degree,0])
							translate([0,0,-s_link_deep*4])
								cube([s_link_diameter_outside*3,s_link_diameter_outside,s_link_deep*10]);
					translate([-s_link_diameter_outside/2,-s_link_diameter_outside/2,s_link_deep])
						rotate([180+s_link_degree,0,0])
							translate([0,0,-s_link_deep*4])
								cube([s_link_diameter_outside,s_link_diameter_outside*3,s_link_deep*10]);
				}
			}
		}else{
			//normal
			cylinder(h=s_link_deep,
						r1=max(s_link_diameter_outside/2-(s_link_deep/tan(s_link_degree)),0),
						r2=s_link_diameter_outside/2);
		}

	translate([s_height/2,0,s_link_space])
		cube([sqrt((s_height/2)*(s_height/2)+(s_height/2)*(s_height/2))+s_link_space+s_thickness,
				s_height,
				s_thickness]);
	translate([sqrt((s_height/2)*(s_height/2)+(s_height/2)*(s_height/2))+s_thickness+s_link_space+s_height/2,
				0,
				-s_thickness])
		cube([s_length-s_height-sqrt((s_height/2)*(s_height/2)+
					(s_height/2)*(s_height/2))-s_thickness-s_link_space,
				s_height,
				s_thickness]);

	hull(){
		translate([sqrt((s_height/2)*(s_height/2)+(s_height/2)*(s_height/2))+s_link_space+s_height/2,
					0,
					s_link_space])
			cube([s_thickness,s_height,s_thickness]);
		translate([sqrt((s_height/2)*(s_height/2)+(s_height/2)*(s_height/2))+s_thickness+s_link_space+s_height/2,
					0,
					-s_thickness])
			cube([s_thickness,s_height,s_thickness]);
	}
}

//###########################################################################

module make_chain(
						c_pieces=6,
						c_parallel=1,
						c_width=8,
						c_length=21,
						c_height=7,
						c_thickness=1.5,
						c_link_space=0.4,
						c_link_deep=2,
						c_link_hole_deep,
						c_link_degree=35,
						c_link_diameter_inside=6,	//hole
						//do you need more space inside of the hinge? decrease c_link_diameter_outside
						c_link_diameter_outside=6,	
						c_link_entry,
						c_snap_fit=false,
						c_flat_foot=true
){

echo(str("chain length : ",(c_length-c_height)*c_pieces+c_height,," mm"));

rotate([90,0,0])
for(ii=[0:1:c_parallel-1])
	for(i=[0:1:c_pieces-1])
		translate([(c_length-c_height)*i,0,ii*(c_width+c_thickness+c_link_space*2)])
			chain(
					width=c_width,
					length=c_length,
					height=c_height,
					thickness=c_thickness,
					link_space=c_link_space,
					link_deep=c_link_deep,
					link_hole_deep=c_link_hole_deep,
					link_degree=c_link_degree,
					link_diameter_inside=c_link_diameter_inside,	//hole
					link_diameter_outside=c_link_diameter_outside,
					link_entry=c_link_entry,
					snap_fit=c_snap_fit,
					flat_foot=c_flat_foot
					);
}