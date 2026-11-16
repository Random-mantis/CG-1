#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 viewProj;
	vec4 cameraPos;
	vec4 ambient;
	vec4 dirDirection;
	vec4 dirColor;
	vec4 lightCounts;
} scene;

layout(set = 1, binding = 0) uniform ModelData {
	mat4 model;
	mat4 normalMatrix;
	vec4 tint;
} model;

layout(push_constant) uniform Push {
	float time;
	float waveStrength;
	float waveFreq;
	float waveEnabled;
} push;

void main() {
	vec4 world = model.model * vec4(inPosition, 1.0);
	fragPos = world.xyz;
	fragNormal = mat3(model.normalMatrix) * inNormal;
	fragUV = inUV;
	gl_Position = scene.viewProj * world;
}
