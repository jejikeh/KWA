#include "render/frame_graph.h"

#include <core/enum.h>
#include <core/memory/linear_memory_resource.h>
#include <core/string.h>
#include <core/unordered_map.h>
#include <core/vector.h>

#include <vulkan/vulkan.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace kw {

class RenderVulkan;
class TimelineSemaphore;

class FrameGraphVulkan : public FrameGraph {
public:
    FrameGraphVulkan(const FrameGraphDescriptor& descriptor);
    ~FrameGraphVulkan() override;

    void render() override;
    void recreate_swapchain() override;

private:
    static constexpr size_t SWAPCHAIN_IMAGE_COUNT = 3;
    
    enum class AttachmentAccess : uint8_t {
        NONE            = 0b00000000,
        READ            = 0b00000001,
        WRITE           = 0b00000010,
        ATTACHMENT      = 0b00000100,
        VERTEX_SHADER   = 0b00001000,
        FRAGMENT_SHADER = 0b00010000,
        BLEND           = 0b00100000,
        LOAD            = 0b01000000,
        STORE           = 0b10000000,
    };

    KW_DECLARE_ENUM_BITMASK(AttachmentAccess);

    struct CreateContext {
        const FrameGraphDescriptor& frame_graph_descriptor;

        // Mapping from attachment names to attachment indices.
        UnorderedMap<StringView, uint32_t> attachment_mapping;

        // Attachment_count x render_pass_count matrix of access to a certain attachment on a certain render pass.
        Vector<AttachmentAccess> attachment_access_matrix;

        // Allocate a piece of memory and reuse it for each graphics pipeline.
        LinearMemoryResource graphics_pipeline_memory_resource;
    };

    struct AttachmentData {
        VkImage image;
        VkImageView image_view;

        // Sampled depth stencil attachments must have either depth or stencil aspect, not both.
        // Attachment must have both though. For color attachment and depth attachments these views alias each other.
        VkImageView sampled_view;

        // Occasionally min index may be larger than max index. This means that the attachment is created at the end
        // of the frame and used at the beginning of the next frame and therefore must be aliased in between.
        uint32_t min_parallel_block_index;
        uint32_t max_parallel_block_index;

        // Defines whether attachment is used as color attachment, depth stencil attachment or sampled.
        VkImageUsageFlags usage_mask;

        // Layout transition from `VK_IMAGE_LAYOUT_UNDEFINED` to specified image layout is performed manually
        // before the first render pass once after the attachment is created.
        VkAccessFlags initial_access_mask;
        VkImageLayout initial_layout;
    };

    struct AllocationData {
        // Render manages all device allocations.
        // These are indices and offsets in its internal allocators.
        uint64_t data_index : 16;
        uint64_t data_offset : 48;
    };
    
    struct DescriptorPoolData {
        VkDescriptorPool descriptor_pool;

        // When descriptor set needs more descriptor bindings than
        // descriptor pool has left, new descriptor pool is allocated.
        uint32_t descriptor_sets_left;
        uint32_t textures_left;
        uint32_t samplers_left;
        uint32_t uniform_buffers_left;
    };

    struct NoOpHash {
        // crc64 is already a decent hash.
        size_t operator()(uint64_t value) const;
    };

    struct DescriptorSetData {
        DescriptorSetData(VkDescriptorSet descriptor_set_, uint64_t last_frame_usage_);
        DescriptorSetData(const DescriptorSetData& other);

        VkDescriptorSet descriptor_set;

        // When descriptor set is not used for a certain number of frames,
        // it is considered to be retired and goes back to free list.
        std::atomic_uint64_t last_frame_usage;
    };

    struct GraphicsPipelineData {
        GraphicsPipelineData(MemoryResource& memory_resource);
        GraphicsPipelineData(GraphicsPipelineData&& other);

        VkShaderModule vertex_shader_module;
        VkShaderModule fragment_shader_module;
        VkDescriptorSetLayout descriptor_set_layout;
        VkPipelineLayout pipeline_layout;
        VkPipeline pipeline;

        // These are needed for draw call validation.
        uint32_t vertex_buffer_count;
        uint32_t instance_buffer_count;

        // Key here is crc64 of all descriptors in a descriptor set.
        UnorderedMap<uint64_t, DescriptorSetData, NoOpHash> bound_descriptor_sets;
        std::shared_mutex bound_descriptor_sets_mutex;

        // Descriptor sets are allocated in batches, the extra ones end up here. When descriptor sets retire,
        // they end up here. When new descriptor set is needed, it's queried from here.
        Vector<VkDescriptorSet> unbound_descriptor_sets;
        std::mutex unbound_descriptor_sets_mutex;

