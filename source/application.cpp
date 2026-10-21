#include "application.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>


#include <imgui.h>

namespace application {

VkPipeline pipeline = VK_NULL_HANDLE; // Превращает вершины в пиксели
VkPipelineLayout pipeline_layout = VK_NULL_HANDLE; // Передача данных в Шейдеры pipeline
VkShaderModule vertex_shader = VK_NULL_HANDLE; // Код из файла pyramide.vert.csv
VkShaderModule fragment_shader = VK_NULL_HANDLE; // Код из файла pyramide.frag.csv
VkBuffer vertex_buffer = VK_NULL_HANDLE; // Вершины пирамиды
VmaAllocation vertex_allocation = VK_NULL_HANDLE; // Область памяти с вершинами пирамиды
VkBuffer index_buffer = VK_NULL_HANDLE; // Индексация вершин пирамиды
VmaAllocation index_allocation = VK_NULL_HANDLE; // Область памяти с индексацией вершин

VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
VkDescriptorSet descriptor_sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
VkBuffer uniform_buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
VmaAllocation uniform_allocations[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
void* uniform_mapped[2] = {nullptr, nullptr};

struct ObjectUniform {
	alignas(16) glm::mat4 mvp;
	alignas(16) glm::vec4 color;
};

bool use_ortho = false;
float position[3] = {0.0f, 0.0f, 0.0f};
float rotation[3] = {0.0f, 0.0f, 0.0f};
float scale_xyz[3] = {1.0f, 1.0f, 1.0f};
float ui_color[3] = {1.0f, 1.0f, 1.0f};
bool anim_playing = false;
float anim_speed = 1.0f;
float anim_radius = 1.5f;
float anim_time = 0.0f;
double last_time = 0.0;

struct Pyramide { // Структура пирамиды
	struct Vertex { // Структура Вершины
		glm::vec3 position; // Начальная позиция пирамиды
		glm::vec3 color; // Цвет пирамиды
	};

	glm::vec3 base_color{0.0f, 0.0f, 1.0f}; 
	const float edge = 2.0f; // Длина ребра
    float radius = 0.0f; // Радиус окружности куда вписано основание
    float height = 0.0f; // Высота Пирамиды
	float y_base = 0.0f; // Высота по основанию Y в декартовых координатах
	float y_apex = 0.0f; // Углы треугольников пирамиды (все они равны 60 градусов)
	std::vector<Vertex> vertices; // Сами вершины
	std::vector<std::uint16_t> indices{ // Правила индексации
		1, 2, 3,
		0, 2, 1,
		0, 3, 2,
		0, 1, 3,
	};

	Pyramide() { // делаем пирамиду
		radius = edge / std::sqrt(3.0f);
		height = std::sqrt(6.0f) / 3.0f * edge;
		y_base = -height / 4.0f;
		y_apex = 3.0f * height / 4.0f;
		const glm::vec3 positions[4] = {
			{0.0f, y_apex, 0.0f},
			{radius, y_base, 0.0f},
			{-0.5f * radius, y_base, 0.5f * radius * std::sqrt(3.0f)},
			{-0.5f * radius, y_base, -0.5f * radius * std::sqrt(3.0f)},
		};
		vertices = {
			{positions[0], glm::abs(glm::normalize(positions[0] + glm::vec3(0.001f)))},
			{positions[1], glm::abs(glm::normalize(positions[1] + glm::vec3(0.001f)))},
			{positions[2], glm::abs(glm::normalize(positions[2] + glm::vec3(0.001f)))},
			{positions[3], glm::abs(glm::normalize(positions[3] + glm::vec3(0.001f)))},
		};
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
	if (vmaCreateBuffer(context.allocator, &info, &alloc, &buffer, &allocation, &mapped) != VK_SUCCESS)	return false; // Выделяет память и создаёт буфер для этой памяти
	
	if (data != nullptr) {
		std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size)); //Копирование память м буфера в data
	}
	if (mapped_out != nullptr) {
		*mapped_out = mapped.pMappedData;
	}
	return true;
}

std::vector<unsigned char> read_file(const std::string& path){
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) return {};
	const auto size = file.tellg();
	std::vector<unsigned char> code(static_cast<size_t>(size));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(code.data()), size);
	return code;
}

std::vector<unsigned char> read_shader(const char* name) {
	std::string a = std::string("shaders/") + name;
	auto code = read_file(a);
	if (!code.empty()) return code;
	a = std::string("../shaders/") + name;
	return read_file(a);
}

