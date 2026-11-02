#include "application.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

VkPipeline pipeline = VK_NULL_HANDLE; // Превращает вершины в пиксели
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE; // Передача данных в Шейдеры pipeline
VkShaderModule vertex_shader = VK_NULL_HANDLE; // Код из файла pyramide.vert.csv
VkShaderModule fragment_shader = VK_NULL_HANDLE; // Код из файла pyramide.frag.csv
VkBuffer vertex_buffer = VK_NULL_HANDLE; // Вершины пирамиды
VmaAllocation vertex_allocation = VK_NULL_HANDLE; // Область памяти с вершинами пирамиды
VkBuffer index_buffer = VK_NULL_HANDLE; // Индексация вершин пирамиды
VmaAllocation index_allocation = VK_NULL_HANDLE; // Область памяти с индексацией вершин

VkBuffer floor_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation floor_vertex_allocation = VK_NULL_HANDLE;
VkBuffer floor_index_buffer = VK_NULL_HANDLE;
VmaAllocation floor_index_allocation = VK_NULL_HANDLE;
uint32_t floor_index_count = 0;

VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
VkDescriptorSetLayout model_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet scene_set = VK_NULL_HANDLE;
VkDescriptorSet model_sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

VkBuffer scene_ubo = VK_NULL_HANDLE;
VmaAllocation scene_ubo_alloc = VK_NULL_HANDLE;
void* scene_ubo_mapped = nullptr;

