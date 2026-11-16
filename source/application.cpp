#include "application.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>

namespace application {

constexpr int kMaxPointLights = 32;
constexpr int kMaxSpotLights = 32;
constexpr int kMaxModels = 8;
constexpr int kMaxMaterials = 8;

VkPipeline pipeline = VK_NULL_HANDLE; // Превращает вершины в пиксели
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE; // Передача данных в Шейдеры pipeline
VkShaderModule vertex_shader = VK_NULL_HANDLE; // Код из файла pyramide.vert.csv
VkShaderModule fragment_shader = VK_NULL_HANDLE; // Код из файла pyramide.frag.csv

VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout model_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout material_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet scene_set = VK_NULL_HANDLE;
VkDescriptorSet model_set = VK_NULL_HANDLE;

VkBuffer scene_ubo = VK_NULL_HANDLE;
VmaAllocation scene_ubo_alloc = VK_NULL_HANDLE;
void* scene_ubo_mapped = nullptr;

VkBuffer model_ubo = VK_NULL_HANDLE;
VmaAllocation model_ubo_alloc = VK_NULL_HANDLE;
void* model_ubo_mapped = nullptr;
uint32_t model_ubo_align = 256;

VkBuffer point_ssbo = VK_NULL_HANDLE;
VmaAllocation point_ssbo_alloc = VK_NULL_HANDLE;
void* point_ssbo_mapped = nullptr;

VkBuffer spot_ssbo = VK_NULL_HANDLE;
VmaAllocation spot_ssbo_alloc = VK_NULL_HANDLE;
void* spot_ssbo_mapped = nullptr;

VkSampler texture_sampler = VK_NULL_HANDLE;
VkCommandPool upload_pool = VK_NULL_HANDLE;

VkPipeline unlit_pipeline = VK_NULL_HANDLE;
VkPipeline unlit_line_pipeline = VK_NULL_HANDLE;
VkPipelineLayout unlit_layout = VK_NULL_HANDLE;
VkShaderModule unlit_vert = VK_NULL_HANDLE;
VkShaderModule unlit_frag = VK_NULL_HANDLE;

struct SceneUniform {
	alignas(16) glm::mat4 view_proj;
	alignas(16) glm::vec4 camera_pos;
	alignas(16) glm::vec4 ambient;
	alignas(16) glm::vec4 dir_direction;
	alignas(16) glm::vec4 dir_color;
	alignas(16) glm::vec4 light_counts;
};

struct ModelUniform {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 normal_matrix;
	alignas(16) glm::vec4 tint;
};

struct PointLightCPU {
	glm::vec4 position;
	glm::vec4 color;
};

struct SpotLightCPU {
	glm::vec4 position;
	glm::vec4 direction;
	glm::vec4 color;
	glm::vec4 cone;
};

struct UnlitPush {
	glm::mat4 mvp;
	glm::vec4 color;
};

struct WavePush {
	float time;
	float wave_strength;
	float wave_freq;
	float wave_enabled;
};

struct VertexPNU {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
};

struct GpuTexture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct Material {
	std::string name;
	GpuTexture albedo;
	GpuTexture specular;
	GpuTexture emissive;
	VkDescriptorSet set = VK_NULL_HANDLE;
};

struct MeshGPU {
	VkBuffer vertex_buffer = VK_NULL_HANDLE;
	VmaAllocation vertex_allocation = VK_NULL_HANDLE;
	VkBuffer index_buffer = VK_NULL_HANDLE;
	VmaAllocation index_allocation = VK_NULL_HANDLE;
	uint32_t index_count = 0;
};

struct SceneObject {
	MeshGPU* mesh = nullptr;
	int material_index = 0;
	float pos[3] = {0.0f, 0.0f, 0.0f};
	float rot[3] = {0.0f, 0.0f, 0.0f};
	float scale[3] = {1.0f, 1.0f, 1.0f};
	float tint[3] = {1.0f, 1.0f, 1.0f};
	float shininess = 32.0f;
	bool wave = false;
};

MeshGPU pyramid_mesh;
MeshGPU floor_mesh;
MeshGPU cube_mesh;
MeshGPU cone_mesh;

std::vector<Material> materials;
std::vector<SceneObject> objects;
GpuTexture black_texture;

float cam_pos[3] = {0.0f, 2.0f, 6.0f};
float cam_yaw = -1.5707963f;
float cam_pitch = -0.35f;
float cam_fov = 60.0f;

float ambient[3] = {0.08f, 0.08f, 0.10f};
float dir_direction[3] = {-0.4f, -1.0f, -0.3f};
float dir_color[3] = {1.0f, 0.95f, 0.85f};
float dir_intensity = 1.0f;

std::vector<PointLightCPU> point_lights;
std::vector<SpotLightCPU> spot_lights;
bool show_light_viz = true;
float wave_strength = 0.035f;
float wave_freq = 10.0f;
double app_time = 0.0;

struct Pyramide { // Структура пирамиды
	struct Vertex { // Структура Вершины
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
	};

	const float edge = 2.0f; // Длина ребра
	float radius = 0.0f; // Радиус окружности куда вписано основание
	float height = 0.0f; // Высота Пирамиды
	float y_base = 0.0f; // Высота по основанию Y в декартовых координатах
	float y_apex = 0.0f; // Углы треугольников пирамиды (все они равны 60 градусов)
	std::vector<Vertex> vertices; // Сами вершины
	std::vector<std::uint16_t> indices; // Правила индексации

