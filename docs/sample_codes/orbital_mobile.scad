// 3DScad sample: parametric orbital mobile
// Demonstrates: sin, cos, sqrt, pow, abs, min, max, PI
// A decorative 3D sculpture with orbiting spheres in elliptical patterns

module scene_model() {
    orbit_r = 40;
    count = 16;
    small_r = 4;
    large_r = 6;

    union() {
        sphere(r=orbit_r * 0.2);

        for (i = [0 : count - 1]) {
            translate([sin(i * 360 / count) * orbit_r, cos(i * 360 / count) * orbit_r * 0.65, sin(i * 720 / count) * orbit_r * 0.3]) {
                sphere(r=small_r + abs(sin(i * 720 / count)) * 2);
            }
        }

        for (i = [0 : count / 2 - 1]) {
            translate([sin(i * 720 / count + 22.5) * orbit_r * 0.5, cos(i * 720 / count + 22.5) * orbit_r * 0.5, 0]) {
                sphere(r=large_r * 0.55);
            }
        }

        for (i = [0 : 6]) {
            translate([0, 0, (i / 6 - 0.5) * orbit_r * sqrt(1 - pow(i / 6 * 2 - 1, 2))]) {
                sphere(r=large_r * min(1, (1 - abs(i / 6 - 0.5)) * 2));
            }
        }

        difference() {
            cylinder(h=1.5, r=orbit_r * 0.8, center=true);
            cylinder(h=2.5, r=orbit_r * 0.65, center=true);
        }
    }
}

scene_model();