VkBuffer model_ubos[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
VmaAllocation model_ubo_allocs[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
void* model_ubo_mapped[2] = {nullptr, nullptr};

VkBuffer point_ssbo = VK_NULL_HANDLE;
VmaAllocation point_ssbo_alloc = VK_NULL_HANDLE;
void* point_ssbo_mapped = nullptr;

VkBuffer spot_ssbo = VK_NULL_HANDLE;
VmaAllocation spot_ssbo_alloc = VK_NULL_HANDLE;
void* spot_ssbo_mapped = nullptr;

VkPipeline unlit_pipeline = VK_NULL_HANDLE;
VkPipeline unlit_line_pipeline = VK_NULL_HANDLE;
VkPipelineLayout unlit_layout = VK_NULL_HANDLE;
VkShaderModule unlit_vert = VK_NULL_HANDLE;
VkShaderModule unlit_frag = VK_NULL_HANDLE;
VkBuffer cube_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation cube_vertex_allocation = VK_NULL_HANDLE;
VkBuffer cube_index_buffer = VK_NULL_HANDLE;
VmaAllocation cube_index_allocation = VK_NULL_HANDLE;
uint32_t cube_index_count = 0;
VkBuffer cone_vertex_buffer = VK_NULL_HANDLE;
VmaAllocation cone_vertex_allocation = VK_NULL_HANDLE;
uint32_t cone_vertex_count = 0;

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
	alignas(16) glm::vec4 albedo;
	alignas(16) glm::vec4 specular;
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

float cam_pos[3] = {0.0f, 2.0f, 6.0f};
float cam_yaw = -1.5707963f;
float cam_pitch = -0.35f;
float cam_fov = 60.0f;

float model_pos[3] = {0.0f, 0.0f, 0.0f};
float model_rot[3] = {0.0f, 0.0f, 0.0f};
float model_scale[3] = {1.0f, 1.0f, 1.0f};
float albedo[3] = {0.2f, 0.45f, 0.95f};
float specular_color[3] = {1.0f, 1.0f, 1.0f};
float shininess = 32.0f;

float ambient[3] = {0.08f, 0.08f, 0.10f};
float dir_direction[3] = {-0.4f, -1.0f, -0.3f};
float dir_color[3] = {1.0f, 0.95f, 0.85f};
float dir_intensity = 1.0f;

std::vector<PointLightCPU> point_lights;
std::vector<SpotLightCPU> spot_lights;
bool show_light_viz = true;

struct Pyramide { // Структура пирамиды
	struct Vertex { // Структура Вершины
		glm::vec3 position;
		glm::vec3 normal;
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
			vertices.push_back({a, n});
			vertices.push_back({b, n});
			vertices.push_back({c, n});
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

bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                   VkBuffer& buffer, VmaAllocation& allocation, void** mapped_out = nullptr) {
	// size -- размер буфера с типом для размера объектов в Vulkan
	// usage -- флаги цели буфера
	// data -- данные, куда попадёт буфер
	// buffer -- Идентификатор, по которому драйвер видеокарты может идентифицировать буфер
	// allocation -- Метка блока памяти
	auto& context = graphics::internal::context; // Объекты для работы с Vulkan

	const VkBufferCreateInfo info{ // Описание буфера, который будет передан видеокарте
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // Метка самой структуры
		.size = size,
		.usage = usage,
	};
	const VmaAllocationCreateInfo alloc{ // Описание памяти, которая попадёт в видеокарту
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT // Флаг того, что процессор пишет байты один раз от первого до последнего
		       | VMA_ALLOCATION_CREATE_MAPPED_BIT, // Сразу выдаёт указатель на память
		.usage = VMA_MEMORY_USAGE_AUTO, // Делегирует подбор памяти библиотеки
	};
	VmaAllocationInfo mapped{}; // Сведения о выданном участке памяти.
	if (vmaCreateBuffer(context.allocator, &info, &alloc, &buffer, &allocation, &mapped) != VK_SUCCESS) return false;

	if (data != nullptr) {
		std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
	}
	if (mapped_out != nullptr) {
		*mapped_out = mapped.pMappedData;
	}
	return true;
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

bool create_mesh_buffers() {
	struct Vn {
		glm::vec3 p;
		glm::vec3 n;
	};
	const float s = 8.0f;
	const Vn floor_verts[] = {
		{{-s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}},
		{{ s, 0.0f, -s}, {0.0f, 1.0f, 0.0f}},
		{{ s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}},
		{{-s, 0.0f,  s}, {0.0f, 1.0f, 0.0f}},
	};
	const uint16_t floor_idx[] = {0, 1, 2, 0, 2, 3};
	floor_index_count = 6;
	if (!create_buffer(sizeof(floor_verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, floor_verts,
	                   floor_vertex_buffer, floor_vertex_allocation)) return false;
	if (!create_buffer(sizeof(floor_idx), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, floor_idx,
	                   floor_index_buffer, floor_index_allocation)) return false;

	const float hs = 0.12f;
	const glm::vec3 cube_pos[] = {
		{-hs,-hs,-hs},{ hs,-hs,-hs},{ hs, hs,-hs},{-hs, hs,-hs},
		{-hs,-hs, hs},{ hs,-hs, hs},{ hs, hs, hs},{-hs, hs, hs},
	};
	const uint16_t cube_idx[] = {
		0,1,2, 0,2,3, 4,6,5, 4,7,6,
		0,4,5, 0,5,1, 2,6,7, 2,7,3,
		0,3,7, 0,7,4, 1,5,6, 1,6,2,
	};
	cube_index_count = static_cast<uint32_t>(sizeof(cube_idx) / sizeof(cube_idx[0]));
	if (!create_buffer(sizeof(cube_pos), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cube_pos,
	                   cube_vertex_buffer, cube_vertex_allocation)) return false;
	if (!create_buffer(sizeof(cube_idx), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, cube_idx,
	                   cube_index_buffer, cube_index_allocation)) return false;

	std::vector<glm::vec3> cone;
	const int segments = 24;
	const float len = 1.0f;
	const float radius = 0.5f;
	cone.push_back({0.0f, 0.0f, 0.0f});
	cone.push_back({0.0f, 0.0f, len});
	for (int i = 0; i < segments; ++i) {
		const float a0 = float(i) / float(segments) * 6.2831853f;
		const float a1 = float(i + 1) / float(segments) * 6.2831853f;
		const glm::vec3 p0{radius * std::cos(a0), radius * std::sin(a0), len};
		const glm::vec3 p1{radius * std::cos(a1), radius * std::sin(a1), len};
		cone.push_back({0.0f, 0.0f, 0.0f});
		cone.push_back(p0);
		cone.push_back(p0);
		cone.push_back(p1);
	}
	cone_vertex_count = static_cast<uint32_t>(cone.size());
	return create_buffer(cone.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cone.data(),
	                     cone_vertex_buffer, cone_vertex_allocation);
}

bool create_lit_pipeline() {
	auto& context = graphics::internal::context;

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
	if (vkCreateDescriptorSetLayout(context.device, &scene_layout_info, nullptr, &scene_set_layout) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorSetLayoutBinding model_binding{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo model_layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &model_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &model_layout_info, nullptr, &model_set_layout) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorPoolSize pool_sizes[] = {
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 8},
		{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4},
	};
	const VkDescriptorPoolCreateInfo pool_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 8,
		.poolSizeCount = 2,
		.pPoolSizes = pool_sizes,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		return false;
	}

	{
		const VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &scene_set_layout,
		};
		if (vkAllocateDescriptorSets(context.device, &alloc_info, &scene_set) != VK_SUCCESS) return false;
	}
	{
		VkDescriptorSetLayout layouts[2] = {model_set_layout, model_set_layout};
		const VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 2,
			.pSetLayouts = layouts,
		};
		if (vkAllocateDescriptorSets(context.device, &alloc_info, model_sets) != VK_SUCCESS) return false;
	}

	if (!create_buffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
	                   scene_ubo, scene_ubo_alloc, &scene_ubo_mapped)) return false;
	for (int i = 0; i < 2; ++i) {
		if (!create_buffer(sizeof(ModelUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
		                   model_ubos[i], model_ubo_allocs[i], &model_ubo_mapped[i])) return false;
	}
	if (!create_buffer(sizeof(PointLightCPU) * kMaxPointLights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr,
	                   point_ssbo, point_ssbo_alloc, &point_ssbo_mapped)) return false;
	if (!create_buffer(sizeof(SpotLightCPU) * kMaxSpotLights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr,
	                   spot_ssbo, spot_ssbo_alloc, &spot_ssbo_mapped)) return false;

	const VkDescriptorBufferInfo scene_info{.buffer = scene_ubo, .offset = 0, .range = sizeof(SceneUniform)};
	const VkDescriptorBufferInfo point_info{.buffer = point_ssbo, .offset = 0, .range = VK_WHOLE_SIZE};
	const VkDescriptorBufferInfo spot_info{.buffer = spot_ssbo, .offset = 0, .range = VK_WHOLE_SIZE};
	const VkDescriptorBufferInfo model_infos[2] = {
		{.buffer = model_ubos[0], .offset = 0, .range = sizeof(ModelUniform)},
		{.buffer = model_ubos[1], .offset = 0, .range = sizeof(ModelUniform)},
	};

	VkWriteDescriptorSet writes[5]{};
	writes[0] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = scene_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &scene_info,
	};
	writes[1] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = scene_set,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &point_info,
	};
	writes[2] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = scene_set,
		.dstBinding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &spot_info,
	};
	writes[3] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = model_sets[0],
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &model_infos[0],
	};
	writes[4] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = model_sets[1],
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &model_infos[1],
	};
	vkUpdateDescriptorSets(context.device, 5, writes, 0, nullptr);

	VkDescriptorSetLayout pipe_layouts[2] = {scene_set_layout, model_set_layout};
	const VkPipelineLayoutCreateInfo layout_info{ // Описание pipeline_layout
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = pipe_layouts,
	};
	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) return false;

	const VkPipelineShaderStageCreateInfo stages[]{ // Описание начала pipeline
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription binding{ // Описание шага по буферу
		.binding = 0,
		.stride = sizeof(Pyramide::Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attributes[]{ // Описание попадания байтов шага в шейдер
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Pyramide::Vertex, position)},
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Pyramide::Vertex, normal)},
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = 2,
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
	const VkDynamicState dynamic_states[]{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
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
		.binding = 0,
		.stride = sizeof(glm::vec3),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};
	const VkVertexInputAttributeDescription attribute{
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = 0,
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
	const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	const VkPipelineDynamicStateCreateInfo dynamic{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};
	VkGraphicsPipelineCreateInfo info{
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
		.layout = unlit_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &unlit_pipeline) != VK_SUCCESS) {
		return false;
	}

	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	return vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &unlit_line_pipeline) == VK_SUCCESS;
}

