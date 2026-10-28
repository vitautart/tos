#include <fstream>
#include <cstring>
#include <print>

#include <engi/Pipeline.hpp>

#define VK_CHECK_PRINT(result, msg) if (result != VK_SUCCESS) { std::println(msg, (int)result); } 

// Shader file loading
static std::vector<char> read_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

// Pipeline RAII wrapper implementations
engi::Pipeline::Pipeline(VkPipeline pipeline, VkDevice device) : m_pipeline(pipeline), m_device(device) {}

engi::Pipeline::~Pipeline()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
}

engi::Pipeline::Pipeline(Pipeline&& other) noexcept : m_pipeline(VK_NULL_HANDLE)
{
    std::swap(m_pipeline, other.m_pipeline);
    std::swap(m_device, other.m_device);
}

engi::Pipeline& engi::Pipeline::operator=(Pipeline&& other) noexcept
{
    if (this != &other)
    {
        Pipeline tmp(std::move(other));
        std::swap(m_pipeline, tmp.m_pipeline);
        std::swap(m_device, tmp.m_device);
    }
    return *this;
}



auto engi::PipelineBuilder::vertex_shader_from_file(const std::string& path, VkDevice device) -> PipelineBuilder&
{
    auto code = read_file(path);
    if (!code.empty())
    {
        VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        auto result = vkCreateShaderModule(device, &info, nullptr, &m_vertex_shader);
        VK_CHECK_PRINT(result, "[ERROR] Vertex shader module creation failed: {}");
    }
    return *this;
}

auto engi::PipelineBuilder::fragment_shader_from_file(const std::string& path, VkDevice device) -> PipelineBuilder&
{
    auto code = read_file(path);
    if (!code.empty())
    {
        VkShaderModuleCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };
        auto result = vkCreateShaderModule(device, &info, nullptr, &m_fragment_shader);
        VK_CHECK_PRINT(result, "[ERROR] Fragment shader module creation failed: {}");
    }
    return *this;
}

auto engi::PipelineBuilder::fragment_specialization_u32(uint32_t constant_id, uint32_t value) -> PipelineBuilder&
{
    const auto offset = static_cast<uint32_t>(m_fragment_spec_data.size());
    m_fragment_spec_entries.push_back(VkSpecializationMapEntry
    {
        .constantID = constant_id,
        .offset = offset,
        .size = sizeof(uint32_t)
    });

    m_fragment_spec_data.resize(offset + sizeof(uint32_t));
    std::memcpy(m_fragment_spec_data.data() + offset, &value, sizeof(uint32_t));
    return *this;
}

// Render target configuration
auto engi::PipelineBuilder::color_format(VkFormat format) -> PipelineBuilder&
{
    m_color_format = format;
    return *this;
}

auto engi::PipelineBuilder::depth_format(VkFormat format) -> PipelineBuilder&
{
    m_depth_format = format;
    return *this;
}

auto engi::PipelineBuilder::samples(VkSampleCountFlagBits samples) -> PipelineBuilder&
{
    m_samples = samples;
    return *this;
}

// Input assembly
auto engi::PipelineBuilder::topology(VkPrimitiveTopology topology) -> PipelineBuilder&
{
    m_topology = topology;
    return *this;
}

// Rasterization
auto engi::PipelineBuilder::polygon_mode(VkPolygonMode mode) -> PipelineBuilder&
{
    m_polygon_mode = mode;
    return *this;
}

auto engi::PipelineBuilder::cull_mode(VkCullModeFlags mode) -> PipelineBuilder&
{
    m_cull_mode = mode;
    return *this;
}

auto engi::PipelineBuilder::front_face(VkFrontFace face) -> PipelineBuilder&
{
    m_front_face = face;
    return *this;
}

auto engi::PipelineBuilder::line_width(float width) -> PipelineBuilder&
{
    m_line_width = width;
    return *this;
}

// Depth testing
auto engi::PipelineBuilder::depth_test(bool enable) -> PipelineBuilder&
{
    m_depth_test = enable;
    return *this;
}

auto engi::PipelineBuilder::depth_write(bool enable) -> PipelineBuilder&
{
    m_depth_write = enable;
    return *this;
}