	Pyramide() { // делаем пирамиду
		radius = edge / std::sqrt(3.0f);
		height = std::sqrt(6.0f) / 3.0f * edge;
		y_base = -height / 4.0f;
		y_apex = 3.0f * height / 4.0f;
		const glm::vec3 p0{0.0f, y_apex, 0.0f};
		const glm::vec3 p1{radius, y_base, 0.0f};
		const glm::vec3 p2{-0.5f * radius, y_base, 0.5f * radius * std::sqrt(3.0f)};
		const glm::vec3 p3{-0.5f * radius, y_base, -0.5f * radius * std::sqrt(3.0f)};

		auto add_face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
			const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
			const uint16_t base = static_cast<uint16_t>(vertices.size());
			vertices.push_back({a, n, {0.0f, 1.0f}});
			vertices.push_back({b, n, {1.0f, 1.0f}});
			vertices.push_back({c, n, {0.5f, 0.0f}});
			indices.push_back(base);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
		};

		add_face(p0, p2, p1);
		add_face(p0, p3, p2);
		add_face(p0, p1, p3);
		add_face(p1, p2, p3);
	}
};

Pyramide pyramide; // Собственно сама пирамида

bool create_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                        VkBuffer& buffer, VmaAllocation& allocation, void** mapped_out = nullptr) {
	auto& context = graphics::internal::context;
	const VkBufferCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
	};
	const VmaAllocationCreateInfo alloc{
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	VmaAllocationInfo mapped{};
	if (vmaCreateBuffer(context.allocator, &info, &alloc, &buffer, &allocation, &mapped) != VK_SUCCESS) return false;
	if (data != nullptr) {
		std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
	}
	if (mapped_out != nullptr) {
		*mapped_out = mapped.pMappedData;
	}
	return true;
}

bool begin_upload(VkCommandBuffer& cmd) {
	auto& context = graphics::internal::context;
	const VkCommandBufferAllocateInfo alloc{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = upload_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	if (vkAllocateCommandBuffers(context.device, &alloc, &cmd) != VK_SUCCESS) return false;
	const VkCommandBufferBeginInfo begin{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	return vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS;
}

void end_upload(VkCommandBuffer cmd) {
	auto& context = graphics::internal::context;
	vkEndCommandBuffer(cmd);
	const VkSubmitInfo submit{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd,
	};
	vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE);
	vkQueueWaitIdle(context.graphics_queue);
	vkFreeCommandBuffers(context.device, upload_pool, 1, &cmd);
}

bool create_device_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                          VkBuffer& buffer, VmaAllocation& allocation) {
	auto& context = graphics::internal::context;
	VkBuffer staging = VK_NULL_HANDLE;
	VmaAllocation staging_alloc = VK_NULL_HANDLE;
	if (!create_host_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data, staging, staging_alloc)) return false;

	const VkBufferCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};
	const VmaAllocationCreateInfo alloc{
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};
	if (vmaCreateBuffer(context.allocator, &info, &alloc, &buffer, &allocation, nullptr) != VK_SUCCESS) {
		vmaDestroyBuffer(context.allocator, staging, staging_alloc);
		return false;
	}

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (!begin_upload(cmd)) {
		vmaDestroyBuffer(context.allocator, staging, staging_alloc);
		vmaDestroyBuffer(context.allocator, buffer, allocation);
		return false;
	}
	const VkBufferCopy copy{.size = size};
	vkCmdCopyBuffer(cmd, staging, buffer, 1, &copy);
	end_upload(cmd);
	vmaDestroyBuffer(context.allocator, staging, staging_alloc);
	return true;
}

void destroy_texture(GpuTexture& tex) {
	auto& context = graphics::internal::context;
	if (tex.view) vkDestroyImageView(context.device, tex.view, nullptr);
	if (tex.image) vmaDestroyImage(context.allocator, tex.image, tex.allocation);
	tex = {};
}

bool create_texture_rgba(const unsigned char* pixels, int width, int height, GpuTexture& out) {
	auto& context = graphics::internal::context;
	const VkDeviceSize size = VkDeviceSize(width) * VkDeviceSize(height) * 4;

	VkBuffer staging = VK_NULL_HANDLE;
	VmaAllocation staging_alloc = VK_NULL_HANDLE;
	if (!create_host_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels, staging, staging_alloc)) return false;

	const VkImageCreateInfo image_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.extent = {uint32_t(width), uint32_t(height), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	const VmaAllocationCreateInfo alloc{
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};
	if (vmaCreateImage(context.allocator, &image_info, &alloc, &out.image, &out.allocation, nullptr) != VK_SUCCESS) {
		vmaDestroyBuffer(context.allocator, staging, staging_alloc);
		return false;
	}

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (!begin_upload(cmd)) {
		vmaDestroyBuffer(context.allocator, staging, staging_alloc);
		destroy_texture(out);
		return false;
	}

	const VkImageMemoryBarrier to_dst{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = out.image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &to_dst);

	const VkBufferImageCopy region{
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageExtent = {uint32_t(width), uint32_t(height), 1},
	};
	vkCmdCopyBufferToImage(cmd, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	const VkImageMemoryBarrier to_shader{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = out.image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &to_shader);
	end_upload(cmd);
	vmaDestroyBuffer(context.allocator, staging, staging_alloc);

	const VkImageViewCreateInfo view_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = out.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	};
	if (vkCreateImageView(context.device, &view_info, nullptr, &out.view) != VK_SUCCESS) {
		destroy_texture(out);
		return false;
	}
	out.width = uint32_t(width);
	out.height = uint32_t(height);
	return true;
}

bool load_texture_file(const std::string& path, GpuTexture& out) {
	int w = 0, h = 0, comp = 0;
	stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
	if (!pixels) return false;
	const bool ok = create_texture_rgba(pixels, w, h, out);
	stbi_image_free(pixels);
	return ok;
}

