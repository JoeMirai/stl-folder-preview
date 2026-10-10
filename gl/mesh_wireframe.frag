#version 120

uniform float zoom;
uniform vec4 wire_color;

varying vec3 ec_pos;

void main() {
    gl_FragColor = wire_color;
}