        // Descriptor sets are allocated in geometric sequence: 1, 2, 4, 8, 16 and so on. This way graphics pipelines
        // that use a few descriptor sets don't waste much of a descriptor memory, and render passes that need many
        // descriptor sets don't waste CPU time to allocate them.
        uint32_t descriptor_set_count;

        // This is needed for draw call validation. Actual number of descriptors written to descriptor set may be less
        // because of shader optimizations.
        uint32_t uniform_attachment_descriptor_count;

        // Mapping from `VkWriteDescriptorSet` indices to `m_attachment_data` indices.
        Vector<uint32_t> uniform_attachment_indices;

        // Mapping from `VkWriteDescriptorSet` indices to `DrawCallDescriptor` indices.
        Vector<uint32_t> uniform_attachment_mapping;

        // This is needed for draw call validation. Actual number of descriptors written to descriptor set may be less
        // because of shader optimizations.
        uint32_t uniform_texture_descriptor_count;

        // First texture from `uniform_texture_mapping` goes to this binding.
        uint32_t uniform_texture_first_binding;

        // Mapping from `VkWriteDescriptorSet` indices to `DrawCallDescriptor` indices.
        Vector<uint32_t> uniform_texture_mapping;

        // These are immutable samplers and there's no need to bind them to descriptor set.
        // They are stored to be destroyed after descriptor set layout is destroyed.
        Vector<VkSampler> uniform_samplers;

        // This is needed for draw call validation. Actual number of descriptors written to descriptor set may be less
        // because of shader optimizations.
        uint32_t uniform_buffer_descriptor_count;

        // First buffer from `uniform_buffer_mapping` goes to this binding.
        uint32_t uniform_buffer_first_binding;

        // Mapping from `VkWriteDescriptorSet` indices to `DrawCallDescriptor` indices.
        Vector<uint32_t> uniform_buffer_mapping;

        // Sizes of each uniform buffer in descriptor set.
        Vector<uint32_t> uniform_buffer_sizes;

        // This is needed for draw call validation and for push constants command. When push constants are optimized
        // away from the shader stages, this value is equal to the expected one anyway.
        uint32_t push_constants_size;

        // This is needed for push constants command. Might be different than graphics pipeline descriptor value
        // because of shader optimizations. Value of 0 means push constants were optimized away from all stages and
        // won't be pushed.
        VkShaderStageFlags push_constants_visibility;
    };

    struct RenderPassData {
        RenderPassData(MemoryResource& memory_resource);
        RenderPassData(RenderPassData&& other);

        VkRenderPass render_pass;

        // Graphics pipelines are indexed in draw call descriptors the same way they're indexed here.
        Vector<GraphicsPipelineData> graphics_pipeline_data;

        // These are used to begin render pass and to set scissor.
        uint32_t framebuffer_width;
        uint32_t framebuffer_height;

        // A render pass has multiple framebuffers if its attachments have `count` greater than 1.
        // A render pass has `SWAPCHAIN_IMAGE_COUNT` framebuffers if one of its attachments is a swapchain attachment.
        Vector<VkFramebuffer> framebuffers;

        // Render passes with the same parallel index are executed without pipeline barriers in between.
        uint32_t parallel_block_index;

        // Color attachment indices, followed by a depth stencil attachment index.
        Vector<uint32_t> attachment_indices;

        // User-specified `RenderPass` inheritor with overridden `render` method.
        RenderPass* render_pass_delegate;
    };

    struct ParallelBlockData {
        // These define a pipeline barrier that is placed between two consecutive parallel blocks.
        VkPipelineStageFlags source_stage_mask;
        VkPipelineStageFlags destination_stage_mask;
        VkAccessFlags source_access_mask;
        VkAccessFlags destination_access_mask;
    };

    struct ThreadTaskData {
        uint32_t render_pass_index;

        // This is defined as UINT32_MAX if swapchain attachment is present in a framebuffer.
        uint32_t framebuffer_index;
    };

    struct CommandPoolData {
        VkCommandPool command_pool;

        // Some of these are preallocated, some are created on demand during rendering.
        Vector<VkCommandBuffer> command_buffers;
    };

    class RenderPassContextVulkan : public RenderPassContext {
    public:
        RenderPassContextVulkan(FrameGraphVulkan& frame_graph, uint32_t swapchain_image_index,
                                VkCommandBuffer command_buffer, uint32_t render_pass_index,
                                uint32_t attachment_index, uint32_t attachment_width, uint32_t attachment_height);

        void draw(const DrawCallDescriptor& descriptor) override;

        uint32_t get_attachment_width() const override;
        uint32_t get_attachment_height() const override;
        uint32_t get_attachemnt_index() const override;