bool initialize() {
	const auto vertex_code = read_shader("pyramide.vert.spv");
	const auto fragment_code = read_shader("pyramide.frag.spv");
	const auto unlit_v_code = read_shader("unlit.vert.spv");
	const auto unlit_f_code = read_shader("unlit.frag.spv");
	if (vertex_code.empty() || fragment_code.empty() || unlit_v_code.empty() || unlit_f_code.empty()) return false;

	if (!create_shader_module(vertex_code, vertex_shader)) return false;
	if (!create_shader_module(fragment_code, fragment_shader)) return false;
	if (!create_shader_module(unlit_v_code, unlit_vert)) return false;
	if (!create_shader_module(unlit_f_code, unlit_frag)) return false;

	if (!create_buffer(pyramide.vertices.size() * sizeof(Pyramide::Vertex),
	                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                   pyramide.vertices.data(),
	                   vertex_buffer, vertex_allocation)) return false;
	if (!create_buffer(pyramide.indices.size() * sizeof(std::uint16_t),
	                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                   pyramide.indices.data(),
	                   index_buffer, index_allocation)) return false;

	if (!create_mesh_buffers()) return false;
	if (!create_lit_pipeline()) return false;
	if (!create_unlit_pipelines()) return false;

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

	vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_allocation);
	vmaDestroyBuffer(context.allocator, index_buffer, index_allocation);
	vmaDestroyBuffer(context.allocator, floor_vertex_buffer, floor_vertex_allocation);
	vmaDestroyBuffer(context.allocator, floor_index_buffer, floor_index_allocation);
	vmaDestroyBuffer(context.allocator, cube_vertex_buffer, cube_vertex_allocation);
	vmaDestroyBuffer(context.allocator, cube_index_buffer, cube_index_allocation);
	vmaDestroyBuffer(context.allocator, cone_vertex_buffer, cone_vertex_allocation);
	vmaDestroyBuffer(context.allocator, scene_ubo, scene_ubo_alloc);
	for (int i = 0; i < 2; ++i) {
		vmaDestroyBuffer(context.allocator, model_ubos[i], model_ubo_allocs[i]);
	}
	vmaDestroyBuffer(context.allocator, point_ssbo, point_ssbo_alloc);
	vmaDestroyBuffer(context.allocator, spot_ssbo, spot_ssbo_alloc);

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, model_set_layout, nullptr);
}

