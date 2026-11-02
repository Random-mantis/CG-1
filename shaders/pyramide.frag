#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneData {
	mat4 viewProj;
	vec4 cameraPos;
	vec4 ambient;
	vec4 dirDirection;
	vec4 dirColor;
	vec4 lightCounts;
} scene;

struct PointLight {
	vec4 position;
	vec4 color;
};

struct SpotLight {
	vec4 position;
	vec4 direction;
	vec4 color;
	vec4 cone;
};

layout(set = 0, binding = 1) readonly buffer PointLights {
	PointLight points[];
};

layout(set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spots[];
};

layout(set = 1, binding = 0) uniform ModelData {
	mat4 model;
	mat4 normalMatrix;
	vec4 albedo;
	vec4 specular;
} model;

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, float atten) {
	float diff = max(dot(N, L), 0.0);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), max(model.specular.w, 1.0));
	vec3 diffuse = diff * lightColor * model.albedo.rgb;
	vec3 specular = spec * lightColor * model.specular.rgb;
	return (diffuse + specular) * atten;
}

void main() {
	vec3 N = normalize(fragNormal);
	vec3 V = normalize(scene.cameraPos.xyz - fragPos);
	vec3 color = scene.ambient.rgb * model.albedo.rgb;

	vec3 Ldir = normalize(-scene.dirDirection.xyz);
	vec3 dirCol = scene.dirColor.rgb * scene.dirColor.w;
	color += blinnPhong(N, V, Ldir, dirCol, 1.0);

	int pointCount = int(scene.lightCounts.x);
	for (int i = 0; i < pointCount; ++i) {
		vec3 toLight = points[i].position.xyz - fragPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = 1.0 / (1.0 + dist * dist);
		vec3 lcol = points[i].color.rgb * points[i].color.w;
		color += blinnPhong(N, V, L, lcol, atten);
	}

	int spotCount = int(scene.lightCounts.y);
	for (int i = 0; i < spotCount; ++i) {
		vec3 toLight = spots[i].position.xyz - fragPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = 1.0 / (1.0 + dist * dist);
		float cosTheta = dot(-L, normalize(spots[i].direction.xyz));
		float inner = spots[i].cone.x;
		float outer = spots[i].cone.y;
		float soft = clamp((cosTheta - outer) / max(inner - outer, 0.0001), 0.0, 1.0);
		vec3 lcol = spots[i].color.rgb * spots[i].color.w;
		color += blinnPhong(N, V, L, lcol, atten * soft);
	}

	outColor = vec4(color, 1.0);
}