glm::mat4 make_model(float pos[3], float rot[3], float scl[3], float phase) {
	glm::mat4 m(1.0f);
	float ax = pos[0];
	float ay = pos[1];
	float az = pos[2];
	if (anim_playing) {
		ax += anim_radius * std::cos(anim_time * anim_speed + phase);
		az += anim_radius * std::sin(anim_time * anim_speed + phase);
	} else if (phase != 0.0f) {
		ax += anim_radius;
	}
	m = glm::translate(m, glm::vec3(ax, ay, az));
	float yaw = rot[1];
	if (anim_playing) {
		yaw += anim_time * anim_speed;
	}
	m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
	m = glm::rotate(m, rot[0], glm::vec3(1.0f, 0.0f, 0.0f));
	m = glm::rotate(m, rot[2], glm::vec3(0.0f, 0.0f, 1.0f));
	m = glm::scale(m, glm::vec3(scl[0], scl[1], scl[2]));
	return m;
}

auto operator_matrix = glm::mat4(1.0f);
bool initialize() {
    const auto vertex_code = read_shader("pyramide.vert.spv"); // Файл для позиции вершин
    const auto fragment_code = read_shader("pyramide.frag.spv"); // Файл для цвета вершин
	if (vertex_code.empty() or fragment_code.empty()) return false;
	auto& context = graphics::internal::context;

	VkShaderModuleCreateInfo module_info{ // Структура для создания модуля шейдеров
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, // Идентификатор структуры
	    .codeSize = vertex_code.size(), // Размер будущего модуля
	    .pCode = reinterpret_cast<const std::uint32_t*>(vertex_code.data()) // Код модуля
    };

	if (vkCreateShaderModule(context.device, &module_info, nullptr, &vertex_shader) != VK_SUCCESS) return false;
    // Создание шейдерного модуля 
	module_info.codeSize = fragment_code.size();
	module_info.pCode = reinterpret_cast<const std::uint32_t*>(fragment_code.data());
	if (vkCreateShaderModule(context.device, &module_info, nullptr, &fragment_shader) != VK_SUCCESS) return false;

	if (!create_buffer(pyramide.vertices.size() * sizeof(Pyramide::Vertex),
	                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, // Идентификатор буфера вершин
	                   pyramide.vertices.data(),
	                   vertex_buffer, vertex_allocation)) return false; // Создание буфера вершин
	
	if (!create_buffer(pyramide.indices.size() * sizeof(std::uint16_t),
	                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, // Флаг буфера индексов
	                   pyramide.indices.data(),
	                   index_buffer, index_allocation)) return false; // Создание буфера индексов

	for (int i = 0; i < 2; ++i) {
		if (!create_buffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, nullptr,
		                   uniform_buffers[i], uniform_allocations[i], &uniform_mapped[i])) {
			return false;
		}
	}

	const VkDescriptorSetLayoutBinding dsl_binding{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};
	const VkDescriptorSetLayoutCreateInfo dsl_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &dsl_binding,
	};
	if (vkCreateDescriptorSetLayout(context.device, &dsl_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
		return false;
	}

	const VkDescriptorPoolSize pool_size{
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 2,
	};
	const VkDescriptorPoolCreateInfo pool_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		return false;
	}

	VkDescriptorSetLayout layouts[2] = {descriptor_set_layout, descriptor_set_layout};
	const VkDescriptorSetAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 2,
		.pSetLayouts = layouts,
	};
	if (vkAllocateDescriptorSets(context.device, &alloc_info, descriptor_sets) != VK_SUCCESS) {
		return false;
	}

	for (int i = 0; i < 2; ++i) {
		const VkDescriptorBufferInfo buffer_info{
			.buffer = uniform_buffers[i],
			.offset = 0,
			.range = sizeof(ObjectUniform),
		};
		const VkWriteDescriptorSet write{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_sets[i],
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		};
		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	const VkPipelineLayoutCreateInfo layout_info{ // Описание pipeline_layout
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // Идентификатор структуры
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) return false;
	// Создание pipeline layout

	const VkPipelineShaderStageCreateInfo stages[]{ // Описание начала pipeline
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // Идентификатор структуры
			.stage = VK_SHADER_STAGE_VERTEX_BIT, // Идентификатор стадии вершин (запись позиций вершин)
			.module = vertex_shader, 
			.pName = "main", // Функция точки ввода
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT, // Идентификатор стадии отрисовки пикселей
			.module = fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription binding{ // Описание шага по буферу
		.binding = 0, // Номер слота в буфере bif[binding]
		.stride = sizeof(Pyramide::Vertex), // Размер вершины
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX, // Устанавливает правило для каждой новой точки треугольника брать следующую вершину

	};

	const VkVertexInputAttributeDescription attributes[]{ // Описание попадания байтов шага в шейдер
		{
			.location = 0, // Номер входа в шейдере
			.binding = 0, // Номер слота буфера
			.format = VK_FORMAT_R32G32B32_SFLOAT, // Формат позицци (3 4-байтных float)
			.offset = offsetof(Pyramide::Vertex, position), // Начало поля в структуре вершины
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Pyramide::Vertex, color),
		},
	};
	const VkPipelineVertexInputStateCreateInfo vertex_input{ // Правила чтения вершин
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // Идентификатор структуры
		.vertexBindingDescriptionCount = 1, // Количество правил шагов
		.pVertexBindingDescriptions = &binding, // Правила шага в буфере 
		.vertexAttributeDescriptionCount = 2, // Количество описаний аттрибутов
		.pVertexAttributeDescriptions = attributes, // Описания аттрибутов
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly{ // Правила создания фигур из вершин
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, // Идентификатор структуры
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, // Правила по треугольнику на каждые три вержины
	};

	const VkPipelineViewportStateCreateInfo viewport{ // Описывает правила распределения по окну
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, // Идентификатор структуры
		.viewportCount = 1, // Количество областей выхода
		.scissorCount = 1, // Количество прямоугольников обрезки
	};

	const VkPipelineRasterizationStateCreateInfo rasterization{ // Правила превращения треугольника в пиксели
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, // Идентификатор структуры
		.polygonMode = VK_POLYGON_MODE_FILL, // Правила заливки треугольника целиком
		.cullMode = VK_CULL_MODE_NONE, // Задние грани не отбрасываются.
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // Правило лицевая грань -- та грань чьи вершины идут против часовой стрелки
		.lineWidth = 1.0f, // Толщина линии
	};

	const VkPipelineMultisampleStateCreateInfo multisample{ // Правила сглаживания пикселей
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, // Идентифифкатор структуры
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, // Правило 1 пиксель -- 1 образец (то либо цвет фона либо треугольника)
	};

	const VkPipelineDepthStencilStateCreateInfo depth{ // Правила глубины (расстояния от камеры) и трафарета
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE, // правило сравнения глубины перед записью.
		.depthWriteEnable = VK_TRUE, // Правило записи буфера после сравнения.
		.depthCompareOp = VK_COMPARE_OP_LESS, // Оставляет ближайший пиксель.
	}; // Трафарета тут нет
    
	const VkPipelineColorBlendAttachmentState blend_attachment{ // Правила использования цвета пикселя
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
		                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, // Доставлять все компоненты цвета
	};
	const VkPipelineColorBlendStateCreateInfo blend{ // Правила смешивания цвета
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1, // Количество цветных картинок
		.pAttachments = &blend_attachment, // Правила использования цвета
	};
	const VkDynamicState dynamic_states[]{ // Перечеслиние состояний, которые можно менять командами в PipeLine
		VK_DYNAMIC_STATE_VIEWPORT, // Размер области проекции
		VK_DYNAMIC_STATE_SCISSOR, // Прямоугольник обрезки
	};
	const VkPipelineDynamicStateCreateInfo dynamic{ // Передача динамических состояний
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2, // Количество состояний
		.pDynamicStates = dynamic_states, // Сами состояния
	};
	const VkGraphicsPipelineCreateInfo pipeline_info{ // Структура для создания pipeline
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2, // Количество шейдеров
		.pStages = stages, // Сами Шейдеры
		.pVertexInputState = &vertex_input, // Правила чтения вершин
		.pInputAssemblyState = &input_assembly, // Правила рисования треугольников
		.pViewportState = &viewport, // Правила областей выхода
		.pRasterizationState = &rasterization, // Правила отрисовки пикселей
		.pMultisampleState = &multisample, // Правила сглаживания пикселей
		.pDepthStencilState = &depth, // Правила глубины
		.pColorBlendState = &blend, // Правила смешивания цветов
		.pDynamicState = &dynamic, // Динамические состояния
		.layout = pipeline_layout, // Передача данных в Шейдеры
		.renderPass = context.render_pass, // Правила рисования одного кадра
		.subpass = 0, // Один этап рисования (в данном случае в окно)
	};
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) return false;
	// VK_NULL_HANDLE значит не нужен кэш pipelinов, 1 -- их количество
	last_time = 0.0;
	return true;

	
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, pipeline, nullptr);
    vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    vkDestroyShaderModule(context.device, vertex_shader, nullptr);
    vkDestroyShaderModule(context.device, fragment_shader, nullptr);
    vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_allocation);
    vmaDestroyBuffer(context.allocator, index_buffer, index_allocation);
	for (int i = 0; i < 2; ++i) {
		vmaDestroyBuffer(context.allocator, uniform_buffers[i], uniform_allocations[i]);
	}
	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
}