std::string find_texture_path(const char* name) {
	const std::string a = std::string("textures/") + name;
	std::ifstream f(a, std::ios::binary);
	if (f) return a;
	const std::string b = std::string("../textures/") + name;
	std::ifstream f2(b, std::ios::binary);
	if (f2) return b;
	return a;
}

float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerp(float a, float b, float t) { return a + t * (b - a); }
float grad(int hash, float x, float y) {
	const int h = hash & 3;
	const float u = h < 2 ? x : y;
	const float v = h < 2 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float perlin2d(float x, float y) {
	static const int p[512] = {
		151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
		190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
		125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
		105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
		135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
		82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
		153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
		251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
		157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,
		66,215,61,156,180,
		151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
		190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
		125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
		105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,
		135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,
		82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,
		153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,
		251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,
		157,184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,
		66,215,61,156,180
	};
	const int xi = int(std::floor(x)) & 255;
	const int yi = int(std::floor(y)) & 255;
	const float xf = x - std::floor(x);
	const float yf = y - std::floor(y);
	const float u = fade(xf);
	const float v = fade(yf);
	const int aa = p[p[xi] + yi];
	const int ab = p[p[xi] + yi + 1];
	const int ba = p[p[xi + 1] + yi];
	const int bb = p[p[xi + 1] + yi + 1];
	return lerp(lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u),
	            lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u), v);
}

bool create_checker_texture(int size, GpuTexture& out) {
	std::vector<unsigned char> pixels(size_t(size) * size_t(size) * 4);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const bool dark = ((x / 32) + (y / 32)) % 2 == 0;
			const unsigned char c = dark ? 60 : 200;
			const size_t i = (size_t(y) * size + x) * 4;
			pixels[i] = c;
			pixels[i + 1] = c;
			pixels[i + 2] = unsigned char(dark ? 70 : 180);
			pixels[i + 3] = 255;
		}
	}
	return create_texture_rgba(pixels.data(), size, size, out);
}

bool create_specular_from_luma(const unsigned char* rgba, int w, int h, GpuTexture& out) {
	std::vector<unsigned char> pixels(size_t(w) * size_t(h) * 4);
	for (int i = 0; i < w * h; ++i) {
		const float r = rgba[i * 4] / 255.0f;
		const float g = rgba[i * 4 + 1] / 255.0f;
		const float b = rgba[i * 4 + 2] / 255.0f;
		const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
		const unsigned char s = static_cast<unsigned char>(std::clamp(luma * 1.4f, 0.0f, 1.0f) * 255.0f);
		pixels[i * 4] = s;
		pixels[i * 4 + 1] = s;
		pixels[i * 4 + 2] = s;
		pixels[i * 4 + 3] = 255;
	}
	return create_texture_rgba(pixels.data(), w, h, out);
}

bool create_emissive_perlin(int size, GpuTexture& out) {
	std::vector<unsigned char> pixels(size_t(size) * size_t(size) * 4);
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const float n = perlin2d(x * 0.05f, y * 0.05f);
			const float v = std::clamp((n * 0.5f + 0.5f), 0.0f, 1.0f);
			const float glow = std::pow(v, 3.0f);
			const size_t i = (size_t(y) * size + x) * 4;
			pixels[i] = static_cast<unsigned char>(glow * 40.0f);
			pixels[i + 1] = static_cast<unsigned char>(glow * 120.0f);
			pixels[i + 2] = static_cast<unsigned char>(glow * 255.0f);
			pixels[i + 3] = 255;
		}
	}
	return create_texture_rgba(pixels.data(), size, size, out);
}

bool create_black_texture(GpuTexture& out) {
	const unsigned char pixel[4] = {0, 0, 0, 255};
	return create_texture_rgba(pixel, 1, 1, out);
}

std::vector<unsigned char> read_file(const std::string& path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) return {};
	const auto size = file.tellg();
	std::vector<unsigned char> code(static_cast<size_t>(size));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), size);
	return code;
}

std::vector<unsigned char> read_shader(const char* name) {
	std::string a = std::string("shaders/") + name;
	auto code = read_file(a);
	if (!code.empty()) return code;
	a = std::string("../shaders/") + name;
	return read_file(a);
}

glm::mat4 make_model_matrix(const float pos[3], const float rot[3], const float scl[3]) {
	glm::mat4 m(1.0f);
	m = glm::translate(m, glm::vec3(pos[0], pos[1], pos[2]));
	m = glm::rotate(m, rot[1], glm::vec3(0.0f, 1.0f, 0.0f));
	m = glm::rotate(m, rot[0], glm::vec3(1.0f, 0.0f, 0.0f));
	m = glm::rotate(m, rot[2], glm::vec3(0.0f, 0.0f, 1.0f));
	m = glm::scale(m, glm::vec3(scl[0], scl[1], scl[2]));
	return m;
}

glm::vec3 camera_forward() {
	return glm::normalize(glm::vec3(
		std::cos(cam_pitch) * std::cos(cam_yaw),
		std::sin(cam_pitch),
		std::cos(cam_pitch) * std::sin(cam_yaw)));
}