        // Used for synchronization of transfer and graphics queues.
        uint64_t transfer_semaphore_value;

    private:
        bool allocate_descriptor_sets(GraphicsPipelineData& graphics_pipeline_data);

        FrameGraphVulkan& m_frame_graph;
        uint32_t m_swapchain_image_index;
        VkCommandBuffer m_command_buffer;
        uint32_t m_render_pass_index;
        uint32_t m_attachment_index;
        uint32_t m_attachment_width;
        uint32_t m_attachment_height;
        uint32_t m_graphics_pipeline_index;
    };

    void create_lifetime_resources(const FrameGraphDescriptor& descriptor);
    void destroy_lifetime_resources();

    void create_surface(CreateContext& create_context);
    void compute_present_mode(CreateContext& create_context);

    void compute_attachment_descriptors(CreateContext& create_context);
    void compute_attachment_mapping(CreateContext& create_context);
    void compute_attachment_access(CreateContext& create_context);
    void compute_parallel_block_indices(CreateContext& create_context);
    void compute_parallel_blocks(CreateContext& create_context);
    void compute_attachment_ranges(CreateContext& create_context);
    void compute_attachment_usage_mask(CreateContext& create_context);
    void compute_attachment_layouts(CreateContext& create_context);

    void create_render_passes(CreateContext& create_context);
    void create_render_pass(CreateContext& create_context, uint32_t render_pass_index);
    void create_graphics_pipeline(CreateContext& create_context, uint32_t render_pass_index, uint32_t graphics_pipeline_index);

    void create_command_pools(CreateContext& create_context);
    void create_synchronization(CreateContext& create_context);
    void create_thread_tasks(CreateContext& create_context);

    void create_temporary_resources();
    void destroy_temporary_resources();

    bool create_swapchain();
    void create_swapchain_images();
    void create_swapchain_image_views();

    void create_attachment_images();
    void allocate_attachment_memory();
    void create_attachment_image_views();

    void create_framebuffers();

    RenderVulkan& m_render;
    Window& m_window;
    ThreadPool& m_thread_pool;

    uint32_t m_descriptor_set_count_per_descriptor_pool;
    uint32_t m_uniform_texture_count_per_descriptor_pool;
    uint32_t m_uniform_sampler_count_per_descriptor_pool;
    uint32_t m_uniform_buffer_count_per_descriptor_pool;

    VkFormat m_surface_format;
    VkColorSpaceKHR m_color_space;
    VkPresentModeKHR m_present_mode;

    uint32_t m_swapchain_width;
    uint32_t m_swapchain_height;

    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain;
    VkImage m_swapchain_images[SWAPCHAIN_IMAGE_COUNT];
    VkImageView m_swapchain_image_views[SWAPCHAIN_IMAGE_COUNT];

    Vector<AttachmentDescriptor> m_attachment_descriptors;
    Vector<AttachmentData> m_attachment_data;
    Vector<AllocationData> m_allocation_data;
    Vector<RenderPassData> m_render_pass_data;
    Vector<ParallelBlockData> m_parallel_block_data;

    // One render pass may render to a few framebuffers. We'd like to render those
    // framebuffers in parallel even though they share the same render pass.
    Vector<ThreadTaskData> m_thread_task_data;

    // Each frame in flight requires `thread_count` command pools.
    Vector<CommandPoolData> m_command_pool_data[SWAPCHAIN_IMAGE_COUNT];

    // Descriptor pools are mostly traversed and rarely changed, so prefer shared access.
    Vector<DescriptorPoolData> m_descriptor_pools;
    std::mutex m_descriptor_pools_mutex;

    // `vkAcquireNextImageKHR` and `vkQueuePresentKHR` don't support timeline semaphores,
    // so we're forced to deal with a bunch of binary semaphores.
    VkSemaphore m_image_acquired_binary_semaphores[SWAPCHAIN_IMAGE_COUNT];
    VkSemaphore m_render_finished_binary_semaphores[SWAPCHAIN_IMAGE_COUNT];

    // Fences for command buffer access synchronization.
    VkFence m_fences[SWAPCHAIN_IMAGE_COUNT];

    // This semaphore is signaled when frame rendering is finished along with `m_render_finished_binary_semaphores`.
    std::shared_ptr<TimelineSemaphore> m_render_finished_timeline_semaphore;

    // Used to calculate semaphore index and for descriptor set retirement detection.
    uint64_t m_frame_index;

    // When swapchain and all attachments are recreated, they are in undefined layout
    // and need to be manually transitioned to a layout expected by render passes.
    bool m_is_attachment_layout_set;
};

KW_DEFINE_ENUM_BITMASK(FrameGraphVulkan::AttachmentAccess);

} // namespace kw
