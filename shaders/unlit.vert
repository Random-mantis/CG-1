#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform Push {
	mat4 mvp;
	vec4 color;
} push;

void main() {
	gl_Position = push.mvp * vec4(inPosition, 1.0);
	fragColor = push.color.rgb;
}