glm::mat4 camera_view() {
	const glm::vec3 eye(cam_pos[0], cam_pos[1], cam_pos[2]);
	return glm::lookAt(eye, eye + camera_forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

bool create_shader_module(const std::vector<unsigned char>& code, VkShaderModule& module) {
	auto& context = graphics::internal::context;
	const VkShaderModuleCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const std::uint32_t*>(code.data()),
	};
	return vkCreateShaderModule(context.device, &info, nullptr, &module) == VK_SUCCESS;
}

bool create_mesh_gpu(const std::vector<VertexPNU>& verts, const std::vector<uint16_t>& indices, MeshGPU& mesh) {
	if (!create_device_buffer(verts.size() * sizeof(VertexPNU), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                         verts.data(), mesh.vertex_buffer, mesh.vertex_allocation)) return false;
	if (!create_device_buffer(indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                         indices.data(), mesh.index_buffer, mesh.index_allocation)) return false;
	mesh.index_count = uint32_t(indices.size());
	return true;
}

void destroy_mesh(MeshGPU& mesh) {
	auto& context = graphics::internal::context;
	if (mesh.vertex_buffer) vmaDestroyBuffer(context.allocator, mesh.vertex_buffer, mesh.vertex_allocation);
	if (mesh.index_buffer) vmaDestroyBuffer(context.allocator, mesh.index_buffer, mesh.index_allocation);
	mesh = {};
}

bool write_material_set(Material& mat) {
	auto& context = graphics::internal::context;
	const VkDescriptorImageInfo images[3] = {
		{.sampler = texture_sampler, .imageView = mat.albedo.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{.sampler = texture_sampler, .imageView = mat.specular.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{.sampler = texture_sampler, .imageView = mat.emissive.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	};
	VkWriteDescriptorSet writes[3]{};
	for (int i = 0; i < 3; ++i) {
		writes[i] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = mat.set,
			.dstBinding = uint32_t(i),
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &images[i],
		};
	}
	vkUpdateDescriptorSets(context.device, 3, writes, 0, nullptr);
	return true;
}

bool add_material(const std::string& name, GpuTexture albedo, GpuTexture specular, GpuTexture emissive) {
	auto& context = graphics::internal::context;
	Material mat;
	mat.name = name;
	mat.albedo = albedo;
	mat.specular = specular.view ? specular : black_texture;
	mat.emissive = emissive.view ? emissive : black_texture;

	const VkDescriptorSetAllocateInfo alloc{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &material_set_layout,
	};
	if (vkAllocateDescriptorSets(context.device, &alloc, &mat.set) != VK_SUCCESS) return false;
	write_material_set(mat);
	materials.push_back(std::move(mat));
	return true;
}

bool create_materials() {
	if (!create_black_texture(black_texture)) return false;

	GpuTexture owl_albedo{};
	if (!load_texture_file(find_texture_path("owl.png"), owl_albedo)) return false;

	int w = 0, h = 0, comp = 0;
	stbi_uc* owl_pixels = stbi_load(find_texture_path("owl.png").c_str(), &w, &h, &comp, 4);
	GpuTexture owl_spec{};
	GpuTexture owl_emissive{};
	if (owl_pixels) {
		create_specular_from_luma(owl_pixels, w, h, owl_spec);
		stbi_image_free(owl_pixels);
	}
	if (!owl_spec.view) {
		owl_spec = black_texture;
	}
	create_emissive_perlin(256, owl_emissive);
	if (!add_material("Owl", owl_albedo, owl_spec, owl_emissive)) return false;

	GpuTexture checker{};
	if (!create_checker_texture(256, checker)) return false;
	if (!add_material("Checker", checker, black_texture, black_texture)) return false;

	GpuTexture noise_alb{};
	create_emissive_perlin(256, noise_alb);
	GpuTexture noise_em{};
	create_emissive_perlin(128, noise_em);
	if (!add_material("GlowNoise", noise_alb, black_texture, noise_em)) return false;

	return true;
}

bool create_meshes() {
	std::vector<VertexPNU> pyramid_verts;
	pyramid_verts.reserve(pyramide.vertices.size());
	for (const auto& v : pyramide.vertices) {
		pyramid_verts.push_back({v.position, v.normal, v.uv});
	}
	if (!create_mesh_gpu(pyramid_verts, pyramide.indices, pyramid_mesh)) return false;

	const float s = 8.0f;
	const std::vector<VertexPNU> floor_verts = {
		{{-s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
		{{ s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}, {4.0f, 0.0f}},
		{{ s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {4.0f, 4.0f}},
		{{-s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}, {0.0f, 4.0f}},
	};
	const std::vector<uint16_t> floor_idx = {0, 1, 2, 0, 2, 3};
	if (!create_mesh_gpu(floor_verts, floor_idx, floor_mesh)) return false;

	const float hs = 0.12f;
	const std::vector<VertexPNU> cube_verts = {
		{{-hs,-hs,-hs},{0,0,-1},{0,0}}, {{hs,-hs,-hs},{0,0,-1},{1,0}}, {{hs,hs,-hs},{0,0,-1},{1,1}}, {{-hs,hs,-hs},{0,0,-1},{0,1}},
		{{-hs,-hs, hs},{0,0, 1},{0,0}}, {{hs,-hs, hs},{0,0, 1},{1,0}}, {{hs,hs, hs},{0,0, 1},{1,1}}, {{-hs,hs, hs},{0,0, 1},{0,1}},
	};
	const std::vector<uint16_t> cube_idx = {
		0,1,2, 0,2,3, 4,6,5, 4,7,6,
		0,4,5, 0,5,1, 2,6,7, 2,7,3,
		0,3,7, 0,7,4, 1,5,6, 1,6,2,
	};
	if (!create_mesh_gpu(cube_verts, cube_idx, cube_mesh)) return false;

	std::vector<VertexPNU> cone;
	const int segments = 24;
	cone.push_back({{0,0,0},{0,0,1},{0,0}});
	cone.push_back({{0,0,1},{0,0,1},{1,0}});
	for (int i = 0; i < segments; ++i) {
		const float a0 = float(i) / float(segments) * 6.2831853f;
		const float a1 = float(i + 1) / float(segments) * 6.2831853f;
		const glm::vec3 p0{0.5f * std::cos(a0), 0.5f * std::sin(a0), 1.0f};
		const glm::vec3 p1{0.5f * std::cos(a1), 0.5f * std::sin(a1), 1.0f};
		cone.push_back({{0,0,0},{0,0,1},{0,0}});
		cone.push_back({p0,{0,0,1},{1,0}});
		cone.push_back({p0,{0,0,1},{1,0}});
		cone.push_back({p1,{0,0,1},{0,1}});
	}
	std::vector<uint16_t> cone_idx(cone.size());
	for (uint16_t i = 0; i < cone_idx.size(); ++i) cone_idx[i] = i;
	return create_mesh_gpu(cone, cone_idx, cone_mesh);
}

bool create_lit_pipeline() {
	auto& context = graphics::internal::context;

	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(context.physical_device, &props);
	model_ubo_align = uint32_t(props.limits.minUniformBufferOffsetAlignment);
	if (model_ubo_align < sizeof(ModelUniform)) {
		model_ubo_align = uint32_t((sizeof(ModelUniform) + model_ubo_align - 1) / model_ubo_align * model_ubo_align);
	} else {
		const uint32_t stride = uint32_t((sizeof(ModelUniform) + model_ubo_align - 1) / model_ubo_align * model_ubo_align);
		model_ubo_align = stride;
	}

	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
	};
	const VkDescriptorSetLayoutCreateInfo scene_layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = scene_bindings,
	};
	if (vkCreateDescriptorSetLayout(context.device, &scene_layout_info, nullptr, &scene_set_layout) != VK_SUCCESS) return false;

	const VkDescriptorSetLayoutBinding model_binding{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo model_layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &model_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &model_layout_info, nullptr, &model_set_layout) != VK_SUCCESS) return false;

	const VkDescriptorSetLayoutBinding material_bindings[] = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
	};
	const VkDescriptorSetLayoutCreateInfo material_layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = material_bindings,
	};
	if (vkCreateDescriptorSetLayout(context.device, &material_layout_info, nullptr, &material_set_layout) != VK_SUCCESS) return false;

	const VkDescriptorPoolSize pool_sizes[] = {
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 8},
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = 8},
		{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 8},
		{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 64},
	};
	const VkDescriptorPoolCreateInfo pool_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 32,
		.poolSizeCount = 4,
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) return false;

	{
		const VkDescriptorSetAllocateInfo alloc{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &scene_set_layout,
		};
		if (vkAllocateDescriptorSets(context.device, &alloc, &scene_set) != VK_SUCCESS) return false;
	}
	{
		const VkDescriptorSetAllocateInfo alloc{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &model_set_layout,
		};
		if (vkAllocateDescriptorSets(context.device, &alloc, &model_set) != VK_SUCCESS) return false;
	}

	if (!create_host_buffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
	                        scene_ubo, scene_ubo_alloc, &scene_ubo_mapped)) return false;
	if (!create_host_buffer(VkDeviceSize(model_ubo_align) * kMaxModels, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
	                        model_ubo, model_ubo_alloc, &model_ubo_mapped)) return false;
	if (!create_host_buffer(sizeof(PointLightCPU) * kMaxPointLights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr,
	                        point_ssbo, point_ssbo_alloc, &point_ssbo_mapped)) return false;
	if (!create_host_buffer(sizeof(SpotLightCPU) * kMaxSpotLights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr,
	                        spot_ssbo, spot_ssbo_alloc, &spot_ssbo_mapped)) return false;

	const VkDescriptorBufferInfo scene_info{.buffer = scene_ubo, .offset = 0, .range = sizeof(SceneUniform)};
	const VkDescriptorBufferInfo point_info{.buffer = point_ssbo, .offset = 0, .range = VK_WHOLE_SIZE};
	const VkDescriptorBufferInfo spot_info{.buffer = spot_ssbo, .offset = 0, .range = VK_WHOLE_SIZE};
	const VkDescriptorBufferInfo model_info{.buffer = model_ubo, .offset = 0, .range = sizeof(ModelUniform)};

	VkWriteDescriptorSet writes[4]{};
	writes[0] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_set, .dstBinding = 0,
	             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &scene_info};
	writes[1] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_set, .dstBinding = 1,
	             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &point_info};
	writes[2] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = scene_set, .dstBinding = 2,
	             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &spot_info};
	writes[3] = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = model_set, .dstBinding = 0,
	             .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .pBufferInfo = &model_info};
	vkUpdateDescriptorSets(context.device, 4, writes, 0, nullptr);

	const VkSamplerCreateInfo sampler_info{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxLod = 1.0f,
	};
	if (vkCreateSampler(context.device, &sampler_info, nullptr, &texture_sampler) != VK_SUCCESS) return false;

	const VkPushConstantRange push{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(WavePush),
	};
	VkDescriptorSetLayout layouts[3] = {scene_set_layout, model_set_layout, material_set_layout};
	const VkPipelineLayoutCreateInfo layout_info{ // Описание pipeline_layout
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 3,
		.pSetLayouts = layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};
	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) return false;

	const VkPipelineShaderStageCreateInfo stages[]{ // Описание начала pipeline
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
		 .module = vertex_shader, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		 .module = fragment_shader, .pName = "main"},
	};

	const VkVertexInputBindingDescription binding{ // Описание шага по буферу
		.binding = 0,
		.stride = sizeof(VertexPNU),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attributes[]{ // Описание попадания байтов шага в шейдер
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(VertexPNU, position)},
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(VertexPNU, normal)},
		{.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(VertexPNU, uv)},
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 3,
		.pVertexAttributeDescriptions = attributes,
	};
	const VkPipelineInputAssemblyStateCreateInfo input_assembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depth{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState blend_attachment{
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo blend{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};
	const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamic{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};
	const VkGraphicsPipelineCreateInfo pipeline_info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth,
		.pColorBlendState = &blend,
		.pDynamicState = &dynamic,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};
	return vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) == VK_SUCCESS;
}