void update(double time) {
	const float dt = last_time == 0.0 ? 0.0f : float(time - last_time);
	last_time = time;
	if (anim_playing) {
		anim_time += dt;
	}

	ImGui::Begin("Lab 1");
	ImGui::Checkbox("Orthographic", &use_ortho);
	ImGui::DragFloat3("Position", position, 0.01f);
	ImGui::DragFloat3("Rotation", rotation, 0.01f);
	ImGui::DragFloat3("Scale", scale_xyz, 0.01f, 0.01f, 10.0f);
	ImGui::ColorEdit3("Color", ui_color);
	ImGui::Checkbox("Animation", &anim_playing);
	ImGui::SliderFloat("Anim speed", &anim_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Anim radius", &anim_radius, 0.0f, 5.0f);
	ImGui::End();

	ImGuiIO& io = ImGui::GetIO();
	if (!io.WantCaptureMouse && !ImGui::IsAnyItemActive() && io.MouseDown[0]) {
		rotation[1] += glm::radians(io.MouseDelta.x);
		rotation[0] += glm::radians(io.MouseDelta.y);
	}

	if (!io.WantCaptureMouse && !ImGui::IsAnyItemActive() && io.MouseWheel != 0.0f) {
		const float sc = io.MouseWheel > 0.0f ? 1.1f : 0.9f;
		scale_xyz[0] *= sc;
		scale_xyz[1] *= sc;
		scale_xyz[2] *= sc;
	}

	operator_matrix = make_model(position, rotation, scale_xyz, 0.0f);
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;
	vkResetCommandBuffer(fd.command_buffer, 0); // Сброс буфера комманд
	const VkCommandBufferBeginInfo begin{ // Создание начального состояния буфера комманд
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin); // Создания начального буфера комманд
	const VkClearValue clears[2]{ // Значение очищения рендера
		{.color = {{0.05f, 0.05f, 0.08f, 1.0f}}}, // Цвет фона
		{.depthStencil = {1.0f, 0}}, // Глубина (самая дальняя) и трафарет
	};
	const VkRenderPassBeginInfo pass{ // Информация для начала рисования
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass, // Цвет и глубина окна
		.framebuffer = fd.framebuffer, // Картинки кадра
		.renderArea = {{0, 0}, context.swapchain_extent}, // Область отчистки и рисования (прямоугольник)
		.clearValueCount = 2, // Количество Рендеров очищения
		.pClearValues = clears // Сами рендеры очищения.
	};
	vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE); // Начало рисования, последнее значение -- отсутствие второго буфера
	const VkViewport viewport{ // Область проекции в окне
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	const VkRect2D scissor{{0, 0}, context.swapchain_extent}; // Смещение и размер прямоугольника обрезки на экране
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport); // Запись в буфер команд области проекций
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor); // Запись в буфер комманд области обрезки
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);
	glm::mat4 projection;
	if (use_ortho) {
		const float h = 3.0f;
		const float w = h * aspect;
		projection = glm::orthoRH_ZO(-w, w, -h, h, 0.1f, 20.0f);
	} else {
		projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 20.0f); // Создание матрицы перспективы
		// Параметры 1) Угол обзора 2) масштабирующий аспект по окну 3) ближняя и дальняя плоскость отсечения
	}
	projection[1][1] *= -1.0f; 
	const glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 2.0f, 6.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));

	const glm::mat4 models[2] = {
		make_model(position, rotation, scale_xyz, 0.0f),
		make_model(position, rotation, scale_xyz, 3.14159265f),
	};
	const glm::vec4 colors[2] = {
		glm::vec4(ui_color[0], ui_color[1], ui_color[2], 1.0f),
		glm::vec4(1.0f - ui_color[0], 1.0f - ui_color[1], 1.0f - ui_color[2], 1.0f),
	};

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); // Вставляет pipeline в буфер комманд
	const VkDeviceSize offset = 0; // Смещение
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset); // Вставляем буфер вершин в командный буфер
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16); // Вставляем буфер индексов в командный буфер

	for (int i = 0; i < 2; ++i) {
		ObjectUniform u{
			.mvp = projection * view * models[i],
			.color = colors[i],
		};
		std::memcpy(uniform_mapped[i], &u, sizeof(u));
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[i], 0, nullptr);
		vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(pyramide.indices.size()), 1, 0, 0, 0); // Рисуем 1 пирамиду без смещения номеров вершин и экземпляра с первого индекса,    
	}

	vkCmdEndRenderPass(fd.command_buffer); // Завершаем текущий рендер.
	vkEndCommandBuffer(fd.command_buffer); // Завершаем запись в буфер

}

} // namespace application

// Вид матрицы перспективы [[near 0 0 0], [0 near 0 0], [0 0  far far*near], [0 0 -1 0]]
// Вид матрицы вращения [[], [], [], []]
