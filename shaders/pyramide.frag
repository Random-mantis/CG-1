#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
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
	vec4 tint;
} model;

layout(set = 2, binding = 0) uniform sampler2D albedoMap;
layout(set = 2, binding = 1) uniform sampler2D specularMap;
layout(set = 2, binding = 2) uniform sampler2D emissiveMap;

layout(push_constant) uniform Push {
	float time;
	float waveStrength;
	float waveFreq;
	float waveEnabled;
} push;

vec2 waveOffset(vec2 uv, float phase) {
	return vec2(
		sin(uv.y * push.waveFreq + push.time + phase),
		cos(uv.x * push.waveFreq * 0.9 + push.time * 0.8 + phase)
	) * push.waveStrength;
}

vec3 sampleAlbedo(vec2 uv) {
	if (push.waveEnabled < 0.5) {
		return texture(albedoMap, uv).rgb;
	}
	vec2 uvA = uv + waveOffset(uv, 0.0);
	vec2 uvB = uv + waveOffset(uv, 2.1) * 0.65;
	vec3 a = texture(albedoMap, uvA).rgb;
	vec3 b = texture(albedoMap, uvB).rgb;
	vec3 c = texture(albedoMap, uv).rgb;
	vec3 blended = (a * vec3(1.08, 1.0, 0.95) + b * vec3(0.95, 1.0, 1.08)) * 0.5;
	return mix(c, blended, 0.85);
}

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 lightColor, float atten, vec3 albedo, float specMask) {
	float diff = max(dot(N, L), 0.0);
	vec3 H = normalize(L + V);
	float spec = pow(max(dot(N, H), 0.0), max(model.tint.w, 1.0));
	vec3 diffuse = diff * lightColor * albedo;
	vec3 specular = spec * lightColor * specMask;
	return (diffuse + specular) * atten;
}

void main() {
	vec2 uv = fragUV;
	vec3 albedo = sampleAlbedo(uv) * model.tint.rgb;
	float specMask = texture(specularMap, uv).r;
	vec3 emissive = texture(emissiveMap, uv).rgb;

	vec3 N = normalize(fragNormal);
	vec3 V = normalize(scene.cameraPos.xyz - fragPos);
	vec3 color = scene.ambient.rgb * albedo;

	vec3 Ldir = normalize(-scene.dirDirection.xyz);
	vec3 dirCol = scene.dirColor.rgb * scene.dirColor.w;
	color += blinnPhong(N, V, Ldir, dirCol, 1.0, albedo, specMask);

	int pointCount = int(scene.lightCounts.x);
	for (int i = 0; i < pointCount; ++i) {
		vec3 toLight = points[i].position.xyz - fragPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = 1.0 / (1.0 + dist * dist);
		vec3 lcol = points[i].color.rgb * points[i].color.w;
		color += blinnPhong(N, V, L, lcol, atten, albedo, specMask);
	}

	int spotCount = int(scene.lightCounts.y);
	for (int i = 0; i < spotCount; ++i) {
		vec3 toLight = spots[i].position.xyz - fragPos;
		float dist = length(toLight);
		vec3 L = toLight / max(dist, 0.0001);
		float atten = 1.0 / (1.0 + dist * dist);
		float cosTheta = dot(-L, normalize(spots[i].direction.xyz));
		float soft = clamp((cosTheta - spots[i].cone.y) / max(spots[i].cone.x - spots[i].cone.y, 0.0001), 0.0, 1.0);
		vec3 lcol = spots[i].color.rgb * spots[i].color.w;
		color += blinnPhong(N, V, L, lcol, atten * soft, albedo, specMask);
	}

	color += emissive;
	outColor = vec4(color, 1.0);
}