bool create_unlit_pipelines() {
	auto& context = graphics::internal::context;
	const VkPushConstantRange push{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.offset = 0,
		.size = sizeof(UnlitPush),
	};
	const VkPipelineLayoutCreateInfo layout_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push,
	};
	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &unlit_layout) != VK_SUCCESS) return false;

	const VkPipelineShaderStageCreateInfo stages[] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
		 .module = unlit_vert, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		 .module = unlit_frag, .pName = "main"},
	};
	const VkVertexInputBindingDescription binding{
		.binding = 0, .stride = sizeof(VertexPNU), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attribute{
		.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(VertexPNU, position),
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = &attribute,
	};
	VkPipelineInputAssemblyStateCreateInfo input_assembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	const VkPipelineViewportStateCreateInfo viewport{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1,
	};
	const VkPipelineRasterizationStateCreateInfo rasterization{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
	};
	const VkPipelineMultisampleStateCreateInfo multisample{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	const VkPipelineDepthStencilStateCreateInfo depth{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS,
	};
	const VkPipelineColorBlendAttachmentState blend_attachment{
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	const VkPipelineColorBlendStateCreateInfo blend{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, .pAttachments = &blend_attachment,
	};
	const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamic{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, .pDynamicStates = dynamic_states,
	};
	VkGraphicsPipelineCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2, .pStages = stages,
		.pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport, .pRasterizationState = &rasterization,
		.pMultisampleState = &multisample, .pDepthStencilState = &depth,
		.pColorBlendState = &blend, .pDynamicState = &dynamic,
		.layout = unlit_layout, .renderPass = context.render_pass, .subpass = 0,
	};
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &unlit_pipeline) != VK_SUCCESS) return false;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	return vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &unlit_line_pipeline) == VK_SUCCESS;
}

