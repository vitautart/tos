#pragma once

#include <vulkan/vulkan.h>
#include <expected>
#include <vector>
#include <string>
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace engi
{
// Move-only RAII wrapper for VkPipeline
class Pipeline
{
public:
    Pipeline() = default;
    explicit Pipeline(VkPipeline pipeline, VkDevice device);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    VkPipeline get() const { return m_pipeline; }

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    // non-owning reference to device, used for destruction
    VkDevice m_device = VK_NULL_HANDLE;
};

class PipelineBuilder
{
public:
    PipelineBuilder() = default;
    ~PipelineBuilder() = default;

    // File-based shader creation
    auto vertex_shader_from_file(const std::string& path, VkDevice device) -> PipelineBuilder&;
    auto fragment_shader_from_file(const std::string& path, VkDevice device) -> PipelineBuilder&;
    auto fragment_specialization_u32(uint32_t constant_id, uint32_t value) -> PipelineBuilder&;

    // Render target configuration
    auto color_format(VkFormat format) -> PipelineBuilder&;
    auto depth_format(VkFormat format) -> PipelineBuilder&;
    auto samples(VkSampleCountFlagBits samples) -> PipelineBuilder&;
    
    // Input assembly
    auto topology(VkPrimitiveTopology topology) -> PipelineBuilder&;

    // Rasterization
    auto polygon_mode(VkPolygonMode mode) -> PipelineBuilder&;
    auto cull_mode(VkCullModeFlags mode) -> PipelineBuilder&;
    auto front_face(VkFrontFace face) -> PipelineBuilder&;
    auto line_width(float width) -> PipelineBuilder&;
    
    // Depth testing
    auto depth_test(bool enable) -> PipelineBuilder&;
    auto depth_write(bool enable) -> PipelineBuilder&;
    auto depth_compare_op(VkCompareOp op) -> PipelineBuilder&;
    
    // Blending
    auto blend_attachment(const VkPipelineColorBlendAttachmentState& attachment) -> PipelineBuilder&;
    auto alpha_blending() -> PipelineBuilder&;
    auto additive_blending() -> PipelineBuilder&;
    auto no_blending() -> PipelineBuilder&;

    // Add vertex input items one by one
    auto add(const VkVertexInputBindingDescription& binding) -> PipelineBuilder&;
    auto add(const VkVertexInputAttributeDescription& attribute) -> PipelineBuilder&;

    // Build the pipeline, returns RAII wrapper
    auto build(VkDevice device) -> std::expected<Pipeline, VkResult>;

private:
    // Shader modules created from files
    VkShaderModule m_vertex_shader = VK_NULL_HANDLE;
    VkShaderModule m_fragment_shader = VK_NULL_HANDLE;

    // Render target formats
    VkFormat m_color_format = VK_FORMAT_UNDEFINED;
    VkFormat m_depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;

    // Specialization constants
    std::vector<VkSpecializationMapEntry> m_fragment_spec_entries;
    std::vector<uint8_t> m_fragment_spec_data;
    
    // Input assembly
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::vector<VkVertexInputBindingDescription> m_vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> m_vertex_attributes;
    
    // Rasterization
    VkPolygonMode m_polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags m_cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace m_front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    float m_line_width = 1.0f;
    
    // Depth stencil
    bool m_depth_test = true;
    bool m_depth_write = true;
    VkCompareOp m_depth_compare_op = VK_COMPARE_OP_LESS;
    
    // Color blending
    std::vector<VkPipelineColorBlendAttachmentState> m_blend_attachments;
};

} // namespace engi