auto engi::PipelineBuilder::depth_compare_op(VkCompareOp op) -> PipelineBuilder&
{
    m_depth_compare_op = op;
    return *this;
}

// Blending
auto engi::PipelineBuilder::blend_attachment(const VkPipelineColorBlendAttachmentState& attachment) -> PipelineBuilder&
{
    m_blend_attachments.push_back(attachment);
    return *this;
}

auto engi::PipelineBuilder::alpha_blending() -> PipelineBuilder&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

auto engi::PipelineBuilder::additive_blending() -> PipelineBuilder&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

auto engi::PipelineBuilder::no_blending() -> PipelineBuilder&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

auto engi::PipelineBuilder::add(const VkVertexInputBindingDescription& binding) -> PipelineBuilder&
{
    m_vertex_bindings.push_back(binding);
    return *this;
}

auto engi::PipelineBuilder::add(const VkVertexInputAttributeDescription& attribute) -> PipelineBuilder&
{
    m_vertex_attributes.push_back(attribute);
    return *this;
}

// Build method
auto engi::PipelineBuilder::build(VkDevice device) -> std::expected<Pipeline, VkResult>
{
    if (m_vertex_shader == VK_NULL_HANDLE)
    {
        std::println("[ERROR] Pipeline build failed: vertex shader not set");
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
    }
    
    // If no blend attachments specified, add default one
    if (m_blend_attachments.empty())
        no_blending();
    
    VkSpecializationInfo fragment_specialization_info
    {
        .mapEntryCount = static_cast<uint32_t>(m_fragment_spec_entries.size()),
        .pMapEntries = m_fragment_spec_entries.data(),
        .dataSize = m_fragment_spec_data.size(),
        .pData = m_fragment_spec_data.data()
    };

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    
    if (m_vertex_shader != VK_NULL_HANDLE)
    {
        shader_stages.push_back(VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_vertex_shader,
            .pName = "main",
            .pSpecializationInfo = nullptr
        });
    }

    if (m_fragment_shader != VK_NULL_HANDLE)
    {
        shader_stages.push_back(VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = m_fragment_shader,
            .pName = "main",
            .pSpecializationInfo = m_fragment_spec_entries.empty() ? nullptr : &fragment_specialization_info
        });
    }
    
    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertex_input
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertex_bindings.size()),
        .pVertexBindingDescriptions = m_vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertex_attributes.size()),
        .pVertexAttributeDescriptions = m_vertex_attributes.data()
    };
    
    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo input_assembly
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = m_topology,
        .primitiveRestartEnable = VK_FALSE
    };
    
    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewport_state
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr
    };
    
    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterization
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = m_polygon_mode,
        .cullMode = m_cull_mode,
        .frontFace = m_front_face,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = m_line_width
    };
    
    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisample
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = m_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };
    
    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depth_stencil
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = m_depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = m_depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = m_depth_compare_op,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f
    };
    
    // Color blend state
    VkPipelineColorBlendStateCreateInfo color_blend
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(m_blend_attachments.size()),
        .pAttachments = m_blend_attachments.data(),
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };
    
    // Dynamic states
    VkDynamicState dynamic_states[] = 
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamic_state
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states
    };

    std::array<VkFormat, 1> color_formats = { m_color_format };
    // Rendering info
    VkPipelineRenderingCreateInfo rendering_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<uint32_t>(color_formats.size()),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = m_depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO};
    flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
    flags2.pNext = &rendering_info;
    
    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipeline_info
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &flags2,
        .flags = 0,
        .stageCount = static_cast<uint32_t>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = VK_NULL_HANDLE, // layout is not needed for VK_EXT_descriptor_heap
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);

    // Destroy shader modules we created (they are no longer needed after pipeline creation)
    if (m_vertex_shader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, m_vertex_shader, nullptr);
        m_vertex_shader = VK_NULL_HANDLE;
    }
    if (m_fragment_shader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, m_fragment_shader, nullptr);
        m_fragment_shader = VK_NULL_HANDLE;
    }

    if (result != VK_SUCCESS)
    {
        std::println("[ERROR] Graphics pipeline creation failed: {}", (int)result);
        return std::unexpected(result);
    }

    return Pipeline(pipeline, device);
}