bool initialize() {
	auto& context = graphics::internal::context;
	const VkCommandPoolCreateInfo pool_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};
	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &upload_pool) != VK_SUCCESS) return false;

	const auto vertex_code = read_shader("pyramide.vert.spv");
	const auto fragment_code = read_shader("pyramide.frag.spv");
	const auto unlit_v_code = read_shader("unlit.vert.spv");
	const auto unlit_f_code = read_shader("unlit.frag.spv");
	if (vertex_code.empty() || fragment_code.empty() || unlit_v_code.empty() || unlit_f_code.empty()) return false;
	if (!create_shader_module(vertex_code, vertex_shader)) return false;
	if (!create_shader_module(fragment_code, fragment_shader)) return false;
	if (!create_shader_module(unlit_v_code, unlit_vert)) return false;
	if (!create_shader_module(unlit_f_code, unlit_frag)) return false;

	if (!create_lit_pipeline()) return false;
	if (!create_materials()) return false;
	if (!create_meshes()) return false;
	if (!create_unlit_pipelines()) return false;

	objects.push_back({
		.mesh = &pyramid_mesh,
		.material_index = 0,
		.pos = {0.0f, 0.0f, 0.0f},
		.rot = {0.0f, 0.0f, 0.0f},
		.scale = {1.0f, 1.0f, 1.0f},
		.tint = {1.0f, 1.0f, 1.0f},
		.shininess = 48.0f,
		.wave = true,
	});
	objects.push_back({
		.mesh = &floor_mesh,
		.material_index = 1,
		.pos = {0.0f, pyramide.y_base - 0.01f, 0.0f},
		.rot = {0.0f, 0.0f, 0.0f},
		.scale = {1.0f, 1.0f, 1.0f},
		.tint = {1.0f, 1.0f, 1.0f},
		.shininess = 16.0f,
		.wave = false,
	});

	point_lights.push_back({
		glm::vec4(2.0f, 2.5f, 1.0f, 1.0f),
		glm::vec4(1.0f, 0.4f, 0.2f, 2.0f),
	});
	spot_lights.push_back({
		glm::vec4(-2.0f, 3.0f, 2.0f, 1.0f),
		glm::vec4(0.3f, -1.0f, -0.4f, 0.0f),
		glm::vec4(0.3f, 0.7f, 1.0f, 3.0f),
		glm::vec4(std::cos(glm::radians(15.0f)), std::cos(glm::radians(25.0f)), 0.0f, 0.0f),
	});
	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipeline(context.device, unlit_pipeline, nullptr);
	vkDestroyPipeline(context.device, unlit_line_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, unlit_layout, nullptr);
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);
	vkDestroyShaderModule(context.device, unlit_vert, nullptr);
	vkDestroyShaderModule(context.device, unlit_frag, nullptr);

	destroy_mesh(pyramid_mesh);
	destroy_mesh(floor_mesh);
	destroy_mesh(cube_mesh);
	destroy_mesh(cone_mesh);

	for (auto& mat : materials) {
		if (mat.albedo.image != black_texture.image) destroy_texture(mat.albedo);
		if (mat.specular.image != black_texture.image) destroy_texture(mat.specular);
		if (mat.emissive.image != black_texture.image) destroy_texture(mat.emissive);
	}
	materials.clear();
	destroy_texture(black_texture);

	vmaDestroyBuffer(context.allocator, scene_ubo, scene_ubo_alloc);
	vmaDestroyBuffer(context.allocator, model_ubo, model_ubo_alloc);
	vmaDestroyBuffer(context.allocator, point_ssbo, point_ssbo_alloc);
	vmaDestroyBuffer(context.allocator, spot_ssbo, spot_ssbo_alloc);

	vkDestroySampler(context.device, texture_sampler, nullptr);
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, material_set_layout, nullptr);
	vkDestroyCommandPool(context.device, upload_pool, nullptr);
}