void update(double) {
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Lab 2")) {
		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Cam pos", cam_pos, 0.05f);
			ImGui::SliderFloat("Yaw", &cam_yaw, -3.14159f, 3.14159f);
			ImGui::SliderFloat("Pitch", &cam_pitch, -1.5f, 1.5f);
			ImGui::SliderFloat("FOV", &cam_fov, 20.0f, 100.0f);
		}
		if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("Position", model_pos, 0.01f);
			ImGui::DragFloat3("Rotation", model_rot, 0.01f);
			ImGui::DragFloat3("Scale", model_scale, 0.01f, 0.01f, 10.0f);
			ImGui::ColorEdit3("Albedo", albedo);
			ImGui::ColorEdit3("Specular", specular_color);
			ImGui::SliderFloat("Shininess", &shininess, 1.0f, 128.0f);
		}
		if (ImGui::CollapsingHeader("Ambient / Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Ambient", ambient);
			ImGui::DragFloat3("Dir direction", dir_direction, 0.01f);
			ImGui::ColorEdit3("Dir color", dir_color);
			ImGui::SliderFloat("Dir intensity", &dir_intensity, 0.0f, 5.0f);
		}
		if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Button("Add point") && point_lights.size() < kMaxPointLights) {
				point_lights.push_back({
					glm::vec4(0.0f, 2.0f, 0.0f, 1.0f),
					glm::vec4(1.0f, 1.0f, 1.0f, 1.5f),
				});
			}
			for (int i = 0; i < static_cast<int>(point_lights.size()); ++i) {
				ImGui::PushID(i + 100);
				ImGui::Text("Point %d", i);
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
		if (ImGui::CollapsingHeader("Spot lights", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (ImGui::Button("Add spot") && spot_lights.size() < kMaxSpotLights) {
				spot_lights.push_back({
					glm::vec4(0.0f, 3.0f, 0.0f, 1.0f),
					glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),
					glm::vec4(1.0f, 1.0f, 0.6f, 2.0f),
					glm::vec4(std::cos(glm::radians(12.0f)), std::cos(glm::radians(20.0f)), 0.0f, 0.0f),
				});
			}
			for (int i = 0; i < static_cast<int>(spot_lights.size()); ++i) {
				ImGui::PushID(i + 200);
				ImGui::Text("Spot %d", i);
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
				if (spot_lights[i].cone.x < spot_lights[i].cone.y) {
					spot_lights[i].cone.y = spot_lights[i].cone.x;
				}
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

void draw_mesh(VkCommandBuffer cmd, VkBuffer vbo, VkBuffer ibo, uint32_t index_count) {
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);
	vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void upload_model(int slot, const glm::mat4& model, const glm::vec4& alb, const glm::vec4& spec) {
	ModelUniform u{};
	u.model = model;
	u.normal_matrix = glm::transpose(glm::inverse(model));
	u.albedo = alb;
	u.specular = spec;
	std::memcpy(model_ubo_mapped[slot], &u, sizeof(u));
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	vkResetCommandBuffer(fd.command_buffer, 0); // Сброс буфера комманд
	const VkCommandBufferBeginInfo begin{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
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

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &scene_set, 0, nullptr);

	upload_model(0, make_model_matrix(model_pos, model_rot, model_scale),
	             glm::vec4(albedo[0], albedo[1], albedo[2], 1.0f),
	             glm::vec4(specular_color[0], specular_color[1], specular_color[2], shininess));
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &model_sets[0], 0, nullptr);
	draw_mesh(fd.command_buffer, vertex_buffer, index_buffer, static_cast<uint32_t>(pyramide.indices.size()));

	float floor_pos[3] = {0.0f, pyramide.y_base - 0.01f, 0.0f};
	float floor_rot[3] = {0.0f, 0.0f, 0.0f};
	float floor_scl[3] = {1.0f, 1.0f, 1.0f};
	upload_model(1, make_model_matrix(floor_pos, floor_rot, floor_scl),
	             glm::vec4(0.35f, 0.35f, 0.38f, 1.0f),
	             glm::vec4(0.2f, 0.2f, 0.2f, 16.0f));
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, &model_sets[1], 0, nullptr);
	draw_mesh(fd.command_buffer, floor_vertex_buffer, floor_index_buffer, floor_index_count);

	if (show_light_viz) {
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &cube_vertex_buffer, &offset);
		vkCmdBindIndexBuffer(fd.command_buffer, cube_index_buffer, 0, VK_INDEX_TYPE_UINT16);

		for (const auto& pl : point_lights) {
			glm::mat4 m(1.0f);
			m = glm::translate(m, glm::vec3(pl.position));
			UnlitPush push{view_proj * m, glm::vec4(pl.color.x, pl.color.y, pl.color.z, 1.0f)};
			vkCmdPushConstants(fd.command_buffer, unlit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(fd.command_buffer, cube_index_count, 1, 0, 0, 0);
		}
		for (const auto& sl : spot_lights) {
			glm::mat4 m(1.0f);
			m = glm::translate(m, glm::vec3(sl.position));
			UnlitPush push{view_proj * m, glm::vec4(sl.color.x, sl.color.y, sl.color.z, 1.0f)};
			vkCmdPushConstants(fd.command_buffer, unlit_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(fd.command_buffer, cube_index_count, 1, 0, 0, 0);
		}

		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, unlit_line_pipeline);
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &cone_vertex_buffer, &offset);
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
			vkCmdDraw(fd.command_buffer, cone_vertex_count, 1, 0, 0);
		}
	}

	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
