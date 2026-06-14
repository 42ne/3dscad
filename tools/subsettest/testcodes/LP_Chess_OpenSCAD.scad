
size = 1;
offset = 2;
$fs=0.05;

// draw king

module king(size, x, y) {
   small=size/3;
   height=3*size;
   left=0;
   right=size-small; 
   lrm=(left+right)/2; 
   back=0; 
   front=size-small; 
   bfm=(back+front)/2;

   union() {translate([x*offset, y*offset, 0]) {
     cube(size=[size, size, height]);

	translate([left,back,height]) { cube(small); }
	translate([lrm,back,height]) { cube(small); }
   	translate([right,back,height]) { cube(small); }
	translate([left,front,height]) { cube(small); }
	translate([lrm,front,height]) { cube(small); }
	translate([right,front,height]) { cube(small); }
	translate([left,bfm,height]) { cube(small); }
	translate([right,bfm,height]) { cube(small); }
     }}
  }

// draw bishop

module bishop(size, x, y) {
   dsize=size*1.5;
   union() { translate([x*offset, y*offset, 0]) {
      cube([size,size,2*size]);
	 translate([size/2, size/2, 3*size]) {
	    sphere(r=size/6);
	    }
      translate([size/2,size/2,size*2]) {
	      rotate([90,45,45]) {
	         cube([dsize,dsize,size/10], center=true);
	      }
	      rotate([90,45,-45]) {
	         cube([dsize,dsize,size/10], center=true);
	      }
        }
     }}
   }

// draw a knight

module k_crown(small) {
   sphere(r=small/2);
   translate([-2*small, 0, -small]) { cube(small, center=true); }
   translate([-small, 0, -2*small]) { cube(small, center=true); }
   translate([2*small, 0, small]) { cube(small, center=true); }
   translate([small, 0, 2*small]) { cube(small, center=true); }
   translate([-2*small, 0, small]) { cube(small, center=true); }
   translate([-small, 0, 2*small]) { cube(small, center=true); }
   translate([2*small, 0, -small]) { cube(small, center=true); }
   translate([small, 0, -2*small]) { cube(small, center=true); }
   }

module knight (size, x, y) {
   height=2.5*size;
   small=size/3; // ratio of small cubes to body
   ho = small/2; // head larger than body
   hleft = 0-ho;
   hright = size + ho;
   hback = 0-ho;
   hfront = size + ho;
   htop = height + ho;
   hbbottom = height-size-ho;
   hfbottom = htop - small;

   union() {translate([x*offset, y*offset, 0]) {
      cube([size, size, height]);
      polyhedron(
        points = [[hleft,hback,hbbottom],[hright,hback,hbbottom],[hleft,hback,htop],[hright,hback,htop],[hleft,hfront,hfbottom],[hright,hfront,hfbottom],[hleft,hfront,htop],[hright,hfront,htop]], 
        triangles=[[0,2,1],[1,2,3], [0, 4, 2], [2, 4, 6], [1, 3, 5], [3, 7, 5], [4, 5, 6], [6, 5, 7], [2, 6, 3], [3, 6, 7], [0, 1, 5], [0, 5, 4]]);
        translate ([size/2, -small/2, height-size/2]) {
            k_crown(small);
        }
        translate([size/2, size/2, height+small/2]) {
            rotate([90,0,0]) { k_crown(small); }
        }
//      translate([0, -small, hbbottom]) { cube(small); }
//      translate([size-small, -small, hbbottom]) { cube(small); }
//      translate([0, -small, height]) { cube(small); }
//      translate([size-small, -small, height]) { cube(small); }
//      translate([-small, -small, height-small]) { cube(small); }
//      translate([-small, -small, hbbottom+small]) { cube(small); }
      }
   }}


// draw one pawn

module pawn (size, x, y) {
   small=size/3; // size of small cubes relative to body
   union() { translate([x*offset, y*offset, 0]) {
	cube(size);
	translate([0,2*small,size*5/6]) {
	   cube(small);
	   }
	translate([small,2*small,size]) {
	   cube(small);
	   }
	translate([small,size,size*5/6]) {
	   cube(small);
	   }
	translate([2*small,2*small,size*5/6]) {
	   cube(small);
	   }
	translate([size/2, size/2, size]) {
	   sphere(small/2, center=true);
	   }
    }}
}

module rook (size, x, y) {
   dsize=size*1.25;
   union() { translate([x*offset, y*offset, 0]) {
      cube([size, size, 2.5*size]);
      translate([size/2,size/2,size*2]) {
	      rotate([90,0,0]) {
	         cube([dsize,dsize,size/10], center=true);
	      }
	      rotate([90,0,90]) {
	         cube([dsize,dsize,size/10], center=true);
	      }
	   }
      }}
   }

module q_top (size, dsize) {
   //cylinder(h=size/10, r=dsize);
   cube([dsize,dsize,size/10], center=true);
   }

module queen (size, x, y) {
   dsize = size*1.5;
   height = 3*size;
   topcenter = height;
   union() { translate([x*offset, y*offset, 0]) {
      cube([size, size, 3*size]);
	 translate([size/2, size/2, 4*size]) {
	    sphere(r=size/6);
	    }
      translate([size/2,size/2,topcenter]) {
	      rotate([90,45,90]) {
	         q_top(size, dsize);
	      }
	      rotate([90,45,0]) {
	         q_top(size, dsize);
	      }
	      rotate([90,45,45]) {
	         q_top(size, dsize);

	      }
	      rotate([90,45,-45]) {
	         q_top(size, dsize);
	      }
	   }
      }
   }}

// uncomment one at a time to print each piece

//pawn(size,0,1);
//rook(size, 0, 0);
//bishop(size, 0, 0);
//knight(size, 0, 0);
//king(size, 0, 0);
//queen(size, 0, 0);

// draw 8 pawns, up one row

for ( count = [0:7]) {
   pawn(size, count, 1);
}

// draw home row pieces

rook(size, 0, 0);
bishop(size, 1, 0);
knight(size, 2, 0);
king(size, 3, 0);
queen(size, 4, 0);
knight(size,5, 0);
bishop(size, 6, 0);
rook(size, 7, 0);