void update(double time) {
	app_time = time;
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 560.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Lab 3")) {
		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Cam pos", cam_pos, 0.05f);
			ImGui::SliderFloat("Yaw", &cam_yaw, -3.14159f, 3.14159f);
			ImGui::SliderFloat("Pitch", &cam_pitch, -1.5f, 1.5f);
			ImGui::SliderFloat("FOV", &cam_fov, 20.0f, 100.0f);
		}
		if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (int i = 0; i < int(objects.size()); ++i) {
				ImGui::PushID(i);
				ImGui::Text("Object %d", i);
				ImGui::DragFloat3("Pos", objects[i].pos, 0.01f);
				ImGui::DragFloat3("Rot", objects[i].rot, 0.01f);
				ImGui::DragFloat3("Scale", objects[i].scale, 0.01f, 0.01f, 10.0f);
				ImGui::ColorEdit3("Tint", objects[i].tint);
				ImGui::SliderFloat("Shininess", &objects[i].shininess, 1.0f, 128.0f);
				std::vector<const char*> names;
				names.reserve(materials.size());
				for (const auto& m : materials) names.push_back(m.name.c_str());
				ImGui::Combo("Texture", &objects[i].material_index, names.data(), int(names.size()));
				ImGui::Checkbox("Wave sampling", &objects[i].wave);
				ImGui::Separator();
				ImGui::PopID();
			}
			ImGui::SliderFloat("Wave strength", &wave_strength, 0.0f, 0.1f);
			ImGui::SliderFloat("Wave freq", &wave_freq, 1.0f, 30.0f);
		}
		if (ImGui::CollapsingHeader("Ambient / Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Ambient", ambient);
			ImGui::DragFloat3("Dir direction", dir_direction, 0.01f);
			ImGui::ColorEdit3("Dir color", dir_color);
			ImGui::SliderFloat("Dir intensity", &dir_intensity, 0.0f, 5.0f);
		}
		if (ImGui::CollapsingHeader("Point lights")) {
			if (ImGui::Button("Add point") && point_lights.size() < kMaxPointLights) {
				point_lights.push_back({glm::vec4(0, 2, 0, 1), glm::vec4(1, 1, 1, 1.5f)});
			}
			for (int i = 0; i < int(point_lights.size()); ++i) {
				ImGui::PushID(100 + i);
				ImGui::DragFloat3("Pos", &point_lights[i].position.x, 0.05f);
				ImGui::ColorEdit3("Color", &point_lights[i].color.x);
				ImGui::SliderFloat("Intensity", &point_lights[i].color.w, 0.0f, 10.0f);
				if (ImGui::Button("Remove")) {
					point_lights.erase(point_lights.begin() + i);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
		}
		if (ImGui::CollapsingHeader("Spot lights")) {
			if (ImGui::Button("Add spot") && spot_lights.size() < kMaxSpotLights) {
				spot_lights.push_back({
					glm::vec4(0, 3, 0, 1), glm::vec4(0, -1, 0, 0), glm::vec4(1, 1, 0.6f, 2),
					glm::vec4(std::cos(glm::radians(12.0f)), std::cos(glm::radians(20.0f)), 0, 0),
				});
			}
			for (int i = 0; i < int(spot_lights.size()); ++i) {
				ImGui::PushID(200 + i);
				ImGui::DragFloat3("Pos", &spot_lights[i].position.x, 0.05f);
				ImGui::DragFloat3("Dir", &spot_lights[i].direction.x, 0.01f);
				ImGui::ColorEdit3("Color", &spot_lights[i].color.x);
				ImGui::SliderFloat("Intensity", &spot_lights[i].color.w, 0.0f, 10.0f);
				float inner_deg = glm::degrees(std::acos(std::clamp(spot_lights[i].cone.x, -1.0f, 1.0f)));
				float outer_deg = glm::degrees(std::acos(std::clamp(spot_lights[i].cone.y, -1.0f, 1.0f)));
				if (ImGui::SliderFloat("Inner deg", &inner_deg, 1.0f, 60.0f)) {
					spot_lights[i].cone.x = std::cos(glm::radians(inner_deg));
				}
				if (ImGui::SliderFloat("Outer deg", &outer_deg, 1.0f, 80.0f)) {
					spot_lights[i].cone.y = std::cos(glm::radians(outer_deg));
				}
				if (spot_lights[i].cone.x < spot_lights[i].cone.y) spot_lights[i].cone.y = spot_lights[i].cone.x;
				if (ImGui::Button("Remove")) {
					spot_lights.erase(spot_lights.begin() + i);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
		}
		ImGui::Checkbox("Show light gizmos", &show_light_viz);
	}
	ImGui::End();

	ImGuiIO& io = ImGui::GetIO();
	if (!io.WantCaptureMouse && io.MouseDown[0]) {
		cam_yaw += io.MouseDelta.x * 0.005f;
		cam_pitch -= io.MouseDelta.y * 0.005f;
		cam_pitch = std::clamp(cam_pitch, -1.5f, 1.5f);
	}
	if (!io.WantCaptureKeyboard) {
		const glm::vec3 forward = camera_forward();
		const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
		const float speed = (io.KeyShift ? 8.0f : 3.0f) * io.DeltaTime;
		glm::vec3 move(0.0f);
		if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
		if (glm::length(move) > 0.0f) {
			move = glm::normalize(move) * speed;
			cam_pos[0] += move.x;
			cam_pos[1] += move.y;
			cam_pos[2] += move.z;
		}
	}
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	vkResetCommandBuffer(fd.command_buffer, 0); // Сброс буфера комманд
	const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(fd.command_buffer, &begin);

	const VkClearValue clears[2]{
		{.color = {{0.03f, 0.03f, 0.05f, 1.0f}}},
		{.depthStencil = {1.0f, 0}},
	};
	const VkRenderPassBeginInfo pass{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = {{0, 0}, context.swapchain_extent},
		.clearValueCount = 2,
		.pClearValues = clears,
	};
	vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport{
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	const VkRect2D scissor{{0, 0}, context.swapchain_extent};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(cam_fov), aspect, 0.1f, 100.0f);
	projection[1][1] *= -1.0f;
	const glm::mat4 view = camera_view();
	const glm::mat4 view_proj = projection * view;

	SceneUniform scene{};
	scene.view_proj = view_proj;
	scene.camera_pos = glm::vec4(cam_pos[0], cam_pos[1], cam_pos[2], 1.0f);
	scene.ambient = glm::vec4(ambient[0], ambient[1], ambient[2], 1.0f);
	scene.dir_direction = glm::vec4(glm::normalize(glm::vec3(dir_direction[0], dir_direction[1], dir_direction[2])), 0.0f);
	scene.dir_color = glm::vec4(dir_color[0], dir_color[1], dir_color[2], dir_intensity);
	scene.light_counts = glm::vec4(float(point_lights.size()), float(spot_lights.size()), 0.0f, 0.0f);
	std::memcpy(scene_ubo_mapped, &scene, sizeof(scene));
	if (!point_lights.empty()) {
		std::memcpy(point_ssbo_mapped, point_lights.data(), point_lights.size() * sizeof(PointLightCPU));
	}
	if (!spot_lights.empty()) {
		std::memcpy(spot_ssbo_mapped, spot_lights.data(), spot_lights.size() * sizeof(SpotLightCPU));
	}

	for (int i = 0; i < int(objects.size()) && i < kMaxModels; ++i) {
		ModelUniform u{};
		u.model = make_model_matrix(objects[i].pos, objects[i].rot, objects[i].scale);
		u.normal_matrix = glm::transpose(glm::inverse(u.model));
		u.tint = glm::vec4(objects[i].tint[0], objects[i].tint[1], objects[i].tint[2], objects[i].shininess);
		std::memcpy(static_cast<char*>(model_ubo_mapped) + i * model_ubo_align, &u, sizeof(u));
	}

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &scene_set, 0, nullptr);

	for (int i = 0; i < int(objects.size()) && i < kMaxModels; ++i) {
		auto& obj = objects[i];
		const int mat = std::clamp(obj.material_index, 0, int(materials.size()) - 1);
		const uint32_t dyn_offset = uint32_t(i * model_ubo_align);
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        1, 1, &model_set, 1, &dyn_offset);
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
		                        2, 1, &materials[mat].set, 0, nullptr);

		WavePush push{
			.time = float(app_time),
			.wave_strength = wave_strength,
			.wave_freq = wave_freq,
			.wave_enabled = obj.wave ? 1.0f : 0.0f,
		};
		vkCmdPushConstants(fd.command_buffer, pipeline_layout,
		                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0, sizeof(push), &push);

		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &obj.mesh->vertex_buffer, &offset);
		vkCmdBindIndexBuffer(fd.command_buffer, obj.mesh->index_buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(fd.command_buffer, obj.mesh->index_count, 1, 0, 0, 0);
	}

	if (show_light_viz) {
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &cube_mesh.vertex_buffer, &offset);
		vkCmdBindIndexBuffer(fd.command_buffer, cube_mesh.index_buffer, 0, VK_INDEX_TYPE_UINT16);
		for (const auto& pl : point_lights) {
			glm::mat4 m(1.0f);
			m = glm::translate(m, glm::vec3(pl.position));
			UnlitPush push{view_proj * m, glm::vec4(pl.color.x, pl.color.y, pl.color.z, 1.0f)};
			vkCmdPushConstants(fd.command_buffer, unlit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(fd.command_buffer, cube_mesh.index_count, 1, 0, 0, 0);
		}
		for (const auto& sl : spot_lights) {
			glm::mat4 m(1.0f);
			m = glm::translate(m, glm::vec3(sl.position));
			UnlitPush push{view_proj * m, glm::vec4(sl.color.x, sl.color.y, sl.color.z, 1.0f)};
			vkCmdPushConstants(fd.command_buffer, unlit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(fd.command_buffer, cube_mesh.index_count, 1, 0, 0, 0);
		}

		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_line_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &cone_mesh.vertex_buffer, &offset);
		vkCmdBindIndexBuffer(fd.command_buffer, cone_mesh.index_buffer, 0, VK_INDEX_TYPE_UINT16);
		for (const auto& sl : spot_lights) {
			const glm::vec3 dir = glm::normalize(glm::vec3(sl.direction));
			glm::vec3 up(0.0f, 1.0f, 0.0f);
			if (std::abs(glm::dot(dir, up)) > 0.99f) up = glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 right = glm::normalize(glm::cross(up, dir));
			up = glm::normalize(glm::cross(dir, right));
			const float outer = std::acos(std::clamp(sl.cone.y, -1.0f, 1.0f));
			const float len = 1.5f;
			const float radius = std::tan(outer) * len;
			glm::mat4 basis(1.0f);
			basis[0] = glm::vec4(right * radius, 0.0f);
			basis[1] = glm::vec4(up * radius, 0.0f);
			basis[2] = glm::vec4(dir * len, 0.0f);
			basis[3] = glm::vec4(glm::vec3(sl.position), 1.0f);
			UnlitPush push{view_proj * basis, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)};
			vkCmdPushConstants(fd.command_buffer, unlit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(fd.command_buffer, cone_mesh.index_count, 1, 0, 0, 0);
		}
	}

	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
