#include "application.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
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
		vertices = {
			{{0.0f, y_apex, 0.0f}, base_color},
			{{radius, y_base, 0.0f}, base_color},
			{{-0.5f * radius, y_base, 0.5f * radius * std::sqrt(3.0f)}, base_color},
			{{-0.5f * radius, y_base, -0.5f * radius * std::sqrt(3.0f)}, base_color},
		};
	}
};


Pyramide pyramide; // Собственно сама пирамида

bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                   VkBuffer& buffer, VmaAllocation& allocation) { 
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
	
	std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size)); //Копирование память м буфера в data
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

auto operator_matrix = glm::mat4(1.0f);
bool initialize() {
    const auto vertex_code = read_file(std::string("shaders/pyramide.vert.spv")); // Файл для позиции вершин
    const auto fragment_code = read_file(std::string("shaders/pyramide.frag.spv")); // Файл для цвета вершин
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
	const VkPushConstantRange push{ // Описание данных, напрямую попадаемых в шейдер
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, // Флаг для чтения только вершинного шейдера
		.offset = 0, // Смещение в байтах
		.size = sizeof(glm::mat4), // Размер данных
	};

	const VkPipelineLayoutCreateInfo layout_info{ // Описание pipeline_layout
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // Идентификатор структуры
		.pushConstantRangeCount = 1, // Количество push участков
		.pPushConstantRanges = &push, // Указатель на push участки
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
}

void update([[maybe_unused]] double time) {
	ImGuiIO& io = ImGui::GetIO();
	if (io.MouseDown[0]){
		const float dx = glm::radians(io.MouseDelta.x);
		const float dy = glm::radians(io.MouseDelta.y);
		operator_matrix = glm::rotate(glm::mat4(1.0f), dx, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::rotate(glm::mat4(1.0f), dy, glm::vec3(1.0f, 0.0f, 0.0f)) * operator_matrix;
	}

	if (io.MouseWheel){
		const float sc = io.MouseWheel > 0.0f ? 1.1f : 0.9f;
		operator_matrix = glm::scale(operator_matrix, glm::vec3(sc));  
	}
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
	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 10.0f); // Создание матрицы перспективы
	// Параметры 1) Угол обзора 2) масштабирующий аспект по окну 3) ближняя и дальняя плоскость отсечения
	projection[1][1] *= -1.0f; 
	const glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 1.0f, 4.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 mvp = projection * view * operator_matrix;
	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); // Вставляет pipeline в буфер комманд
	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp); // Вставка матрицы mvp в шейдеры
	const VkDeviceSize offset = 0; // Смещение
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset); // Вставляем буфер вершин в командный буфер
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16); // Вставляем буфер индексов в командный буфер
	vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(pyramide.indices.size()), 1, 0, 0, 0); // Рисуем 1 пирамиду без смещения номеров вершин и экземпляра с первого индекса,    
	vkCmdEndRenderPass(fd.command_buffer); // Завершаем текущий рендер.
	vkEndCommandBuffer(fd.command_buffer); // Завершаем запись в буфер

}

} // namespace application

// Вид матрицы перспективы [[near 0 0 0], [0 near 0 0], [0 0  far far*near], [0 0 -1 0]]
// Вид матрицы вращения [[], [], [], []]