// TEST 08: polygon() — flat and extruded, with paths=
// Expected: pentagon shape, L-shape via points+paths.
// Check: polygon points render as flat filled mesh.
// Check: extruded polygon is a 3D solid.

// simple pentagon (no paths)
translate([0, 0, 0]) {
    linear_extrude(height=5) {
        polygon(points=[
            [0,   0  ],
            [10,  0  ],
            [14,  8  ],
            [5,   13 ],
            [-4,  8  ]
        ]);
    }
}

// polygon with explicit outer path (same points, explicit paths[0])
translate([25, 0, 0]) {
    linear_extrude(height=5) {
        polygon(
            points=[[0,0],[12,0],[12,4],[4,4],[4,12],[0,12]],
            paths=[[0,1,2,3,4,5]]
        );
    }
}

// polygon with hole: outer square minus inner triangle
translate([50, 0, 0]) {
    linear_extrude(height=5) {
        polygon(
            points=[[0,0],[16,0],[16,16],[0,16],  [4,4],[12,4],[8,12]],
            paths=[[0,1,2,3],[4,5,6]]
        );
    }
}
