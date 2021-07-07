#include "render/vulkan/frame_graph_vulkan.h"
#include "render/vulkan/render_vulkan.h"
#include "render/vulkan/timeline_semaphore.h"
#include "render/vulkan/vulkan_utils.h"

#include <system/window.h>

#include <core/debug/assert.h>
#include <core/debug/log.h>
#include <core/math/scalar.h>
#include <core/utils/crc_utils.h>

#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <fstream>
#include <numeric>
#include <thread>

namespace kw {

static const char* SEMANTIC_STRINGS[] = {
    "POSITION",
    "COLOR",
    "TEXCOORD",
    "NORMAL",
    "BINORMAL",
    "TANGENT",
    "JOINTS",
    "WEIGHTS",
};

static_assert(std::size(SEMANTIC_STRINGS) == SEMANTIC_COUNT);

static const VkPrimitiveTopology PRIMITIVE_TOPOLOGY_MAPPING[] = {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  // TRIANGLE_LIST
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, // TRIANGLE_STRIP
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,      // LINE_LIST
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,     // LINE_STRIP
    VK_PRIMITIVE_TOPOLOGY_POINT_LIST,     // POINT_LIST
};

static_assert(std::size(PRIMITIVE_TOPOLOGY_MAPPING) == PRIMITIVE_TOPOLOGY_COUNT);

static const VkPolygonMode FILL_MODE_MAPPING[] = {
    VK_POLYGON_MODE_FILL,  // FILL
    VK_POLYGON_MODE_LINE,  // LINE
    VK_POLYGON_MODE_POINT, // POINT
};

static_assert(std::size(FILL_MODE_MAPPING) == FILL_MODE_COUNT);

static const VkCullModeFlags CULL_MODE_MAPPING[] = {
    VK_CULL_MODE_BACK_BIT,  // BACK
    VK_CULL_MODE_FRONT_BIT, // FRONT
    VK_CULL_MODE_NONE,      // NONE
};

static_assert(std::size(CULL_MODE_MAPPING) == CULL_MODE_COUNT);

static const VkFrontFace FRONT_FACE_MAPPING[] = {
    VK_FRONT_FACE_COUNTER_CLOCKWISE, // COUNTER_CLOCKWISE
    VK_FRONT_FACE_CLOCKWISE,         // CLOCKWISE
};

static_assert(std::size(FRONT_FACE_MAPPING) == FRONT_FACE_COUNT);

static const VkStencilOp STENCIL_OP_MAPPING[] = {
    VK_STENCIL_OP_KEEP,                // KEEP
    VK_STENCIL_OP_ZERO,                // ZERO
    VK_STENCIL_OP_REPLACE,             // REPLACE
    VK_STENCIL_OP_INCREMENT_AND_CLAMP, // INCREMENT_AND_CLAMP
    VK_STENCIL_OP_DECREMENT_AND_CLAMP, // DECREMENT_AND_CLAMP
    VK_STENCIL_OP_INVERT,              // INVERT
    VK_STENCIL_OP_INCREMENT_AND_WRAP,  // INCREMENT_AND_WRAP
    VK_STENCIL_OP_DECREMENT_AND_WRAP,  // DECREMENT_AND_WRAP
};

static_assert(std::size(STENCIL_OP_MAPPING) == STENCIL_OP_COUNT);

static const VkCompareOp COMPARE_OP_MAPPING[] = {
    VK_COMPARE_OP_NEVER,            // NEVER
    VK_COMPARE_OP_LESS,             // LESS
    VK_COMPARE_OP_EQUAL,            // EQUAL
    VK_COMPARE_OP_LESS_OR_EQUAL,    // LESS_OR_EQUAL
    VK_COMPARE_OP_GREATER,          // GREATER
    VK_COMPARE_OP_NOT_EQUAL,        // NOT_EQUAL
    VK_COMPARE_OP_GREATER_OR_EQUAL, // GREATER_OR_EQUAL
    VK_COMPARE_OP_ALWAYS,           // ALWAYS
};

static_assert(std::size(COMPARE_OP_MAPPING) == COMPARE_OP_COUNT);

static const VkBlendFactor BLEND_FACTOR_MAPPING[] = {
    VK_BLEND_FACTOR_ZERO,                // ZERO
    VK_BLEND_FACTOR_ONE,                 // ONE
    VK_BLEND_FACTOR_SRC_COLOR,           // SOURCE_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, // SOURCE_INVERSE_COLOR
    VK_BLEND_FACTOR_SRC_ALPHA,           // SOURCE_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // SOURCE_INVERSE_ALPHA
    VK_BLEND_FACTOR_DST_COLOR,           // DESTINATION_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, // DESTINATION_INVERSE_COLOR
    VK_BLEND_FACTOR_DST_ALPHA,           // DESTINATION_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, // DESTINATION_INVERSE_ALPHA
};

static_assert(std::size(BLEND_FACTOR_MAPPING) == BLEND_FACTOR_COUNT);

static const VkBlendOp BLEND_OP_MAPPING[] = {
    VK_BLEND_OP_ADD,              // ADD
    VK_BLEND_OP_SUBTRACT,         // SUBTRACT
    VK_BLEND_OP_REVERSE_SUBTRACT, // REVERSE_SUBTRACT
    VK_BLEND_OP_MIN,              // MIN
    VK_BLEND_OP_MAX,              // MAX
};

static_assert(std::size(BLEND_OP_MAPPING) == BLEND_OP_COUNT);

static const VkShaderStageFlags SHADER_VISILITY_MAPPING[] = {
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // ALL
    VK_SHADER_STAGE_VERTEX_BIT,                                // VERTEX
    VK_SHADER_STAGE_FRAGMENT_BIT,                              // FRAGMENT
};

static_assert(std::size(SHADER_VISILITY_MAPPING) == SHADER_VISILITY_COUNT);

static const VkFilter FILTER_MAPPING[] = {
    VK_FILTER_LINEAR,  // LINEAR
    VK_FILTER_NEAREST, // NEAREST
};

static_assert(std::size(FILTER_MAPPING) == FILTER_COUNT);

static const VkSamplerMipmapMode MIP_FILTER_MAPPING[] = {
    VK_SAMPLER_MIPMAP_MODE_LINEAR,  // LINEAR
    VK_SAMPLER_MIPMAP_MODE_NEAREST, // NEAREST
};

static_assert(std::size(MIP_FILTER_MAPPING) == FILTER_COUNT);

static const VkSamplerAddressMode ADDRESS_MODE_MAPPING[] = {
    VK_SAMPLER_ADDRESS_MODE_REPEAT,          // WRAP
    VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, // MIRROR
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,   // CLAMP
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, // BORDER
};

static_assert(std::size(ADDRESS_MODE_MAPPING) == ADDRESS_MODE_COUNT);

static const VkBorderColor BORDER_COLOR_MAPPING[] = {
    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, // FLOAT_TRANSPARENT_BLACK
    VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,   // INT_TRANSPARENT_BLACK
    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,      // FLOAT_OPAQUE_BLACK
    VK_BORDER_COLOR_INT_OPAQUE_BLACK,        // INT_OPAQUE_BLACK
    VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,      // FLOAT_OPAQUE_WHITE
    VK_BORDER_COLOR_INT_OPAQUE_WHITE,        // INT_OPAQUE_WHITE
};

static_assert(std::size(BORDER_COLOR_MAPPING) == BORDER_COLOR_COUNT);

static const VkAttachmentLoadOp LOAD_OP_MAPPING[] = {
    VK_ATTACHMENT_LOAD_OP_CLEAR,     // CLEAR
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, // DONT_CARE
    VK_ATTACHMENT_LOAD_OP_LOAD,      // LOAD
};

static_assert(std::size(LOAD_OP_MAPPING) == LOAD_OP_COUNT);

static const VkDynamicState DYNAMIC_STATES[] = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
};

static void* spv_calloc(void* context, size_t count, size_t size) {
    MemoryResource* memory_resource = static_cast<MemoryResource*>(context);
    KW_ASSERT(memory_resource != nullptr);

    void* result = memory_resource->allocate(count * size, 1);
    std::memset(result, 0, count * size);
    return result;
}

static void spv_free(void* context, void* memory) {
    MemoryResource* memory_resource = static_cast<MemoryResource*>(context);
    KW_ASSERT(memory_resource != nullptr);

    memory_resource->deallocate(memory);
}

FrameGraphVulkan::FrameGraphVulkan(const FrameGraphDescriptor& descriptor)
    : m_render(static_cast<RenderVulkan&>(*descriptor.render))
    , m_window(*descriptor.window)
    , m_descriptor_set_count_per_descriptor_pool(static_cast<uint32_t>(descriptor.descriptor_set_count_per_descriptor_pool))
    , m_uniform_texture_count_per_descriptor_pool(static_cast<uint32_t>(descriptor.uniform_texture_count_per_descriptor_pool))
    , m_uniform_sampler_count_per_descriptor_pool(static_cast<uint32_t>(descriptor.uniform_sampler_count_per_descriptor_pool))
    , m_uniform_buffer_count_per_descriptor_pool(static_cast<uint32_t>(descriptor.uniform_buffer_count_per_descriptor_pool))
    , m_surface_format(VK_FORMAT_B8G8R8A8_UNORM)
    , m_color_space(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    , m_present_mode(VK_PRESENT_MODE_FIFO_KHR)
    , m_swapchain_width(0)
    , m_swapchain_height(0)
    , m_surface(VK_NULL_HANDLE)
    , m_swapchain(VK_NULL_HANDLE)
    , m_swapchain_images{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }
    , m_swapchain_image_views{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }
    , m_attachment_descriptors(m_render.persistent_memory_resource)
    , m_attachment_data(m_render.persistent_memory_resource)
    , m_allocation_data(m_render.persistent_memory_resource)
    , m_descriptor_pools(m_render.persistent_memory_resource)
    , m_render_pass_data(m_render.persistent_memory_resource)
    , m_parallel_block_data(m_render.persistent_memory_resource)
    , m_command_pool_data{
        UnorderedMap<std::thread::id, CommandPoolData>(m_render.persistent_memory_resource),
        UnorderedMap<std::thread::id, CommandPoolData>(m_render.persistent_memory_resource),
        UnorderedMap<std::thread::id, CommandPoolData>(m_render.persistent_memory_resource),
    }
    , m_image_acquired_binary_semaphores{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }
    , m_render_finished_binary_semaphores{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }
    , m_fences{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }
    , m_frame_index(0)
    , m_is_attachment_layout_set(false)
    , m_semaphore_index(UINT64_MAX)
    , m_swapchain_image_index(UINT32_MAX)
{
    create_lifetime_resources(descriptor);
    create_temporary_resources();
}

FrameGraphVulkan::~FrameGraphVulkan() {
    VK_ERROR(vkDeviceWaitIdle(m_render.device), "Failed to wait idle.");
    
    destroy_temporary_resources();
    destroy_lifetime_resources();
}

ShaderReflection FrameGraphVulkan::get_shader_reflection(const char* relative_path) {
    KW_ASSERT(relative_path != nullptr, "Invalid shader path.");

    ShaderReflection result{};

    //
    // Read shader from file system and query its reflection.
    //

    String relative_path_spv(relative_path, m_render.transient_memory_resource);

    KW_ERROR(
        relative_path_spv.find(".hlsl") != String::npos,
        "Shader file \"%s\" must have .hlsl extention.", relative_path
    );

    relative_path_spv.replace(relative_path_spv.find(".hlsl"), 5, ".spv");

    std::ifstream file(relative_path_spv.c_str(), std::ios::binary | std::ios::ate);

    KW_ERROR(
        file,
        "Failed to open shader file \"%s\".", relative_path
    );

    std::streamsize size = file.tellg();
    
    KW_ERROR(
        size >= 0,
        "Failed to query shader file size \"%s\".", relative_path
    );

    file.seekg(0, std::ios::beg);

    Vector<char> shader_data(size, m_render.transient_memory_resource);

    KW_ERROR(
        file.read(shader_data.data(), size),
        "Failed to read shader file \"%s\".", relative_path
    );

    SpvReflectShaderModule shader_reflection;

    SpvAllocator spv_allocator{};
    spv_allocator.calloc = &spv_calloc;
    spv_allocator.free = &spv_free;
    spv_allocator.context = &m_render.transient_memory_resource;

    SPV_ERROR(
        spvReflectCreateShaderModule(shader_data.size(), shader_data.data(), &shader_reflection, &spv_allocator),
        "Failed to create shader module from \"%s\".", relative_path
    );

    KW_ERROR(
        spvReflectGetEntryPoint(&shader_reflection, "main") != nullptr,
        "Shader \"%s\" must have entry point \"main\".", relative_path
    );

    //
    // Attribute descriptors.
    //

    uint32_t input_variable_count;

    SPV_ERROR(
        spvReflectEnumerateInputVariables(&shader_reflection, &input_variable_count, nullptr),
        "Failed to get input variable count."
    );

    SpvReflectInterfaceVariable** input_variables = m_render.transient_memory_resource.allocate<SpvReflectInterfaceVariable*>(input_variable_count);

    SPV_ERROR(
        spvReflectEnumerateInputVariables(&shader_reflection, &input_variable_count, input_variables),
        "Failed to get input variables."
    );

    AttributeDescriptor* attribute_descriptors = m_render.transient_memory_resource.allocate<AttributeDescriptor>(input_variable_count);

    for (uint32_t i = 0; i < input_variable_count; i++) {
        size_t j = 0;
        for (; j < std::size(SEMANTIC_STRINGS); j++) {
            size_t length = std::strlen(SEMANTIC_STRINGS[j]);
            if (std::strncmp(SEMANTIC_STRINGS[j], input_variables[i]->semantic, length) == 0) {
                attribute_descriptors[i].semantic = static_cast<Semantic>(j);

                KW_ERROR(
                    std::isdigit(*(input_variables[i]->semantic + length)),
                    "Invalid attribute semantic."
                );

                // `std::atoi` returns zero if no conversion can be performed, which is cool when semantic index is implicit.
                attribute_descriptors[i].semantic_index = static_cast<uint32_t>(std::atoi(input_variables[i]->semantic + length));
            }
        }

        KW_ERROR(
            j < std::size(SEMANTIC_STRINGS),
            "Invalid attribute semantic."
        );

        switch (input_variables[i]->format) {
        case SPV_REFLECT_FORMAT_R32_UINT:
            attribute_descriptors[i].format = TextureFormat::R32_UINT;
            break;
        case SPV_REFLECT_FORMAT_R32_SINT:
            attribute_descriptors[i].format = TextureFormat::R32_SINT;
            break;
        case SPV_REFLECT_FORMAT_R32_SFLOAT:
            attribute_descriptors[i].format = TextureFormat::R32_FLOAT;
            break;
        case SPV_REFLECT_FORMAT_R32G32_UINT:
            attribute_descriptors[i].format = TextureFormat::RG32_UINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32_SINT:
            attribute_descriptors[i].format = TextureFormat::RG32_SINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32_SFLOAT:
            attribute_descriptors[i].format = TextureFormat::RG32_FLOAT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32_UINT:
            attribute_descriptors[i].format = TextureFormat::RGB32_UINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32_SINT:
            attribute_descriptors[i].format = TextureFormat::RGB32_SINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT:
            attribute_descriptors[i].format = TextureFormat::RGB32_FLOAT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:
            attribute_descriptors[i].format = TextureFormat::RGBA32_UINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:
            attribute_descriptors[i].format = TextureFormat::RGBA32_SINT;
            break;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:
            attribute_descriptors[i].format = TextureFormat::RGBA32_FLOAT;
            break;
        default:
            KW_ERROR(false, "Invalid attribute format.");
        }
    }

    result.attribute_descriptors = attribute_descriptors;

    //
    // Uniforms.
    //

    uint32_t descriptor_binding_count;

    SPV_ERROR(
        spvReflectEnumerateDescriptorBindings(&shader_reflection, &descriptor_binding_count, nullptr),
        "Failed to get descriptor binding count."
    );

    SpvReflectDescriptorBinding** descriptor_bindings = m_render.transient_memory_resource.allocate<SpvReflectDescriptorBinding*>(descriptor_binding_count);

    SPV_ERROR(
        spvReflectEnumerateDescriptorBindings(&shader_reflection, &descriptor_binding_count, descriptor_bindings),
        "Failed to get descriptor bindings."
    );

    for (uint32_t i = 0; i < descriptor_binding_count; i++) {
        switch (descriptor_bindings[i]->descriptor_type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            result.uniform_texture_descriptor_count++;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            result.uniform_sampler_name_count++;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            result.uniform_buffer_descriptor_count++;
            break;
        }
    }

    UniformTextureDescriptor* uniform_texture_descriptors = m_render.transient_memory_resource.allocate<UniformTextureDescriptor>(result.uniform_texture_descriptor_count);
    UniformTextureDescriptor* uniform_texture_descriptor = uniform_texture_descriptors;

    const char** uniform_sampler_names = m_render.transient_memory_resource.allocate<const char*>(result.uniform_sampler_name_count);
    const char** uniform_sampler_name = uniform_sampler_names;
    
    UniformBufferDescriptor* uniform_buffer_descriptors = m_render.transient_memory_resource.allocate<UniformBufferDescriptor>(result.uniform_buffer_descriptor_count);
    UniformBufferDescriptor* uniform_buffer_descriptor = uniform_buffer_descriptors;

    for (uint32_t i = 0; i < descriptor_binding_count; i++) {
        KW_ERROR(
            descriptor_bindings[i]->name != nullptr,
            "Invalid uniform name."
        );

        size_t length = std::strlen(descriptor_bindings[i]->name);
        char* data = m_render.transient_memory_resource.allocate<char>(length + 1);
        std::strcpy(data, descriptor_bindings[i]->name);

        switch (descriptor_bindings[i]->descriptor_type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            uniform_texture_descriptor->variable_name = data;

            switch (descriptor_bindings[i]->image.dim) {
            case SpvDim2D:
                if (descriptor_bindings[i]->image.arrayed == 0) {
                    uniform_texture_descriptor->texture_type = TextureType::TEXTURE_2D;
                } else {
                    uniform_texture_descriptor->texture_type = TextureType::TEXTURE_2D_ARRAY;
                }
                break;
            case SpvDim3D:
                uniform_texture_descriptor->texture_type = TextureType::TEXTURE_3D;
                break;
            case SpvDimCube:
                if (descriptor_bindings[i]->image.arrayed == 0) {
                    uniform_texture_descriptor->texture_type = TextureType::TEXTURE_CUBE;
                } else {
                    uniform_texture_descriptor->texture_type = TextureType::TEXTURE_CUBE_ARRAY;
                }
                break;
            }

            uint32_t count = 1;
            for (uint32_t j = 0; j < descriptor_bindings[i]->array.dims_count; j++) {
                count *= descriptor_bindings[i]->array.dims[j];
            }

            (uniform_texture_descriptor++)->count = count;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            *(uniform_sampler_name++) = data;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            uniform_buffer_descriptor->variable_name = data;
            uniform_buffer_descriptor->size = descriptor_bindings[i]->block.size;

            uint32_t count = 1;
            for (uint32_t j = 0; j < descriptor_bindings[i]->array.dims_count; j++) {
                count *= descriptor_bindings[i]->array.dims[j];
            }

            (uniform_buffer_descriptor++)->count = count;
            break;
        }
    }

    result.uniform_texture_descriptors = uniform_texture_descriptors;
    result.uniform_sampler_names = uniform_sampler_names;
    result.uniform_buffer_descriptors = uniform_buffer_descriptors;

    //
    // Push constants.
    //

    if (shader_reflection.push_constant_block_count == 1) {
        KW_ERROR(
            shader_reflection.push_constant_blocks[0].name != nullptr,
            "Invalid push constants block name."
        );

        size_t length = std::strlen(shader_reflection.push_constant_blocks[0].name);
        char* data = m_render.transient_memory_resource.allocate<char>(length + 1);
        std::strcpy(data, shader_reflection.push_constant_blocks[0].name);

        result.push_constants_name = data;
        result.push_constants_size = shader_reflection.push_constant_blocks[0].size;
    }

    return result;
}

std::pair<Task*, Task*> FrameGraphVulkan::create_tasks() {
    return {
        new (m_render.transient_memory_resource.allocate<AcquireTask>()) AcquireTask(*this),
        new (m_render.transient_memory_resource.allocate<PresentTask>()) PresentTask(*this)
    };
}

void FrameGraphVulkan::recreate_swapchain() {
    VK_ERROR(vkDeviceWaitIdle(m_render.device), "Failed to wait idle.");

    destroy_temporary_resources();
    create_temporary_resources();
}

uint64_t FrameGraphVulkan::get_frame_index() const {
    uint64_t result;
    VK_ERROR(
        m_render.get_semaphore_counter_value(m_render.device, m_render_finished_timeline_semaphore->semaphore, &result),
        "Failed to query timeline semaphore counter value."
    );
    return result;
}

uint32_t FrameGraphVulkan::get_width() const {
    return m_swapchain_width;
}

uint32_t FrameGraphVulkan::get_height() const {
    return m_swapchain_height;
}

void FrameGraphVulkan::create_lifetime_resources(const FrameGraphDescriptor& descriptor) {
    CreateContext create_context{
        descriptor,
        UnorderedMap<StringView, uint32_t>(m_render.transient_memory_resource),
        Vector<AttachmentAccess>(m_render.transient_memory_resource),
        Vector<AttachmentBoundsData>(m_render.transient_memory_resource),
        Vector<AttachmentBarrierData>(m_render.transient_memory_resource),
        LinearMemoryResource(m_render.transient_memory_resource, 4 * 1024 * 1024)
    };

    create_surface(create_context);
    compute_present_mode(create_context);

    compute_attachment_descriptors(create_context);
    compute_attachment_mapping(create_context);
    compute_attachment_access(create_context);
    compute_attachment_barrier_data(create_context);
    compute_parallel_block_indices(create_context);
    compute_parallel_blocks(create_context);
    compute_attachment_ranges(create_context);
    compute_attachment_usage_mask(create_context);
    compute_attachment_layouts(create_context);
    compute_attachment_blit_data(create_context);

    create_render_passes(create_context);
    create_synchronization(create_context);
}

void FrameGraphVulkan::destroy_lifetime_resources() {
    m_render_finished_timeline_semaphore.reset();

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        vkDestroyFence(m_render.device, m_fences[swapchain_image_index], &m_render.allocation_callbacks);
        m_fences[swapchain_image_index] = VK_NULL_HANDLE;
    }

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        vkDestroySemaphore(m_render.device, m_render_finished_binary_semaphores[swapchain_image_index], &m_render.allocation_callbacks);
        m_render_finished_binary_semaphores[swapchain_image_index] = VK_NULL_HANDLE;
    }

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        vkDestroySemaphore(m_render.device, m_image_acquired_binary_semaphores[swapchain_image_index], &m_render.allocation_callbacks);
        m_image_acquired_binary_semaphores[swapchain_image_index] = VK_NULL_HANDLE;
    }

    for (DescriptorPoolData& descriptor_pool_data : m_descriptor_pools) {
        vkDestroyDescriptorPool(m_render.device, descriptor_pool_data.descriptor_pool, &m_render.allocation_callbacks);
    }
    m_descriptor_pools.clear();

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        for (auto& [_, command_pool_data] : m_command_pool_data[swapchain_image_index]) {
            vkDestroyCommandPool(m_render.device, command_pool_data.command_pool, &m_render.allocation_callbacks);
        }
        m_command_pool_data[swapchain_image_index].clear();
    }

    m_parallel_block_data.clear();

    for (RenderPassData& render_pass_data : m_render_pass_data) {
        for (GraphicsPipelineData& graphics_pipeline_data : render_pass_data.graphics_pipeline_data) {
            for (VkSampler& sampler : graphics_pipeline_data.uniform_samplers) {
                vkDestroySampler(m_render.device, sampler, &m_render.allocation_callbacks);
            }
            vkDestroyPipeline(m_render.device, graphics_pipeline_data.pipeline, &m_render.allocation_callbacks);
            vkDestroyPipelineLayout(m_render.device, graphics_pipeline_data.pipeline_layout, &m_render.allocation_callbacks);
            vkDestroyDescriptorSetLayout(m_render.device, graphics_pipeline_data.descriptor_set_layout, &m_render.allocation_callbacks);
            vkDestroyShaderModule(m_render.device, graphics_pipeline_data.fragment_shader_module, &m_render.allocation_callbacks);
            vkDestroyShaderModule(m_render.device, graphics_pipeline_data.vertex_shader_module, &m_render.allocation_callbacks);
        }
        vkDestroyRenderPass(m_render.device, render_pass_data.render_pass, &m_render.allocation_callbacks);
    }
    m_render_pass_data.clear();

    for (AttachmentDescriptor& attachment_descriptor : m_attachment_descriptors) {
        m_render.persistent_memory_resource.deallocate(const_cast<char*>(attachment_descriptor.name));
    }
    m_attachment_data.clear();

    vkDestroySurfaceKHR(m_render.instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
}

void FrameGraphVulkan::create_surface(CreateContext& create_context) {
    KW_ASSERT(m_surface == VK_NULL_HANDLE);
    SDL_ERROR(
        SDL_Vulkan_CreateSurface(m_window.get_sdl_window(), m_render.instance, &m_surface),
        "Failed to create Vulkan surface."
    );

    VkBool32 supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_render.physical_device, m_render.graphics_queue_family_index, m_surface, &supported);
    KW_ERROR(
        supported == VK_TRUE,
        "Graphics queue doesn't support presentation."
    );
}

void FrameGraphVulkan::compute_present_mode(CreateContext& create_context) {
    m_present_mode = VK_PRESENT_MODE_FIFO_KHR;

    if (!create_context.frame_graph_descriptor.is_vsync_enabled) {
        uint32_t present_mode_count;
        VK_ERROR(
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_render.physical_device, m_surface, &present_mode_count, nullptr),
            "Failed to query surface present mode count."
        );

        Vector<VkPresentModeKHR> present_modes(present_mode_count, m_render.transient_memory_resource);
        VK_ERROR(
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_render.physical_device, m_surface, &present_mode_count, present_modes.data()),
            "Failed to query surface present modes."
        );

        for (VkPresentModeKHR present_mode : present_modes) {
            if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                m_present_mode = present_mode;
            }
        }

        if (m_present_mode == VK_PRESENT_MODE_FIFO_KHR) {
            Log::print("[WARNING] Failed to turn VSync off.");
        }
    }
}

void FrameGraphVulkan::compute_attachment_descriptors(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    //
    // Calculate attachment count to avoid extra allocations.
    //

    uint32_t attachment_count = 1; // One for swapchain attachment.

    for (size_t i = 0; i < frame_graph_descriptor.color_attachment_descriptor_count; i++) {
        uint32_t count = std::max(frame_graph_descriptor.color_attachment_descriptors[i].count, 1U);
        attachment_count += count;
    }

    for (size_t i = 0; i < frame_graph_descriptor.depth_stencil_attachment_descriptor_count; i++) {
        uint32_t count = std::max(frame_graph_descriptor.depth_stencil_attachment_descriptors[i].count, 1U);
        attachment_count += count;
    }

    KW_ASSERT(
        m_attachment_descriptors.empty(),
        "Attachments descriptors are computed twice."
    );

    m_attachment_descriptors.reserve(attachment_count);

    //
    // Store all the attachments.
    //

    AttachmentDescriptor swapchain_attachment_descriptor{};
    swapchain_attachment_descriptor.name = frame_graph_descriptor.swapchain_attachment_name;
    swapchain_attachment_descriptor.load_op = LoadOp::DONT_CARE;
    swapchain_attachment_descriptor.format = TextureFormat::BGRA8_UNORM;
    swapchain_attachment_descriptor.is_blit_source = true;
    m_attachment_descriptors.push_back(swapchain_attachment_descriptor);

    for (size_t i = 0; i < frame_graph_descriptor.color_attachment_descriptor_count; i++) {
        uint32_t count = std::max(frame_graph_descriptor.color_attachment_descriptors[i].count, 1U);
        for (uint32_t j = 0; j < count; j++) {
            m_attachment_descriptors.push_back(frame_graph_descriptor.color_attachment_descriptors[i]);
        }
    }

    for (size_t i = 0; i < frame_graph_descriptor.depth_stencil_attachment_descriptor_count; i++) {
        uint32_t count = std::max(frame_graph_descriptor.depth_stencil_attachment_descriptors[i].count, 1U);
        for (uint32_t j = 0; j < count; j++) {
            m_attachment_descriptors.push_back(frame_graph_descriptor.depth_stencil_attachment_descriptors[i]);
        }
    }

    //
    // Names specified in descriptors can become corrupted after constructor execution. Normalize width, height and count.
    //

    for (AttachmentDescriptor& attachment_descriptor : m_attachment_descriptors) {
        size_t length = std::strlen(attachment_descriptor.name);

        char* copy = static_cast<char*>(m_render.persistent_memory_resource.allocate(length + 1, 1));
        std::memcpy(copy, attachment_descriptor.name, length + 1);

        attachment_descriptor.name = copy;

        if (attachment_descriptor.width == 0.f) {
            attachment_descriptor.width = 1.f;
        }

        if (attachment_descriptor.height == 0.f) {
            attachment_descriptor.height = 1.f;
        }

        attachment_descriptor.count = std::max(attachment_descriptor.count, 1U);
    }
}

void FrameGraphVulkan::compute_attachment_mapping(CreateContext& create_context) {
    KW_ASSERT(create_context.attachment_mapping.empty());
    create_context.attachment_mapping.reserve(m_attachment_descriptors.size());

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        // Attachment with `count` > 1 may fail on this one.
        create_context.attachment_mapping.emplace(StringView(m_attachment_descriptors[attachment_index].name), static_cast<uint32_t>(attachment_index));
    }
}

void FrameGraphVulkan::compute_attachment_access(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    //
    // Compute conservative attachment access matrix.
    //

    KW_ASSERT(create_context.attachment_access_matrix.empty());
    create_context.attachment_access_matrix.resize(frame_graph_descriptor.render_pass_descriptor_count * m_attachment_descriptors.size());

    for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
        const RenderPassDescriptor& render_pass_descriptor = frame_graph_descriptor.render_pass_descriptors[render_pass_index];

        for (size_t color_attachment_index = 0; color_attachment_index < render_pass_descriptor.color_attachment_name_count; color_attachment_index++) {
            const char* color_attachment_name = render_pass_descriptor.color_attachment_names[color_attachment_index];
            KW_ASSERT(create_context.attachment_mapping.count(color_attachment_name) > 0);

            uint32_t attachment_index = create_context.attachment_mapping[color_attachment_name];
            KW_ASSERT(attachment_index < m_attachment_descriptors.size());

            uint32_t attachment_count = m_attachment_descriptors[attachment_index].count;
            KW_ASSERT(attachment_index + attachment_count <= m_attachment_descriptors.size());

            for (uint32_t offset = 0; offset < attachment_count; offset++) {
                size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index + offset;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                create_context.attachment_access_matrix[access_index] |= AttachmentAccess::WRITE | AttachmentAccess::ATTACHMENT | AttachmentAccess::LOAD | AttachmentAccess::STORE;
            }
        }

        for (size_t graphics_pipeline_index = 0; graphics_pipeline_index < render_pass_descriptor.graphics_pipeline_descriptor_count; graphics_pipeline_index++) {
            const GraphicsPipelineDescriptor& graphics_pipeline_descriptor = render_pass_descriptor.graphics_pipeline_descriptors[graphics_pipeline_index];

            for (size_t attachment_blend_index = 0; attachment_blend_index < graphics_pipeline_descriptor.attachment_blend_descriptor_count; attachment_blend_index++) {
                const AttachmentBlendDescriptor& attachment_blend_descriptor = graphics_pipeline_descriptor.attachment_blend_descriptors[attachment_blend_index];

                const char* attachment_name = attachment_blend_descriptor.attachment_name;
                KW_ASSERT(create_context.attachment_mapping.count(attachment_name) > 0);

                uint32_t attachment_index = create_context.attachment_mapping[attachment_name];
                KW_ASSERT(attachment_index < m_attachment_descriptors.size());

                uint32_t attachment_count = m_attachment_descriptors[attachment_index].count;
                KW_ASSERT(attachment_index + attachment_count <= m_attachment_descriptors.size());

                for (uint32_t offset = 0; offset < attachment_count; offset++) {
                    size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index + offset;
                    KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                    create_context.attachment_access_matrix[access_index] |= AttachmentAccess::BLEND;
                }
            }
        }

        if (render_pass_descriptor.depth_stencil_attachment_name != nullptr) {
            const char* depth_stencil_attachment_name = render_pass_descriptor.depth_stencil_attachment_name;
            KW_ASSERT(create_context.attachment_mapping.count(depth_stencil_attachment_name) > 0);

            uint32_t attachment_index = create_context.attachment_mapping[depth_stencil_attachment_name];
            KW_ASSERT(attachment_index < m_attachment_descriptors.size());

            uint32_t attachment_count = m_attachment_descriptors[attachment_index].count;
            KW_ASSERT(attachment_index + attachment_count <= m_attachment_descriptors.size());

            AttachmentAccess attachment_access = AttachmentAccess::READ;

            for (size_t graphics_pipeline_index = 0; graphics_pipeline_index < render_pass_descriptor.graphics_pipeline_descriptor_count; graphics_pipeline_index++) {
                const GraphicsPipelineDescriptor& graphics_pipeline_descriptor = render_pass_descriptor.graphics_pipeline_descriptors[graphics_pipeline_index];

                if ((graphics_pipeline_descriptor.is_depth_test_enabled && graphics_pipeline_descriptor.is_depth_write_enabled) ||
                    (graphics_pipeline_descriptor.is_stencil_test_enabled && graphics_pipeline_descriptor.stencil_write_mask != 0)) {
                    attachment_access = AttachmentAccess::WRITE;
                    break;
                }
            }

            for (uint32_t offset = 0; offset < attachment_count; offset++) {
                size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index + offset;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                create_context.attachment_access_matrix[access_index] |= attachment_access | AttachmentAccess::ATTACHMENT | AttachmentAccess::LOAD | AttachmentAccess::STORE;
            }
        }

        for (size_t graphics_pipeline_index = 0; graphics_pipeline_index < render_pass_descriptor.graphics_pipeline_descriptor_count; graphics_pipeline_index++) {
            const GraphicsPipelineDescriptor& graphics_pipeline_descriptor = render_pass_descriptor.graphics_pipeline_descriptors[graphics_pipeline_index];

            for (size_t uniform_attachment_index = 0; uniform_attachment_index < graphics_pipeline_descriptor.uniform_attachment_descriptor_count; uniform_attachment_index++) {
                const UniformAttachmentDescriptor& uniform_attachment_descriptor = graphics_pipeline_descriptor.uniform_attachment_descriptors[uniform_attachment_index];
                KW_ASSERT(create_context.attachment_mapping.count(uniform_attachment_descriptor.attachment_name) > 0);

                uint32_t attachment_index = create_context.attachment_mapping[uniform_attachment_descriptor.attachment_name];
                KW_ASSERT(attachment_index < m_attachment_descriptors.size());

                uint32_t attachment_count = m_attachment_descriptors[attachment_index].count;
                KW_ASSERT(attachment_index + attachment_count <= m_attachment_descriptors.size());

                AttachmentAccess shader_access = AttachmentAccess::NONE;

                switch (uniform_attachment_descriptor.visibility) {
                case ShaderVisibility::ALL:
                    shader_access = AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER;
                    break;
                case ShaderVisibility::VERTEX:
                    shader_access = AttachmentAccess::VERTEX_SHADER;
                    break;
                case ShaderVisibility::FRAGMENT:
                    shader_access = AttachmentAccess::FRAGMENT_SHADER;
                    break;
                }

                for (uint32_t offset = 0; offset < attachment_count; offset++) {
                    size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index + offset;
                    KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                    create_context.attachment_access_matrix[access_index] |= AttachmentAccess::READ | shader_access;
                }
            }
        }
    }

    //
    // Compute attachment bounds.
    //

    KW_ASSERT(
        create_context.attachment_bounds_data.empty(),
        "Attachment bounds computed twice."
    );

    create_context.attachment_bounds_data.resize(
        m_attachment_descriptors.size(),
        AttachmentBoundsData{ UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX }
    );

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentBoundsData& attachment_bounds = create_context.attachment_bounds_data[attachment_index];

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                if (attachment_bounds.min_read_render_pass_index == UINT32_MAX) {
                    attachment_bounds.min_read_render_pass_index = render_pass_index;
                }

                attachment_bounds.max_read_render_pass_index = render_pass_index;
            }

            if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                if (attachment_bounds.min_write_render_pass_index == UINT32_MAX) {
                    attachment_bounds.min_write_render_pass_index = render_pass_index;
                }

                attachment_bounds.max_write_render_pass_index = render_pass_index;
            }
        }
    }

    //
    // Compute precise attachment access matrix (remove extra loads and stores).
    //

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentBoundsData& attachment_bounds = create_context.attachment_bounds_data[attachment_index];

        if (attachment_descriptor.load_op != LoadOp::LOAD) {
            if (attachment_bounds.min_write_render_pass_index != UINT32_MAX) {
                size_t access_index = attachment_bounds.min_write_render_pass_index * m_attachment_descriptors.size() + attachment_index;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                // We either DONT_CARE or CLEAR this attachment, so we can remove first write LOAD.
                create_context.attachment_access_matrix[access_index] &= ~AttachmentAccess::LOAD;
            }

            if (attachment_index == 0) {
                // This restriction allows the last write render pass to transition the attachment image to present layout.
                KW_ERROR(
                    attachment_bounds.max_read_render_pass_index == UINT32_MAX ||
                    (attachment_bounds.max_write_render_pass_index != UINT32_MAX &&
                     attachment_bounds.min_read_render_pass_index > attachment_bounds.min_write_render_pass_index &&
                     attachment_bounds.max_read_render_pass_index < attachment_bounds.max_write_render_pass_index),
                    "Swapchain image must not be read before the first write nor after the last write."
                );
            }

            if (attachment_bounds.max_write_render_pass_index != UINT32_MAX) {
                size_t access_index = attachment_bounds.max_write_render_pass_index * m_attachment_descriptors.size() + attachment_index;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                AttachmentAccess& attachment_access = create_context.attachment_access_matrix[access_index];

                if (attachment_bounds.max_read_render_pass_index != UINT32_MAX) {
                    if (attachment_bounds.min_read_render_pass_index > attachment_bounds.min_write_render_pass_index &&
                        attachment_bounds.max_read_render_pass_index < attachment_bounds.max_write_render_pass_index)
                    {
                        // All read accesses are between write accesses, so the last write access is followed by a write access that doesn't load.
                        attachment_access &= ~AttachmentAccess::STORE;
                    }
                } else {
                    // Only write accesses, the last write access shouldn't store because it is followed by a write access that doesn't load.
                    attachment_access &= ~AttachmentAccess::STORE;
                }
            }
        }
    }
}

void FrameGraphVulkan::compute_attachment_barrier_data(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_bounds_data.empty(),
        "Attachment bounds must be computed first."
    );

    KW_ASSERT(
        create_context.attachment_barrier_matrix.empty(),
        "Attachment barriers are initialized twice."
    );

    create_context.attachment_barrier_matrix.resize(
        frame_graph_descriptor.render_pass_descriptor_count * m_attachment_descriptors.size(),
        AttachmentBarrierData{}
    );

    KW_ASSERT(
        create_context.attachment_access_matrix.size() == create_context.attachment_barrier_matrix.size(),
        "Attachment access matrix must be computed first."
    );

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentBoundsData& attachment_bounds = create_context.attachment_bounds_data[attachment_index];

        VkImageLayout layout_attachment_optimal;
        VkImageLayout layout_read_only_optimal;

        if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
            layout_attachment_optimal = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            layout_read_only_optimal = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } else {
            layout_attachment_optimal = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            layout_read_only_optimal = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        //
        // If attachment is not read or written, perform an early quit. The further code can safely assume that there's
        // at least read or write access happening to this attachment.
        //

        if (attachment_bounds.max_read_render_pass_index == UINT32_MAX && attachment_bounds.max_write_render_pass_index == UINT32_MAX) {
            for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
                size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

                if (attachment_index == 0) {
                    attachment_barrier_data.source_image_layout = attachment_barrier_data.destination_image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    attachment_barrier_data.source_access_mask = attachment_barrier_data.destination_access_mask = VK_ACCESS_NONE_KHR;
                    attachment_barrier_data.source_pipeline_stage_mask = attachment_barrier_data.source_pipeline_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;
                } else {
                    attachment_barrier_data.source_image_layout = attachment_barrier_data.destination_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    attachment_barrier_data.source_access_mask = attachment_barrier_data.destination_access_mask = VK_ACCESS_NONE_KHR;
                    attachment_barrier_data.source_pipeline_stage_mask = attachment_barrier_data.source_pipeline_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;
                }
            }

            continue;
        }

        //
        // Compute source image layout for READ/WRITE render passes.
        //

        AttachmentAccess previous_attachment_access;

        if (attachment_bounds.max_read_render_pass_index != UINT32_MAX && attachment_bounds.max_write_render_pass_index != UINT32_MAX) {
            uint32_t max_render_pass_index = std::max(attachment_bounds.max_read_render_pass_index, attachment_bounds.max_write_render_pass_index);

            size_t access_index = max_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            previous_attachment_access = create_context.attachment_access_matrix[access_index];
            KW_ASSERT(previous_attachment_access != AttachmentAccess::NONE);
        } else {
            KW_ASSERT(
                attachment_bounds.max_read_render_pass_index != UINT32_MAX || attachment_bounds.max_write_render_pass_index != UINT32_MAX,
                "One of the above checks ensures that this attachment is at least read or written once."
            );

            uint32_t max_render_pass_index = std::min(attachment_bounds.max_read_render_pass_index, attachment_bounds.max_write_render_pass_index);

            size_t access_index = max_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            previous_attachment_access = create_context.attachment_access_matrix[access_index];
            KW_ASSERT(previous_attachment_access != AttachmentAccess::NONE);
        }

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                // Read render pass can't change image layout, so previous render pass must have set it to read only.
                attachment_barrier_data.source_image_layout = layout_read_only_optimal;
                previous_attachment_access = attachment_access;
            } else if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                if ((attachment_access & AttachmentAccess::LOAD) == AttachmentAccess::NONE) {
                    // This is a first CLEAR/DONT_CARE WRITE render pass on this frame. Ignore attachment content.
                    attachment_barrier_data.source_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
                } else if ((previous_attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                    // Read render pass can't change image layout, so previous render pass must have set it to read only.
                    attachment_barrier_data.source_image_layout = layout_read_only_optimal;
                } else {
                    // Write render pass followed by another write render pass don't perform any layout transitions.
                    attachment_barrier_data.source_image_layout = layout_attachment_optimal;
                }

                previous_attachment_access = attachment_access;
            } else {
                KW_ASSERT(
                    attachment_access == AttachmentAccess::NONE,
                    "Attachment access without READ or WRITE flags must be equal to NONE."
                );
            }
        }

        //
        // Compute destination image layout for READ/WRITE render passes.
        //

        AttachmentAccess next_attachment_access;

        if (attachment_bounds.min_read_render_pass_index != UINT32_MAX && attachment_bounds.min_write_render_pass_index != UINT32_MAX) {
            uint32_t min_render_pass_index = std::min(attachment_bounds.min_read_render_pass_index, attachment_bounds.max_write_render_pass_index);

            size_t access_index = min_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            next_attachment_access = create_context.attachment_access_matrix[access_index];
            KW_ASSERT(next_attachment_access != AttachmentAccess::NONE);
        } else {
            KW_ASSERT(
                attachment_bounds.min_read_render_pass_index != UINT32_MAX || attachment_bounds.min_write_render_pass_index != UINT32_MAX,
                "One of the above checks ensures that this attachment is at least read or written once."
            );

            uint32_t min_render_pass_index = std::min(attachment_bounds.min_read_render_pass_index, attachment_bounds.min_write_render_pass_index);

            size_t access_index = min_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            next_attachment_access = create_context.attachment_access_matrix[access_index];
            KW_ASSERT(next_attachment_access != AttachmentAccess::NONE);
        }

        for (size_t render_pass_index = frame_graph_descriptor.render_pass_descriptor_count; render_pass_index > 0; render_pass_index--) {
            size_t access_index = (render_pass_index - 1) * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                // Read render pass can't change image layout, so keep read only image layout.
                attachment_barrier_data.destination_image_layout = layout_read_only_optimal;
                next_attachment_access = attachment_access;
            } else if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                if ((attachment_access & AttachmentAccess::STORE) == AttachmentAccess::NONE) {
                    if (attachment_index == 0) {
                        // Swapchain attachment must be transitioned to present image layout before present.
                        attachment_barrier_data.destination_image_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    } else {
                        // DONT_CARE render passes are always write render passes. The next render pass always ignores
                        // the attachment layout, so we can just avoid doing any image layout transitions.
                        attachment_barrier_data.destination_image_layout = layout_attachment_optimal;
                    }
                } else if ((next_attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                    // Read render pass can't change image layout, so keep current image layout value.
                    attachment_barrier_data.destination_image_layout = layout_read_only_optimal;
                } else {
                    // Write render pass followed by another write render pass don't perform any layout transitions.
                    attachment_barrier_data.destination_image_layout = layout_attachment_optimal;
                }

                next_attachment_access = attachment_access;
            } else {
                KW_ASSERT(
                    attachment_access == AttachmentAccess::NONE,
                    "Attachment access without READ or WRITE flags must be equal to NONE."
                );
            }
        }

        //
        // Compute source/destination image layouts for NONE render passes.
        //

        VkImageLayout previous_image_layout;

        if (attachment_bounds.max_read_render_pass_index != UINT32_MAX && attachment_bounds.max_write_render_pass_index != UINT32_MAX) {
            uint32_t max_render_pass_index = std::max(attachment_bounds.max_read_render_pass_index, attachment_bounds.max_write_render_pass_index);

            size_t access_index = max_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            previous_image_layout = create_context.attachment_barrier_matrix[access_index].destination_image_layout;
        } else {
            KW_ASSERT(
                attachment_bounds.max_read_render_pass_index != UINT32_MAX || attachment_bounds.max_write_render_pass_index != UINT32_MAX,
                "One of the above checks ensures that this attachment is at least read or written once."
            );

            uint32_t max_render_pass_index = std::min(attachment_bounds.max_read_render_pass_index, attachment_bounds.max_write_render_pass_index);

            size_t access_index = max_render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            previous_image_layout = create_context.attachment_barrier_matrix[access_index].destination_image_layout;
        }

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if (attachment_access == AttachmentAccess::NONE) {
                attachment_barrier_data.source_image_layout = attachment_barrier_data.destination_image_layout = previous_image_layout;
            } else {
                previous_image_layout = attachment_barrier_data.destination_image_layout;
            }
        }

        //
        // Compute source access mask & source pipeline stage for READ/WRITE render passes.
        //

        next_attachment_access = AttachmentAccess::NONE;

        for (size_t render_pass_index = frame_graph_descriptor.render_pass_descriptor_count; render_pass_index > 0; render_pass_index--) {
            size_t access_index = (render_pass_index - 1) * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                KW_ASSERT(
                    (attachment_access & (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER | AttachmentAccess::ATTACHMENT)) != AttachmentAccess::NONE,
                    "Read attachment access implies either VERTEX_SHADER, FRAGMENT_SHADER or ATTACHMENT."
                );

                // `...READ_BIT` in source access mask is a no-op.
                attachment_barrier_data.source_access_mask = VK_ACCESS_NONE_KHR;

                if ((next_attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                    if ((attachment_access & AttachmentAccess::FRAGMENT_SHADER) == AttachmentAccess::FRAGMENT_SHADER) {
                        // We read from this attachment in fragment shader on current render pass, which means
                        // that the writing next render pass needs to wait for fragment shader to complete.
                        attachment_barrier_data.source_pipeline_stage_mask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    }

                    if ((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT) {
                        // We perform depth test with this attachment on current render pass, which means
                        // that the next writing render pass needs to wait for early fragment tests to complete.
                        KW_ASSERT(TextureFormatUtils::is_depth_stencil(attachment_descriptor.format));
                        attachment_barrier_data.source_pipeline_stage_mask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                    }

                    if ((attachment_access & AttachmentAccess::VERTEX_SHADER) == AttachmentAccess::VERTEX_SHADER) {
                        // We read from this attachment in vertex shader on current render pass, which means
                        // that the next writing render pass needs to wait for vertex shader to complete.
                        attachment_barrier_data.source_pipeline_stage_mask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                    }
                } else {
                    // We read from this attachment on both current render pass and the next render pass,
                    // the next render pass doesn't have to wait for that.
                    attachment_barrier_data.source_pipeline_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }

                next_attachment_access = attachment_access;
            } else if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                KW_ASSERT(
                    (attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT,
                    "Write access must be ATTACHMENT."
                );

                // LOAD and BLEND checks are omitted here because they produce `...READ_BIT` access masks,
                // which are redundant for source access mask. STORE is taken into consideration though.
                if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                    // We write to this depth stencil attachment on current render pass, which means that the next
                    // render pass needs to wait for late fragment tests to complete to start rendering.
                    attachment_barrier_data.source_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    attachment_barrier_data.source_pipeline_stage_mask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                } else {
                    // We write to this color attachment on current render pass, which means that the next
                    // render pass needs to wait for color attachment output to start rendering.
                    attachment_barrier_data.source_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    attachment_barrier_data.source_pipeline_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                }

                next_attachment_access = attachment_access;
            } else {
                KW_ASSERT(
                    attachment_access == AttachmentAccess::NONE,
                    "Attachment access without READ or WRITE flags must be equal to NONE."
                );
            }
        }

        //
        // Compute source access mask & source pipeline stage for NONE render passes.
        //

        VkAccessFlags previous_access_mask = VK_ACCESS_NONE_KHR;
        VkPipelineStageFlags previous_pipeline_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if (attachment_access == AttachmentAccess::NONE) {
                attachment_barrier_data.source_access_mask = previous_access_mask;
                attachment_barrier_data.source_pipeline_stage_mask = previous_pipeline_stage_mask;
            } else {
                previous_access_mask = attachment_barrier_data.source_access_mask;
                previous_pipeline_stage_mask = attachment_barrier_data.source_pipeline_stage_mask;
            }

            KW_ASSERT(attachment_barrier_data.source_pipeline_stage_mask != VK_PIPELINE_STAGE_NONE_KHR);
        }

        //
        // Compute destination access mask & destination pipeline stage.
        //

        next_attachment_access = AttachmentAccess::NONE;

        for (size_t render_pass_index = frame_graph_descriptor.render_pass_descriptor_count; render_pass_index > 0; render_pass_index--) {
            size_t access_index = (render_pass_index - 1) * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if (attachment_access != AttachmentAccess::NONE) {
                if ((next_attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                    KW_ASSERT(
                        (next_attachment_access& (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER | AttachmentAccess::ATTACHMENT)) != AttachmentAccess::NONE,
                        "Read attachment access implies either VERTEX_SHADER, FRAGMENT_SHADER or ATTACHMENT."
                    );

                    if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                        if ((next_attachment_access & AttachmentAccess::VERTEX_SHADER) == AttachmentAccess::VERTEX_SHADER) {
                            // We read this attachment in vertex shader in the next render pass after writing to it in
                            // current render pass. The next render pass is allowed to execute every pipeline stage
                            // before vertex shader without waiting.
                            attachment_barrier_data.destination_access_mask |= VK_ACCESS_SHADER_READ_BIT;
                            attachment_barrier_data.destination_pipeline_stage_mask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                        }

                        if ((next_attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT) {
                            // We perform depth test in the next render pass after writing to it in current render pass.
                            // The next render pass is allowed to execute every pipeline stage before early fragment
                            // tests without waiting.
                            KW_ASSERT(TextureFormatUtils::is_depth_stencil(attachment_descriptor.format));
                            attachment_barrier_data.destination_access_mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                            attachment_barrier_data.destination_pipeline_stage_mask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                        }

                        if ((next_attachment_access & AttachmentAccess::FRAGMENT_SHADER) == AttachmentAccess::FRAGMENT_SHADER) {
                            // We read this attachment in fragment shader in the next render pass after writing to it
                            // in current render pass. The next render pass is allowed to execute every pipeline stage
                            // before fragment shader without waiting.
                            attachment_barrier_data.destination_access_mask |= VK_ACCESS_SHADER_READ_BIT;
                            attachment_barrier_data.destination_pipeline_stage_mask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                        }
                    } else {
                        // We read from this attachment on both current render pass and the next render pass,
                        // the next render pass can perform all pipeline stages.
                        attachment_barrier_data.destination_access_mask = VK_ACCESS_NONE_KHR;
                        attachment_barrier_data.destination_pipeline_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                    }
                } else if ((next_attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                    KW_ASSERT(
                        (next_attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT,
                        "Write access must be ATTACHMENT."
                    );

                    if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                        // Next render pass writes to this depth stencil attachment, so it is allowed to execute
                        // every stage before early fragment tests without waiting.
                        attachment_barrier_data.destination_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                        if ((attachment_access & AttachmentAccess::LOAD) == AttachmentAccess::LOAD) {
                            attachment_barrier_data.destination_access_mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                        }
                        attachment_barrier_data.destination_pipeline_stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                    } else {
                        // Next parallel block writes to this color attachment, so it is allowed to execute every stage
                        // before color attachment output without waiting.
                        attachment_barrier_data.destination_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                        if ((attachment_access & (AttachmentAccess::LOAD | AttachmentAccess::BLEND)) != AttachmentAccess::NONE) {
                            attachment_barrier_data.destination_access_mask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
                        }
                        attachment_barrier_data.destination_pipeline_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    }
                } else {
                    KW_ASSERT(
                        next_attachment_access == AttachmentAccess::NONE,
                        "Attachment access without READ or WRITE flags must be equal to NONE."
                    );

                    // This is the last attachment access on this frame.
                    attachment_barrier_data.destination_access_mask = VK_ACCESS_NONE_KHR;
                    attachment_barrier_data.destination_pipeline_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                }

                next_attachment_access = attachment_access;
            }
        }

        //
        // Compute destination access mask & source pipeline stage for NONE render passes.
        //

        VkAccessFlags next_access_mask = VK_ACCESS_NONE_KHR;
        VkPipelineStageFlags next_pipeline_stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        for (size_t render_pass_index = frame_graph_descriptor.render_pass_descriptor_count; render_pass_index > 0; render_pass_index--) {
            size_t access_index = (render_pass_index - 1) * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if (attachment_access == AttachmentAccess::NONE) {
                attachment_barrier_data.source_access_mask = next_access_mask;
                attachment_barrier_data.source_pipeline_stage_mask = next_pipeline_stage_mask;
            } else {
                next_access_mask = attachment_barrier_data.destination_access_mask;
                next_pipeline_stage_mask = attachment_barrier_data.destination_pipeline_stage_mask;
            }

            KW_ASSERT(attachment_barrier_data.destination_pipeline_stage_mask != VK_PIPELINE_STAGE_NONE_KHR);
        }
    }
}

void FrameGraphVulkan::compute_parallel_block_indices(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );
    
    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        m_render_pass_data.empty(),
        "Parallel block indices are computed twice."
    );

    m_render_pass_data.reserve(frame_graph_descriptor.render_pass_descriptor_count);

    // Keep accesses to each attachment in current parallel block. Once they conflict, move attachment to a new parallel block.
    Vector<AttachmentAccess> previous_accesses(m_attachment_descriptors.size(), m_render.transient_memory_resource);
    uint32_t parallel_block_index = 0;

    for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
        for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            AttachmentAccess previous_access = previous_accesses[attachment_index];
            AttachmentAccess current_access = create_context.attachment_access_matrix[access_index];

            if (((current_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE && previous_access != AttachmentAccess::NONE) ||
                (current_access != AttachmentAccess::NONE && (previous_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE))
            {
                std::fill(previous_accesses.begin(), previous_accesses.end(), AttachmentAccess::NONE);
                parallel_block_index++;
                break;
            }
        }

        RenderPassData render_pass_data(m_render.persistent_memory_resource);
        render_pass_data.parallel_block_index = parallel_block_index;
        m_render_pass_data.push_back(std::move(render_pass_data));

        for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            AttachmentAccess& previous_access = previous_accesses[attachment_index];
            AttachmentAccess current_access = create_context.attachment_access_matrix[access_index];

            if (previous_access == AttachmentAccess::NONE) {
                previous_access = current_access;
            } else {
                // Not possible otherwise because when this kind of conflict happens,
                // previous loop clears the `previous_accesses` array.
                KW_ASSERT(current_access == AttachmentAccess::NONE || previous_access == current_access);
            }
        }
    }
}

void FrameGraphVulkan::compute_parallel_blocks(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_barrier_matrix.empty(),
        "Attachments barrier matrix must be computed first."
    );

    KW_ASSERT(
        !m_render_pass_data.empty(),
        "Render pass parallel blocks must be computed first."
    );

    KW_ASSERT(
        m_parallel_block_data.empty(),
        "Parallel block data computed twice."
    );

    // If there's no render passes, there's no parallel blocks too.
    m_parallel_block_data.resize(m_render_pass_data.empty() ? 0 : m_render_pass_data.back().parallel_block_index + 1);

    for (size_t render_pass_index = 0; render_pass_index < m_render_pass_data.size(); render_pass_index++) {
        RenderPassData& render_pass_data = m_render_pass_data[render_pass_index];
        KW_ASSERT(render_pass_data.parallel_block_index < m_parallel_block_data.size());

        ParallelBlockData& parallel_block_data = m_parallel_block_data[render_pass_data.parallel_block_index];

        for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
            const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];

            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
            KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
            AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

            if (attachment_access != AttachmentAccess::NONE) {
                parallel_block_data.source_stage_mask |= attachment_barrier_data.source_pipeline_stage_mask;
                parallel_block_data.destination_stage_mask |= attachment_barrier_data.destination_pipeline_stage_mask;
                parallel_block_data.source_access_mask |= attachment_barrier_data.source_access_mask;
                parallel_block_data.destination_access_mask |= attachment_barrier_data.destination_access_mask;
            }
        }
    }
}

void FrameGraphVulkan::compute_attachment_ranges(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        !m_render_pass_data.empty(),
        "Render pass parallel blocks must be computed first."
    );

    KW_ASSERT(
        m_attachment_data.empty(),
        "Attachment ranges are computed twice."
    );

    m_attachment_data.resize(m_attachment_descriptors.size(), AttachmentData(m_render.persistent_memory_resource));

    for (size_t attachment_index = 0; attachment_index < m_attachment_data.size(); attachment_index++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentData& attachment_data = m_attachment_data[attachment_index];

        // Load attachments must be never aliased.
        if (!frame_graph_descriptor.is_aliasing_enabled || attachment_descriptor.load_op == LoadOp::LOAD) {
            attachment_data.min_parallel_block_index = 0;
            attachment_data.max_parallel_block_index = m_render_pass_data.back().parallel_block_index;
        } else {
            uint32_t min_render_pass_index = UINT32_MAX;
            uint32_t max_render_pass_index = 0;

            for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
                size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
                KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                if ((create_context.attachment_access_matrix[access_index] & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                    min_render_pass_index = std::min(min_render_pass_index, static_cast<uint32_t>(render_pass_index));
                    max_render_pass_index = std::max(max_render_pass_index, static_cast<uint32_t>(render_pass_index));
                }
            }

            if (min_render_pass_index == UINT32_MAX) {
                // This is rather a weird scenario, this attachment is never written. Avoid aliasing such attachment
                // because there's no render pass that would convert its layout from `VK_IMAGE_LAYOUT_UNDEFINED` to
                // `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
                attachment_data.min_parallel_block_index = 0;
                attachment_data.max_parallel_block_index = m_render_pass_data.back().parallel_block_index;
            } else {
                uint32_t previous_read_render_pass_index = UINT32_MAX;

                for (size_t offset = frame_graph_descriptor.render_pass_descriptor_count; offset > 0; offset--) {
                    size_t render_pass_index = (min_render_pass_index + offset) % frame_graph_descriptor.render_pass_descriptor_count;

                    size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
                    KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

                    if ((create_context.attachment_access_matrix[access_index] & AttachmentAccess::READ) == AttachmentAccess::READ) {
                        previous_read_render_pass_index = static_cast<uint32_t>(render_pass_index);
                        break;
                    }
                }

                if (previous_read_render_pass_index != UINT32_MAX) {
                    if (previous_read_render_pass_index > min_render_pass_index) {
                        // Previous read render pass was on previous frame.
                        // Compute non-looped range 000011110000 where min <= max.

                        max_render_pass_index = std::max(max_render_pass_index, previous_read_render_pass_index);
                        KW_ASSERT(m_render_pass_data[min_render_pass_index].parallel_block_index <= m_render_pass_data[max_render_pass_index].parallel_block_index);
                    } else {
                        // Previous read render pass was on the same frame before first write render pass.
                        // Compute looped range 111100001111 where min > max.
                        
                        // Previous read render pass parallel index is always less than first write render pass's
                        // parallel index (so we won't face min = max meaning all render pass range).

                        max_render_pass_index = previous_read_render_pass_index;
                        KW_ASSERT(m_render_pass_data[min_render_pass_index].parallel_block_index > m_render_pass_data[max_render_pass_index].parallel_block_index);
                    }
                }

                attachment_data.min_parallel_block_index = m_render_pass_data[min_render_pass_index].parallel_block_index;
                attachment_data.max_parallel_block_index = m_render_pass_data[max_render_pass_index].parallel_block_index;
            }
        }
    }
}

void FrameGraphVulkan::compute_attachment_usage_mask(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        !m_attachment_data.empty(),
        "Attachment ranges must be computed first."
    );

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentData& attachment_data = m_attachment_data[attachment_index];

        if (attachment_descriptor.is_blit_source) {
            attachment_data.usage_mask |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                    if ((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT) {
                        attachment_data.usage_mask |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    }

                    if ((attachment_access & (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER)) != AttachmentAccess::NONE) {
                        attachment_data.usage_mask |= VK_IMAGE_USAGE_SAMPLED_BIT;
                    }
                } else {
                    KW_ASSERT((attachment_access & (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER)) != AttachmentAccess::NONE);

                    attachment_data.usage_mask |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }
            } else if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                KW_ASSERT((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT);

                if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                    attachment_data.usage_mask |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                } else {
                    attachment_data.usage_mask |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }
            }
        }
    }
}

void FrameGraphVulkan::compute_attachment_layouts(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        !m_attachment_data.empty(),
        "Attachment ranges must be computed first."
    );

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentData& attachment_data = m_attachment_data[attachment_index];

        for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
            size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
            KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

            AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];

            if ((attachment_access & AttachmentAccess::READ) == AttachmentAccess::READ) {
                if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                    if ((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT) {
                        KW_ASSERT((attachment_access & AttachmentAccess::LOAD) == AttachmentAccess::LOAD);

                        attachment_data.initial_access_mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    }

                    if ((attachment_access & (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER)) != AttachmentAccess::NONE) {
                        attachment_data.initial_access_mask |= VK_ACCESS_SHADER_READ_BIT;
                    }

                    if ((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT) {
                        attachment_data.initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                    } else {
                        attachment_data.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }
                } else {
                    KW_ASSERT((attachment_access & (AttachmentAccess::VERTEX_SHADER | AttachmentAccess::FRAGMENT_SHADER)) != AttachmentAccess::NONE);

                    attachment_data.initial_access_mask = VK_ACCESS_SHADER_READ_BIT;
                    attachment_data.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                break;
            } else if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
                KW_ASSERT((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT);

                if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
                    if ((attachment_access & AttachmentAccess::LOAD) == AttachmentAccess::LOAD) {
                        attachment_data.initial_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    } else {
                        attachment_data.initial_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    }

                    attachment_data.initial_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                } else {
                    if ((attachment_access & (AttachmentAccess::LOAD | AttachmentAccess::BLEND)) != AttachmentAccess::NONE) {
                        attachment_data.initial_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
                    } else {
                        attachment_data.initial_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    }

                    attachment_data.initial_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
                break;
            }
        }
    }
}

void FrameGraphVulkan::compute_attachment_blit_data(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !m_attachment_data.empty(),
        "Attachment ranges must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_barrier_matrix.empty(),
        "Attachments barrier matrix must be computed first."
    );

    for (size_t attachment_index = 0; attachment_index < m_attachment_descriptors.size(); attachment_index++) {
        AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        AttachmentData& attachment_data = m_attachment_data[attachment_index];

        if (attachment_descriptor.is_blit_source) {
            KW_ASSERT(attachment_data.blit_data.empty());
            attachment_data.blit_data.reserve(frame_graph_descriptor.render_pass_descriptor_count);

            for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
                size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
                KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

                attachment_data.blit_data.push_back(create_context.attachment_barrier_matrix[access_index]);
            }
        }
    }
}

void FrameGraphVulkan::create_render_passes(CreateContext& create_context) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    for (size_t render_pass_index = 0; render_pass_index < frame_graph_descriptor.render_pass_descriptor_count; render_pass_index++) {
        const RenderPassDescriptor& render_pass_descriptor = frame_graph_descriptor.render_pass_descriptors[render_pass_index];
        RenderPassData& render_pass_data = m_render_pass_data[render_pass_index];

        create_render_pass(create_context, static_cast<uint32_t>(render_pass_index));

        KW_ASSERT(render_pass_data.graphics_pipeline_data.empty());
        render_pass_data.graphics_pipeline_data.reserve(render_pass_descriptor.graphics_pipeline_descriptor_count);

        for (size_t graphics_pipeline_index = 0; graphics_pipeline_index < render_pass_descriptor.graphics_pipeline_descriptor_count; graphics_pipeline_index++) {
            create_context.graphics_pipeline_memory_resource.reset();

            create_graphics_pipeline(create_context, static_cast<uint32_t>(render_pass_index), static_cast<uint32_t>(graphics_pipeline_index));
        }
    }
}

void FrameGraphVulkan::create_render_pass(CreateContext& create_context, uint32_t render_pass_index) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;

    KW_ASSERT(
        !m_attachment_descriptors.empty(),
        "Attachments descriptors must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_mapping.empty(),
        "Attachments mapping must be computed first."
    );
    
    KW_ASSERT(
        !create_context.attachment_access_matrix.empty(),
        "Attachments access matrix must be computed first."
    );

    KW_ASSERT(
        !create_context.attachment_barrier_matrix.empty(),
        "Attachments barrier matrix must be computed first."
    );

    KW_ASSERT(
        !m_attachment_data.empty(),
        "Attachment ranges must be computed first."
    );

    KW_ASSERT(
        render_pass_index < m_render_pass_data.size(),
        "Render pass data must be initialized first."
    );

    const RenderPassDescriptor& render_pass_descriptor = frame_graph_descriptor.render_pass_descriptors[render_pass_index];
    RenderPassData& render_pass_data = m_render_pass_data[render_pass_index];

    //
    // Compute the total number of attachments in this render pass.
    //

    size_t attachment_count = render_pass_descriptor.color_attachment_name_count;
    if (render_pass_descriptor.depth_stencil_attachment_name != nullptr) {
        attachment_count++;
    }

    //
    // Compute attachment descriptions: load and store operations, initial and final layouts.
    //

    Vector<VkAttachmentDescription> attachment_descriptions(attachment_count, m_render.transient_memory_resource);

    KW_ASSERT(
        render_pass_data.attachment_indices.empty(),
        "Attachment indices are computed twice."
    );

    render_pass_data.attachment_indices.resize(attachment_count);

    for (size_t i = 0; i < attachment_descriptions.size(); i++) {
        uint32_t attachment_index;

        if (i == render_pass_descriptor.color_attachment_name_count) {
            KW_ASSERT(create_context.attachment_mapping.count(render_pass_descriptor.depth_stencil_attachment_name) > 0);
            attachment_index = create_context.attachment_mapping[render_pass_descriptor.depth_stencil_attachment_name];
            KW_ASSERT(attachment_index < m_attachment_descriptors.size());
        } else {
            KW_ASSERT(create_context.attachment_mapping.count(render_pass_descriptor.color_attachment_names[i]) > 0);
            attachment_index = create_context.attachment_mapping[render_pass_descriptor.color_attachment_names[i]];
            KW_ASSERT(attachment_index < m_attachment_descriptors.size());
        }

        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
        VkAttachmentDescription& attachment_description = attachment_descriptions[i];

        attachment_description.flags = 0;
        attachment_description.format = TextureFormatUtils::convert_format_vulkan(attachment_descriptor.format);
        attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;

        size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
        KW_ASSERT(access_index < create_context.attachment_access_matrix.size());
        KW_ASSERT(access_index < create_context.attachment_barrier_matrix.size());

        AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];
        AttachmentBarrierData& attachment_barrier_data = create_context.attachment_barrier_matrix[access_index];

        if ((attachment_access & AttachmentAccess::LOAD) == AttachmentAccess::NONE) {
            attachment_description.loadOp = LOAD_OP_MAPPING[static_cast<size_t>(attachment_descriptor.load_op)];
        } else {
            attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }

        if ((attachment_access & AttachmentAccess::STORE) == AttachmentAccess::STORE) {
            attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        } else {
            if (attachment_index == 0) {
                // Store swapchain image for present.
                attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            } else {
                attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
        }

        attachment_description.stencilLoadOp = attachment_description.loadOp;
        attachment_description.stencilStoreOp = attachment_description.storeOp;

        attachment_description.initialLayout = attachment_barrier_data.source_image_layout;
        attachment_description.finalLayout = attachment_barrier_data.destination_image_layout;

        KW_ASSERT(render_pass_data.attachment_indices[i] == 0);
        render_pass_data.attachment_indices[i] = attachment_index;
    }

    //
    // Set up attachment references.
    //

    Vector<VkAttachmentReference> color_attachment_references(render_pass_descriptor.color_attachment_name_count, m_render.transient_memory_resource);
    for (size_t i = 0; i < color_attachment_references.size(); i++) {
        color_attachment_references[i].attachment = i;
        color_attachment_references[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depth_stencil_attachment_reference{};
    depth_stencil_attachment_reference.attachment = static_cast<uint32_t>(render_pass_descriptor.color_attachment_name_count);
    depth_stencil_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    // Check whether depth stencil attachment is actually written by this render pass.
    if (render_pass_descriptor.depth_stencil_attachment_name != nullptr) {
        KW_ASSERT(create_context.attachment_mapping.count(render_pass_descriptor.depth_stencil_attachment_name) > 0);
        uint32_t attachment_index = create_context.attachment_mapping[render_pass_descriptor.depth_stencil_attachment_name];
        KW_ASSERT(attachment_index < m_attachment_descriptors.size());

        size_t access_index = render_pass_index * m_attachment_descriptors.size() + attachment_index;
        KW_ASSERT(access_index < create_context.attachment_access_matrix.size());

        AttachmentAccess attachment_access = create_context.attachment_access_matrix[access_index];

        if ((attachment_access & AttachmentAccess::WRITE) == AttachmentAccess::WRITE) {
            KW_ASSERT((attachment_access & AttachmentAccess::ATTACHMENT) == AttachmentAccess::ATTACHMENT);

            depth_stencil_attachment_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }

    //
    // Set up subpass and create the render pass.
    //

    VkSubpassDescription subpass_description{};
    subpass_description.flags = 0;
    subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.inputAttachmentCount = 0;
    subpass_description.pInputAttachments = nullptr;
    subpass_description.colorAttachmentCount = static_cast<uint32_t>(color_attachment_references.size());
    subpass_description.pColorAttachments = color_attachment_references.data();
    subpass_description.pResolveAttachments = nullptr;
    if (render_pass_descriptor.depth_stencil_attachment_name != nullptr) {
        subpass_description.pDepthStencilAttachment = &depth_stencil_attachment_reference;
    }
    subpass_description.preserveAttachmentCount = 0;
    subpass_description.pPreserveAttachments = nullptr;

    VkRenderPassCreateInfo render_pass_create_info{};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.flags = 0;
    render_pass_create_info.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
    render_pass_create_info.pAttachments = attachment_descriptions.data();
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass_description;
    render_pass_create_info.dependencyCount = 0;
    render_pass_create_info.pDependencies = nullptr;

    KW_ASSERT(render_pass_data.render_pass == VK_NULL_HANDLE);
    VK_ERROR(
        vkCreateRenderPass(m_render.device, &render_pass_create_info, &m_render.allocation_callbacks, &render_pass_data.render_pass),
        "Failed to create render pass \"%s\".", render_pass_descriptor.name
    );
    VK_NAME(m_render, render_pass_data.render_pass, "Render pass \"%s\"", render_pass_descriptor.name);

    //
    // Create render pass impl and pass it to an actual render pass.
    //

    render_pass_data.render_pass_impl = allocate_unique<RenderPassImplVulkan>(m_render.persistent_memory_resource, *this, render_pass_index);

    get_render_pass_impl(render_pass_descriptor.render_pass) = render_pass_data.render_pass_impl.get();
}

void FrameGraphVulkan::create_graphics_pipeline(CreateContext& create_context, uint32_t render_pass_index, uint32_t graphics_pipeline_index) {
    const FrameGraphDescriptor& frame_graph_descriptor = create_context.frame_graph_descriptor;
    const RenderPassDescriptor& render_pass_descriptor = frame_graph_descriptor.render_pass_descriptors[render_pass_index];
    const GraphicsPipelineDescriptor& graphics_pipeline_descriptor = render_pass_descriptor.graphics_pipeline_descriptors[graphics_pipeline_index];

    RenderPassData& render_pass_data = m_render_pass_data[render_pass_index];
    GraphicsPipelineData graphics_pipeline_data(m_render.persistent_memory_resource);

    const VkPhysicalDeviceLimits& limits = m_render.physical_device_properties.limits;

    //
    // Calculate the number of pipeline stages.
    //

    uint32_t stage_count = 0;
    
    /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
        stage_count++;
    }

    if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
        stage_count++;
    }

    //
    // Read shaders from file system and query their reflection.
    //

    SpvReflectShaderModule vertex_shader_reflection;
    SpvReflectShaderModule fragment_shader_reflection;

    SpvAllocator spv_allocator{};
    spv_allocator.calloc = &spv_calloc;
    spv_allocator.free = &spv_free;
    spv_allocator.context = &create_context.graphics_pipeline_memory_resource;

    /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
        String relative_path(graphics_pipeline_descriptor.vertex_shader_filename, create_context.graphics_pipeline_memory_resource);
        KW_ERROR(relative_path.find(".hlsl") != String::npos, "Shader file \"%s\" must have .hlsl extention.", graphics_pipeline_descriptor.vertex_shader_filename);

        relative_path.replace(relative_path.find(".hlsl"), 5, ".spv");

        std::ifstream file(relative_path.c_str(), std::ios::binary | std::ios::ate);
        KW_ERROR(file, "Failed to open shader file \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename);

        std::streamsize size = file.tellg();
        KW_ERROR(size >= 0, "Failed to query shader file size \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename);

        file.seekg(0, std::ios::beg);

        Vector<char> shader_data(size, create_context.graphics_pipeline_memory_resource);

        KW_ERROR(
            file.read(shader_data.data(), size),
            "Failed to read shader file \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
        );

        SPV_ERROR(
            spvReflectCreateShaderModule(shader_data.size(), shader_data.data(), &vertex_shader_reflection, &spv_allocator),
            "Failed to create shader module from \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
        );

        KW_ERROR(
            spvReflectGetEntryPoint(&vertex_shader_reflection, "main") != nullptr,
            "Shader \"%s\" must have entry point \"main\".", graphics_pipeline_descriptor.vertex_shader_filename
        );
    }

    if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
        String relative_path(graphics_pipeline_descriptor.fragment_shader_filename, create_context.graphics_pipeline_memory_resource);
        KW_ERROR(relative_path.find(".hlsl") != String::npos, "Shader file \"%s\" must have .hlsl extention.", graphics_pipeline_descriptor.fragment_shader_filename);

        relative_path.replace(relative_path.find(".hlsl"), 5, ".spv");

        std::ifstream file(relative_path.c_str(), std::ios::binary | std::ios::ate);
        KW_ERROR(file, "Failed to open shader file \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename);

        std::streamsize size = file.tellg();
        KW_ERROR(size >= 0, "Failed to query shader file size \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename);

        file.seekg(0, std::ios::beg);

        Vector<char> shader_data(size, create_context.graphics_pipeline_memory_resource);

        KW_ERROR(
            file.read(shader_data.data(), size),
            "Failed to read shader file \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
        );

        SPV_ERROR(
            spvReflectCreateShaderModule(shader_data.size(), shader_data.data(), &fragment_shader_reflection, &spv_allocator),
            "Failed to create shader module from \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
        );

        KW_ERROR(
            spvReflectGetEntryPoint(&fragment_shader_reflection, "main") != nullptr,
            "Shader \"%s\" must have entry point \"main\".", graphics_pipeline_descriptor.fragment_shader_filename
        );
    }

    //
    // We're about to reassign descriptor binding numbers and fill the descriptor set at the same time.
    //

    Vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings(create_context.graphics_pipeline_memory_resource);
    descriptor_set_layout_bindings.reserve(
        graphics_pipeline_descriptor.uniform_attachment_descriptor_count +
        graphics_pipeline_descriptor.uniform_buffer_descriptor_count +
        graphics_pipeline_descriptor.uniform_texture_descriptor_count +
        graphics_pipeline_descriptor.uniform_sampler_descriptor_count
    );

    uint32_t current_binding = 0;
    uint32_t vertex_shader_binding_count = 0;
    uint32_t fragment_shader_binding_count = 0;
    
    //
    // Uniforms attachments.
    //

    KW_ASSERT(graphics_pipeline_data.uniform_attachment_descriptor_count == 0);

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_attachment_descriptor_count; i++) {
        graphics_pipeline_data.uniform_attachment_descriptor_count += std::max(graphics_pipeline_descriptor.uniform_attachment_descriptors[i].count, 1U);
    }

    KW_ASSERT(graphics_pipeline_data.uniform_attachment_indices.empty());
    graphics_pipeline_data.uniform_attachment_indices.reserve(graphics_pipeline_data.uniform_attachment_descriptor_count);

    KW_ASSERT(graphics_pipeline_data.uniform_attachment_mapping.empty());
    graphics_pipeline_data.uniform_attachment_mapping.reserve(graphics_pipeline_data.uniform_attachment_descriptor_count);

    uint32_t flattened_uniform_attachment_index = 0;

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_attachment_descriptor_count; i++) {
        const UniformAttachmentDescriptor& uniform_attachment_descriptor = graphics_pipeline_descriptor.uniform_attachment_descriptors[i];
        uint32_t uniform_attachment_count = std::max(uniform_attachment_descriptor.count, 1U);

        KW_ASSERT(create_context.attachment_mapping.count(uniform_attachment_descriptor.attachment_name) > 0);
        uint32_t attachment_index = create_context.attachment_mapping[uniform_attachment_descriptor.attachment_name];
        KW_ASSERT(attachment_index < m_attachment_descriptors.size());

        VkShaderStageFlags shader_stage_flags = SHADER_VISILITY_MAPPING[static_cast<size_t>(uniform_attachment_descriptor.visibility)];

        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            if ((shader_stage_flags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&vertex_shader_reflection, uniform_attachment_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        "Descriptor binding \"%s\" is expected to have \"Texture2D\" type in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    KW_ERROR(
                        descriptor_binding->image.dim == SpvDim2D,
                        "Descriptor binding \"%s\" is expected to be a \"Texture2D\" in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    uint32_t count = 1;

                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_attachment_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&vertex_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    vertex_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Uniform attachment \"%s\" is not found in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_VERTEX_BIT;
                }
            }
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            if ((shader_stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&fragment_shader_reflection, uniform_attachment_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        "Shader variable \"%s\" is expected to be a \"Texture2D\" in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    KW_ERROR(
                        descriptor_binding->image.dim == SpvDim2D,
                        "Shader variable \"%s\" is expected to be a \"Texture2D\" in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    uint32_t count = 1;

                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_attachment_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&fragment_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    fragment_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Uniform attachment \"%s\" is not found in \"%s\".", uniform_attachment_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
            }
        }

        if (shader_stage_flags != 0) {
            VkDescriptorSetLayoutBinding descriptor_set_layout_binding{};
            descriptor_set_layout_binding.binding = current_binding++;
            descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptor_set_layout_binding.descriptorCount = uniform_attachment_count;
            descriptor_set_layout_binding.stageFlags = shader_stage_flags;
            descriptor_set_layout_binding.pImmutableSamplers = nullptr;
            descriptor_set_layout_bindings.push_back(descriptor_set_layout_binding);

            graphics_pipeline_data.uniform_attachment_indices.insert(graphics_pipeline_data.uniform_attachment_indices.end(), uniform_attachment_count, attachment_index);

            for (uint32_t j = 0; j < uniform_attachment_count; j++) {
                KW_ASSERT(flattened_uniform_attachment_index + j < graphics_pipeline_data.uniform_attachment_descriptor_count);
                graphics_pipeline_data.uniform_attachment_mapping.push_back(flattened_uniform_attachment_index + j);
            }
        }

        flattened_uniform_attachment_index += uniform_attachment_count;
    }

    KW_ASSERT(graphics_pipeline_data.uniform_attachment_indices.size() == graphics_pipeline_data.uniform_attachment_mapping.size());

    //
    // Uniforms textures.
    //

    KW_ASSERT(graphics_pipeline_data.uniform_texture_descriptor_count == 0);

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_texture_descriptor_count; i++) {
        graphics_pipeline_data.uniform_texture_descriptor_count += std::max(graphics_pipeline_descriptor.uniform_texture_descriptors[i].count, 1U);
    }

    KW_ERROR(
        graphics_pipeline_data.uniform_attachment_descriptor_count + graphics_pipeline_data.uniform_texture_descriptor_count <= m_uniform_texture_count_per_descriptor_pool,
        "The number of image descriptors in graphics pipeline is greater than the number of image descriptors in descriptor pool."
    );

    KW_ASSERT(graphics_pipeline_data.uniform_texture_first_binding == 0);
    graphics_pipeline_data.uniform_texture_first_binding = current_binding;

    KW_ASSERT(graphics_pipeline_data.uniform_texture_mapping.empty());
    graphics_pipeline_data.uniform_texture_mapping.reserve(graphics_pipeline_data.uniform_texture_descriptor_count);

    uint32_t flattened_uniform_texture_index = 0;

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_texture_descriptor_count; i++) {
        const UniformTextureDescriptor& uniform_texture_descriptor = graphics_pipeline_descriptor.uniform_texture_descriptors[i];
        uint32_t uniform_texture_count = std::max(uniform_texture_descriptor.count, 1U);

        VkShaderStageFlags shader_stage_flags = SHADER_VISILITY_MAPPING[static_cast<size_t>(uniform_texture_descriptor.visibility)];

        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            if ((shader_stage_flags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&vertex_shader_reflection, uniform_texture_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        "Descriptor binding \"%s\" is expected to be a texture in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    switch (uniform_texture_descriptor.texture_type) {
                    case TextureType::TEXTURE_2D:
                    case TextureType::TEXTURE_2D_ARRAY:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDim2D,
                            "Shader variable \"%s\" is expected to be a \"Texture2D\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                        );
                        break;
                    case TextureType::TEXTURE_3D:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDim3D,
                            "Shader variable \"%s\" is expected to be a \"Texture3D\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                        );
                        break;
                    case TextureType::TEXTURE_CUBE:
                    case TextureType::TEXTURE_CUBE_ARRAY:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDimCube,
                            "Shader variable \"%s\" is expected to be a \"TextureCube\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                        );
                        break;
                    }

                    if (uniform_texture_descriptor.texture_type == TextureType::TEXTURE_2D_ARRAY || uniform_texture_descriptor.texture_type == TextureType::TEXTURE_CUBE_ARRAY) {
                        KW_ERROR(
                            descriptor_binding->image.arrayed == 1,
                            "Shader variable \"%s\" is expected to be an array in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                        );
                    } else {
                        KW_ERROR(
                            descriptor_binding->image.arrayed == 0,
                            "Shader variable \"%s\" is expected to be not an array in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                        );
                    }

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_texture_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&vertex_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    vertex_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Texture \"%s\" is not found in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_VERTEX_BIT;
                }
            }
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            if ((shader_stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&fragment_shader_reflection, uniform_texture_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        "Descriptor binding \"%s\" is expected to be a texture in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    switch (uniform_texture_descriptor.texture_type) {
                    case TextureType::TEXTURE_2D:
                    case TextureType::TEXTURE_2D_ARRAY:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDim2D,
                            "Shader variable \"%s\" is expected to be a \"Texture2D\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                        );
                        break;
                    case TextureType::TEXTURE_3D:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDim3D,
                            "Shader variable \"%s\" is expected to be a \"Texture3D\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                        );
                        break;
                    case TextureType::TEXTURE_CUBE:
                    case TextureType::TEXTURE_CUBE_ARRAY:
                        KW_ERROR(
                            descriptor_binding->image.dim == SpvDimCube,
                            "Shader variable \"%s\" is expected to be a \"TextureCube\" in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                        );
                        break;
                    }

                    if (uniform_texture_descriptor.texture_type == TextureType::TEXTURE_2D_ARRAY || uniform_texture_descriptor.texture_type == TextureType::TEXTURE_CUBE_ARRAY) {
                        KW_ERROR(
                            descriptor_binding->image.arrayed == 1,
                            "Shader variable \"%s\" is expected to be an array in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                        );
                    } else {
                        KW_ERROR(
                            descriptor_binding->image.arrayed == 0,
                            "Shader variable \"%s\" is expected to be not an array in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                        );
                    }

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_texture_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&fragment_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    fragment_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Texture \"%s\" is not found in \"%s\".", uniform_texture_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
            }
        }

        if (shader_stage_flags != 0) {
            VkDescriptorSetLayoutBinding descriptor_set_layout_binding{};
            descriptor_set_layout_binding.binding = current_binding++;
            descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptor_set_layout_binding.descriptorCount = uniform_texture_count;
            descriptor_set_layout_binding.stageFlags = shader_stage_flags;
            descriptor_set_layout_binding.pImmutableSamplers = nullptr;
            descriptor_set_layout_bindings.push_back(descriptor_set_layout_binding);

            for (uint32_t j = 0; j < uniform_texture_count; j++) {
                KW_ASSERT(flattened_uniform_texture_index + j < graphics_pipeline_data.uniform_texture_descriptor_count);
                graphics_pipeline_data.uniform_texture_mapping.push_back(flattened_uniform_texture_index + j);
            }
        }

        flattened_uniform_texture_index += uniform_texture_count;
    }

    //
    // Uniforms samplers.
    //

    KW_ERROR(
        graphics_pipeline_descriptor.uniform_sampler_descriptor_count <= m_uniform_sampler_count_per_descriptor_pool,
        "The number of sampler descriptors in graphics pipeline is greater than the number of sampler descriptors in descriptor pool."
    );

    KW_ASSERT(graphics_pipeline_data.uniform_samplers.empty());
    graphics_pipeline_data.uniform_samplers.resize(graphics_pipeline_descriptor.uniform_sampler_descriptor_count);

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_sampler_descriptor_count; i++) {
        const UniformSamplerDescriptor& uniform_sampler_descriptor = graphics_pipeline_descriptor.uniform_sampler_descriptors[i];

        VkSamplerCreateInfo sampler_create_info{};
        sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.flags = 0;
        sampler_create_info.magFilter = FILTER_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.mag_filter)];
        sampler_create_info.minFilter = FILTER_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.min_filter)];
        sampler_create_info.mipmapMode = MIP_FILTER_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.mip_filter)];
        sampler_create_info.addressModeU = ADDRESS_MODE_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.address_mode_u)];
        sampler_create_info.addressModeV = ADDRESS_MODE_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.address_mode_v)];
        sampler_create_info.addressModeW = ADDRESS_MODE_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.address_mode_w)];
        sampler_create_info.mipLodBias = std::min(uniform_sampler_descriptor.mip_lod_bias, limits.maxSamplerLodBias);
        sampler_create_info.anisotropyEnable = uniform_sampler_descriptor.anisotropy_enable;
        sampler_create_info.maxAnisotropy = std::min(uniform_sampler_descriptor.max_anisotropy, limits.maxSamplerAnisotropy);
        sampler_create_info.compareEnable = uniform_sampler_descriptor.compare_enable;
        sampler_create_info.compareOp = COMPARE_OP_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.compare_op)];
        sampler_create_info.minLod = uniform_sampler_descriptor.min_lod;
        sampler_create_info.maxLod = uniform_sampler_descriptor.max_lod;
        sampler_create_info.borderColor = BORDER_COLOR_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.border_color)];
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;

        KW_ASSERT(graphics_pipeline_data.uniform_samplers[i] == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateSampler(m_render.device, &sampler_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.uniform_samplers[i]),
            "Failed to create sampler \"%s\".", uniform_sampler_descriptor.variable_name
        );
        VK_NAME(
            m_render, graphics_pipeline_data.uniform_samplers[i],
            "Sampler \"%s\"", uniform_sampler_descriptor.variable_name
        );

        VkShaderStageFlags shader_stage_flags = SHADER_VISILITY_MAPPING[static_cast<size_t>(uniform_sampler_descriptor.visibility)];

        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            if ((shader_stage_flags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&vertex_shader_reflection, uniform_sampler_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,
                        "Descriptor binding \"%s\" is expected to be a sampler in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        count == 1,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&vertex_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    vertex_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Sampler \"%s\" is not found in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_VERTEX_BIT;
                }
            }
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            if ((shader_stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&fragment_shader_reflection, uniform_sampler_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER,
                        "Descriptor binding \"%s\" is expected to be a sampler in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        count == 1,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&fragment_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    fragment_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Sampler \"%s\" is not found in \"%s\".", uniform_sampler_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
            }
        }

        if (shader_stage_flags != 0) {
            VkDescriptorSetLayoutBinding descriptor_set_layout_binding{};
            descriptor_set_layout_binding.binding = current_binding++;
            descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            descriptor_set_layout_binding.descriptorCount = 1;
            descriptor_set_layout_binding.stageFlags = shader_stage_flags;
            descriptor_set_layout_binding.pImmutableSamplers = &graphics_pipeline_data.uniform_samplers[i];
            descriptor_set_layout_bindings.push_back(descriptor_set_layout_binding);
        }
    }

    //
    // Uniform buffers.
    //

    KW_ASSERT(graphics_pipeline_data.uniform_buffer_descriptor_count == 0);

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_buffer_descriptor_count; i++) {
        graphics_pipeline_data.uniform_buffer_descriptor_count += std::max(graphics_pipeline_descriptor.uniform_buffer_descriptors[i].count, 1U);
    }

    KW_ERROR(
        graphics_pipeline_data.uniform_buffer_descriptor_count <= m_uniform_buffer_count_per_descriptor_pool,
        "The number of image descriptors in graphics pipeline is greater than the number of image descriptors in descriptor pool."
    );

    KW_ASSERT(graphics_pipeline_data.uniform_buffer_first_binding == 0);
    graphics_pipeline_data.uniform_buffer_first_binding = current_binding;

    KW_ASSERT(graphics_pipeline_data.uniform_buffer_mapping.empty());
    graphics_pipeline_data.uniform_buffer_mapping.reserve(graphics_pipeline_data.uniform_buffer_descriptor_count);

    KW_ASSERT(graphics_pipeline_data.uniform_buffer_sizes.empty());
    graphics_pipeline_data.uniform_buffer_sizes.reserve(graphics_pipeline_data.uniform_buffer_descriptor_count);

    uint32_t flattened_uniform_buffer_index = 0;

    for (size_t i = 0; i < graphics_pipeline_descriptor.uniform_buffer_descriptor_count; i++) {
        const UniformBufferDescriptor& uniform_buffer_descriptor = graphics_pipeline_descriptor.uniform_buffer_descriptors[i];
        uint32_t uniform_buffer_count = std::max(uniform_buffer_descriptor.count, 1U);

        VkShaderStageFlags shader_stage_flags = SHADER_VISILITY_MAPPING[static_cast<size_t>(uniform_buffer_descriptor.visibility)];

        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            if ((shader_stage_flags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&vertex_shader_reflection, uniform_buffer_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        "Descriptor binding \"%s\" is expected to be an uniform buffer in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    KW_ERROR(
                        descriptor_binding->block.size == uniform_buffer_descriptor.size,
                        "Descriptor binding \"%s\" has mismatching size in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_buffer_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&vertex_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    vertex_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Uniform buffer \"%s\" is not found in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.vertex_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_VERTEX_BIT;
                }
            }
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            if ((shader_stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT) {
                const SpvReflectDescriptorBinding* descriptor_binding =
                    spvReflectGetDescriptorBindingByName(&fragment_shader_reflection, uniform_buffer_descriptor.variable_name, nullptr);

                if (descriptor_binding != nullptr) {
                    KW_ERROR(
                        descriptor_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        "Descriptor binding \"%s\" is expected to be an uniform buffer in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    KW_ERROR(
                        descriptor_binding->block.size == uniform_buffer_descriptor.size,
                        "Descriptor binding \"%s\" has mismatching size in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    uint32_t count = 1;
                    for (uint32_t j = 0; j < descriptor_binding->array.dims_count; j++) {
                        count *= descriptor_binding->array.dims[j];
                    }

                    KW_ERROR(
                        uniform_buffer_count == count,
                        "Descriptor binding \"%s\" has mismatching array size in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    SPV_ERROR(
                        spvReflectChangeDescriptorBindingNumbers(&fragment_shader_reflection, descriptor_binding, current_binding, 0, &spv_allocator),
                        "Failed to change descriptor binding \"%s\" number in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    fragment_shader_binding_count++;
                } else {
                    Log::print("[WARNING] Uniform buffer \"%s\" is not found in \"%s\".", uniform_buffer_descriptor.variable_name, graphics_pipeline_descriptor.fragment_shader_filename);

                    shader_stage_flags ^= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
            }
        }

        if (shader_stage_flags != 0) {
            VkDescriptorSetLayoutBinding descriptor_set_layout_binding{};
            descriptor_set_layout_binding.binding = current_binding++;
            descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            descriptor_set_layout_binding.descriptorCount = uniform_buffer_count;
            descriptor_set_layout_binding.stageFlags = shader_stage_flags;
            descriptor_set_layout_binding.pImmutableSamplers = nullptr;
            descriptor_set_layout_bindings.push_back(descriptor_set_layout_binding);

            for (uint32_t j = 0; j < uniform_buffer_count; j++) {
                KW_ASSERT(flattened_uniform_buffer_index + j < graphics_pipeline_data.uniform_buffer_descriptor_count);
                graphics_pipeline_data.uniform_buffer_mapping.push_back(flattened_uniform_buffer_index + j);
            }

            graphics_pipeline_data.uniform_buffer_sizes.insert(graphics_pipeline_data.uniform_buffer_sizes.end(), uniform_buffer_count, static_cast<uint32_t>(uniform_buffer_descriptor.size));
        }

        flattened_uniform_buffer_index += uniform_buffer_count;
    }

    KW_ASSERT(graphics_pipeline_data.uniform_buffer_mapping.size() == graphics_pipeline_data.uniform_buffer_sizes.size());

    //
    // Check whether all descriptor bindings were specified in the descriptor.
    //
    
    /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
        KW_ERROR(
            vertex_shader_reflection.descriptor_binding_count == vertex_shader_binding_count,
            "Some of the descriptor bindings in \"%s\" are unbound (bound %u, total %u).", graphics_pipeline_descriptor.vertex_shader_filename,
            vertex_shader_binding_count, vertex_shader_reflection.descriptor_binding_count
        );
    }

    if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
        KW_ERROR(
            fragment_shader_reflection.descriptor_binding_count == fragment_shader_binding_count,
            "Some of the descriptor bindings in \"%s\" are unbound (bound %u, total %u).", graphics_pipeline_descriptor.fragment_shader_filename,
            fragment_shader_binding_count, fragment_shader_reflection.descriptor_binding_count
        );
    }
    
    //
    // Create descriptor set layout.
    //

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{};
    descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_create_info.flags = 0;
    descriptor_set_layout_create_info.bindingCount = static_cast<uint32_t>(descriptor_set_layout_bindings.size());
    descriptor_set_layout_create_info.pBindings = descriptor_set_layout_bindings.data();

    if (!descriptor_set_layout_bindings.empty()) {
        KW_ASSERT(graphics_pipeline_data.descriptor_set_layout == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateDescriptorSetLayout(m_render.device, &descriptor_set_layout_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.descriptor_set_layout),
            "Failed to create descriptor set layout \"%s\".", graphics_pipeline_descriptor.name
        );
        VK_NAME(
            m_render, graphics_pipeline_data.descriptor_set_layout,
            "Descriptor set layout \"%s\"", graphics_pipeline_descriptor.name
        );
    }

    //
    // Link vertex output variables to fragment input variables.
    //

    if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
        KW_ERROR(
            vertex_shader_reflection.output_variable_count == fragment_shader_reflection.input_variable_count,
            "Mismatching number of variables between shader stages in \"%s\" and \"%s\"", graphics_pipeline_descriptor.vertex_shader_filename, graphics_pipeline_descriptor.fragment_shader_filename
        );

        for (size_t i = 0; i < vertex_shader_reflection.output_variable_count; i++) {
            const SpvReflectInterfaceVariable* output_variable = vertex_shader_reflection.output_variables[i];
            
            KW_ERROR(
                output_variable != nullptr,
                "Invalid output variable in \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
            );
            
            KW_ERROR(
                output_variable->semantic != nullptr,
                "Invalid output variable semantic in \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
            );

            const SpvReflectInterfaceVariable* input_variable = spvReflectGetInputVariableBySemantic(&fragment_shader_reflection, output_variable->semantic, nullptr);

            KW_ERROR(
                input_variable != nullptr,
                "Failed to find fragment shader input variable \"%s\" in \"%s\".", output_variable->semantic, graphics_pipeline_descriptor.fragment_shader_filename
            );

            if (output_variable->location != input_variable->location) {
                SPV_ERROR(
                    spvReflectChangeInputVariableLocation(&fragment_shader_reflection, input_variable, output_variable->location),
                    "Failed to change fragment shader input variable \"%s\" location in \"%s\".", input_variable->location, graphics_pipeline_descriptor.fragment_shader_filename
                );
            }
        }
    }

    //
    // Save the number of vertex and instance bindings for further draw all validation.
    //

    graphics_pipeline_data.vertex_buffer_count = static_cast<uint32_t>(graphics_pipeline_descriptor.vertex_binding_descriptor_count);
    graphics_pipeline_data.instance_buffer_count = static_cast<uint32_t>(graphics_pipeline_descriptor.instance_binding_descriptor_count);

    //
    // Sort out vertex bindings and attributes for `VkPipelineVertexInputStateCreateInfo`.
    //

    size_t instance_binding_offset = graphics_pipeline_descriptor.vertex_binding_descriptor_count;
    size_t vertex_attribute_count = 0;

    Vector<VkVertexInputBindingDescription> vertex_input_binding_descriptors(create_context.graphics_pipeline_memory_resource);
    vertex_input_binding_descriptors.reserve(graphics_pipeline_descriptor.vertex_binding_descriptor_count + graphics_pipeline_descriptor.instance_binding_descriptor_count);

    for (size_t i = 0; i < graphics_pipeline_descriptor.vertex_binding_descriptor_count; i++) {
        const BindingDescriptor& binding_descriptor = graphics_pipeline_descriptor.vertex_binding_descriptors[i];

        VkVertexInputBindingDescription vertex_binding_description{};
        vertex_binding_description.binding = static_cast<uint32_t>(i);
        vertex_binding_description.stride = static_cast<uint32_t>(binding_descriptor.stride);
        vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        vertex_input_binding_descriptors.push_back(vertex_binding_description);

        vertex_attribute_count += binding_descriptor.attribute_descriptor_count;
    }

    for (size_t i = 0; i < graphics_pipeline_descriptor.instance_binding_descriptor_count; i++) {
        const BindingDescriptor& binding_descriptor = graphics_pipeline_descriptor.instance_binding_descriptors[i];

        VkVertexInputBindingDescription vertex_binding_description{};
        vertex_binding_description.binding = static_cast<uint32_t>(instance_binding_offset + i);
        vertex_binding_description.stride = static_cast<uint32_t>(binding_descriptor.stride);
        vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        vertex_input_binding_descriptors.push_back(vertex_binding_description);

        vertex_attribute_count += binding_descriptor.attribute_descriptor_count;
    }

    KW_ERROR(
        vertex_shader_reflection.input_variable_count == vertex_attribute_count,
        "Mismatching number of variables in vertex shader \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
    );

    Vector<VkVertexInputAttributeDescription> vertex_input_attribute_descriptions(create_context.graphics_pipeline_memory_resource);
    vertex_input_attribute_descriptions.reserve(vertex_attribute_count);

    for (size_t i = 0; i < graphics_pipeline_descriptor.vertex_binding_descriptor_count; i++) {
        const BindingDescriptor& binding_descriptor = graphics_pipeline_descriptor.vertex_binding_descriptors[i];

        for (size_t j = 0; j < graphics_pipeline_descriptor.vertex_binding_descriptors[i].attribute_descriptor_count; j++) {
            const AttributeDescriptor& attribute_descriptor = binding_descriptor.attribute_descriptors[j];

            char semantic[32]{};
            sprintf_s(semantic, sizeof(semantic), "%s%u", SEMANTIC_STRINGS[static_cast<size_t>(attribute_descriptor.semantic)], attribute_descriptor.semantic_index);

            const SpvReflectInterfaceVariable* interface_variable =
                spvReflectGetInputVariableBySemantic(&vertex_shader_reflection, semantic, nullptr);
            
            // "POSITION" and "POSITION0" is the same semantic.
            if (interface_variable == nullptr && attribute_descriptor.semantic_index == 0) {
                interface_variable = spvReflectGetInputVariableBySemantic(&vertex_shader_reflection, SEMANTIC_STRINGS[static_cast<size_t>(attribute_descriptor.semantic)], nullptr);
            }

            KW_ERROR(
                interface_variable != nullptr,
                "Failed to find input variable by semantic \"%s\".", semantic
            );

            VkVertexInputAttributeDescription vertex_input_attribute_description{};
            vertex_input_attribute_description.location = interface_variable->location;
            vertex_input_attribute_description.binding = static_cast<uint32_t>(i);
            vertex_input_attribute_description.format = TextureFormatUtils::convert_format_vulkan(attribute_descriptor.format);
            vertex_input_attribute_description.offset = static_cast<uint32_t>(attribute_descriptor.offset);
            vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
        }
    }

    for (size_t i = 0; i < graphics_pipeline_descriptor.instance_binding_descriptor_count; i++) {
        const BindingDescriptor& binding_descriptor = graphics_pipeline_descriptor.instance_binding_descriptors[i];

        for (size_t j = 0; j < graphics_pipeline_descriptor.instance_binding_descriptors[i].attribute_descriptor_count; j++) {
            const AttributeDescriptor& attribute_descriptor = binding_descriptor.attribute_descriptors[j];

            char semantic[32]{};
            sprintf_s(semantic, sizeof(semantic), "%s%u", SEMANTIC_STRINGS[static_cast<size_t>(attribute_descriptor.semantic)], attribute_descriptor.semantic_index);

            const SpvReflectInterfaceVariable* interface_variable =
                spvReflectGetInputVariableBySemantic(&vertex_shader_reflection, semantic, nullptr);

            // "TEXCOORD" and "TEXCOORD0" is the same semantic.
            if (interface_variable == nullptr && attribute_descriptor.semantic_index == 0) {
                interface_variable = spvReflectGetInputVariableBySemantic(&vertex_shader_reflection, SEMANTIC_STRINGS[static_cast<size_t>(attribute_descriptor.semantic)], nullptr);
            }
            
            KW_ERROR(
                interface_variable != nullptr,
                "Failed to find input variable by semantic \"%s\".", semantic
            );

            VkVertexInputAttributeDescription vertex_input_attribute_description{};
            vertex_input_attribute_description.location = interface_variable->location;
            vertex_input_attribute_description.binding = static_cast<uint32_t>(instance_binding_offset + i);
            vertex_input_attribute_description.format = TextureFormatUtils::convert_format_vulkan(attribute_descriptor.format);
            vertex_input_attribute_description.offset = static_cast<uint32_t>(attribute_descriptor.offset);
            vertex_input_attribute_descriptions.push_back(vertex_input_attribute_description);
        }
    }

    VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info{};
    pipeline_vertex_input_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipeline_vertex_input_state_create_info.flags = 0;
    pipeline_vertex_input_state_create_info.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_binding_descriptors.size());
    pipeline_vertex_input_state_create_info.pVertexBindingDescriptions = vertex_input_binding_descriptors.data();
    pipeline_vertex_input_state_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attribute_descriptions.size());
    pipeline_vertex_input_state_create_info.pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data();

    //
    // Other basic descriptors.
    //

    VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly_state_create_info{};
    pipeline_input_assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipeline_input_assembly_state_create_info.flags = 0;
    pipeline_input_assembly_state_create_info.topology = PRIMITIVE_TOPOLOGY_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.primitive_topology)];
    pipeline_input_assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo pipeline_viewport_state_create_info{};
    pipeline_viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    pipeline_viewport_state_create_info.flags = 0;
    pipeline_viewport_state_create_info.viewportCount = 1;
    pipeline_viewport_state_create_info.pViewports = nullptr;
    pipeline_viewport_state_create_info.scissorCount = 1;
    pipeline_viewport_state_create_info.pScissors = nullptr;

    bool is_depth_bias_enabled =
        graphics_pipeline_descriptor.depth_bias_constant_factor != 0.f ||
        graphics_pipeline_descriptor.depth_bias_clamp != 0.f ||
        graphics_pipeline_descriptor.depth_bias_slope_factor != 0.f;

    VkPipelineRasterizationStateCreateInfo pipeline_rasterization_state_create_info{};
    pipeline_rasterization_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    pipeline_rasterization_state_create_info.flags = 0;
    pipeline_rasterization_state_create_info.depthClampEnable = VK_FALSE;
    pipeline_rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
    pipeline_rasterization_state_create_info.polygonMode = FILL_MODE_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.fill_mode)];
    pipeline_rasterization_state_create_info.cullMode = CULL_MODE_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.cull_mode)];
    pipeline_rasterization_state_create_info.frontFace = FRONT_FACE_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.front_face)];
    pipeline_rasterization_state_create_info.depthBiasEnable = is_depth_bias_enabled;
    pipeline_rasterization_state_create_info.depthBiasConstantFactor = graphics_pipeline_descriptor.depth_bias_constant_factor;
    pipeline_rasterization_state_create_info.depthBiasClamp = graphics_pipeline_descriptor.depth_bias_clamp;
    pipeline_rasterization_state_create_info.depthBiasSlopeFactor = graphics_pipeline_descriptor.depth_bias_slope_factor;
    pipeline_rasterization_state_create_info.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info{};
    pipeline_multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipeline_multisample_state_create_info.flags = 0;
    pipeline_multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipeline_multisample_state_create_info.sampleShadingEnable = VK_FALSE;
    pipeline_multisample_state_create_info.minSampleShading = 0.f;
    pipeline_multisample_state_create_info.pSampleMask = nullptr;
    pipeline_multisample_state_create_info.alphaToCoverageEnable = VK_FALSE;
    pipeline_multisample_state_create_info.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo pipeline_depth_stencil_state_create_info{};
    pipeline_depth_stencil_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    pipeline_depth_stencil_state_create_info.flags = 0;
    pipeline_depth_stencil_state_create_info.depthTestEnable = graphics_pipeline_descriptor.is_depth_test_enabled;
    pipeline_depth_stencil_state_create_info.depthWriteEnable = graphics_pipeline_descriptor.is_depth_write_enabled;
    pipeline_depth_stencil_state_create_info.depthCompareOp = COMPARE_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.depth_compare_op)];
    pipeline_depth_stencil_state_create_info.depthBoundsTestEnable = VK_FALSE;
    pipeline_depth_stencil_state_create_info.stencilTestEnable = graphics_pipeline_descriptor.is_stencil_test_enabled;
    pipeline_depth_stencil_state_create_info.front.failOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.front_stencil_op_state.fail_op)];
    pipeline_depth_stencil_state_create_info.front.passOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.front_stencil_op_state.pass_op)];
    pipeline_depth_stencil_state_create_info.front.depthFailOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.front_stencil_op_state.depth_fail_op)];
    pipeline_depth_stencil_state_create_info.front.compareOp = COMPARE_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.front_stencil_op_state.compare_op)];
    pipeline_depth_stencil_state_create_info.front.compareMask = graphics_pipeline_descriptor.stencil_compare_mask;
    pipeline_depth_stencil_state_create_info.front.writeMask = graphics_pipeline_descriptor.stencil_write_mask;
    pipeline_depth_stencil_state_create_info.front.reference = 0;
    pipeline_depth_stencil_state_create_info.back.failOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.back_stencil_op_state.fail_op)];
    pipeline_depth_stencil_state_create_info.back.passOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.back_stencil_op_state.pass_op)];
    pipeline_depth_stencil_state_create_info.back.depthFailOp = STENCIL_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.back_stencil_op_state.depth_fail_op)];
    pipeline_depth_stencil_state_create_info.back.compareOp = COMPARE_OP_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.back_stencil_op_state.compare_op)];
    pipeline_depth_stencil_state_create_info.back.compareMask = graphics_pipeline_descriptor.stencil_compare_mask;
    pipeline_depth_stencil_state_create_info.back.writeMask = graphics_pipeline_descriptor.stencil_write_mask;
    pipeline_depth_stencil_state_create_info.back.reference = 0;
    pipeline_depth_stencil_state_create_info.minDepthBounds = 0.f;
    pipeline_depth_stencil_state_create_info.maxDepthBounds = 0.f;

    VkPipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info{};
    pipeline_dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    pipeline_dynamic_state_create_info.flags = 0;
    pipeline_dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(std::size(DYNAMIC_STATES));
    pipeline_dynamic_state_create_info.pDynamicStates = DYNAMIC_STATES;

    //
    // `GraphicsPipelineDescriptor` contains only those attachments that need color blending.
    // Other attachments implicitly have `blendEnable` equal to `VK_FALSE`.
    //

    VkPipelineColorBlendAttachmentState disabled_color_blend_attachment{};
    disabled_color_blend_attachment.blendEnable = VK_FALSE;
    disabled_color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    Vector<VkPipelineColorBlendAttachmentState> pipeline_color_blend_attachment_states(create_context.graphics_pipeline_memory_resource);
    pipeline_color_blend_attachment_states.resize(render_pass_descriptor.color_attachment_name_count, disabled_color_blend_attachment);

    for (size_t i = 0; i < graphics_pipeline_descriptor.attachment_blend_descriptor_count; i++) {
        const AttachmentBlendDescriptor& attachment_blend_descriptor = graphics_pipeline_descriptor.attachment_blend_descriptors[i];

        for (size_t j = 0; j < render_pass_descriptor.color_attachment_name_count; j++) {
            if (strcmp(render_pass_descriptor.color_attachment_names[j], attachment_blend_descriptor.attachment_name) == 0) {
                VkPipelineColorBlendAttachmentState& pipeline_color_blend_attachment_state = pipeline_color_blend_attachment_states[j];
                pipeline_color_blend_attachment_state.blendEnable = VK_TRUE;
                pipeline_color_blend_attachment_state.srcColorBlendFactor = BLEND_FACTOR_MAPPING[static_cast<size_t>(attachment_blend_descriptor.source_color_blend_factor)];
                pipeline_color_blend_attachment_state.dstColorBlendFactor = BLEND_FACTOR_MAPPING[static_cast<size_t>(attachment_blend_descriptor.destination_color_blend_factor)];
                pipeline_color_blend_attachment_state.colorBlendOp = BLEND_OP_MAPPING[static_cast<size_t>(attachment_blend_descriptor.color_blend_op)];
                pipeline_color_blend_attachment_state.srcAlphaBlendFactor = BLEND_FACTOR_MAPPING[static_cast<size_t>(attachment_blend_descriptor.source_alpha_blend_factor)];
                pipeline_color_blend_attachment_state.dstAlphaBlendFactor = BLEND_FACTOR_MAPPING[static_cast<size_t>(attachment_blend_descriptor.destination_alpha_blend_factor)];
                pipeline_color_blend_attachment_state.alphaBlendOp = BLEND_OP_MAPPING[static_cast<size_t>(attachment_blend_descriptor.alpha_blend_op)];

                break;
            }
        }
    }

    VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info{};
    pipeline_color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    pipeline_color_blend_state_create_info.flags = 0;
    pipeline_color_blend_state_create_info.logicOpEnable = VK_FALSE;
    pipeline_color_blend_state_create_info.logicOp = VK_LOGIC_OP_CLEAR;
    pipeline_color_blend_state_create_info.attachmentCount = static_cast<uint32_t>(pipeline_color_blend_attachment_states.size());
    pipeline_color_blend_state_create_info.pAttachments = pipeline_color_blend_attachment_states.data();
    pipeline_color_blend_state_create_info.blendConstants[0] = 0.f;
    pipeline_color_blend_state_create_info.blendConstants[1] = 0.f;
    pipeline_color_blend_state_create_info.blendConstants[2] = 0.f;
    pipeline_color_blend_state_create_info.blendConstants[3] = 0.f;

    //
    // Set up push constants for pipeline layout.
    //

    VkShaderStageFlags push_constants_shader_stage_flags = SHADER_VISILITY_MAPPING[static_cast<size_t>(graphics_pipeline_descriptor.push_constants_visibility)];

    if (graphics_pipeline_descriptor.push_constants_name != nullptr) {
        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            if ((push_constants_shader_stage_flags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT) {
                if (vertex_shader_reflection.push_constant_block_count == 1) {
                    KW_ERROR(
                        vertex_shader_reflection.push_constant_blocks[0].name != nullptr,
                        "Push constants have invalid name in \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
                    );

                    KW_ERROR(
                        strcmp(graphics_pipeline_descriptor.push_constants_name, vertex_shader_reflection.push_constant_blocks[0].name) == 0,
                        "Push constants name mismatch in \"%s\". Expected \"%s\", got \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename,
                        graphics_pipeline_descriptor.push_constants_name, vertex_shader_reflection.push_constant_blocks[0].name
                    );

                    KW_ERROR(
                        graphics_pipeline_descriptor.push_constants_size == vertex_shader_reflection.push_constant_blocks[0].size,
                        "Mismatching push constants size in \"%s\". Expected %zu, got %u.", graphics_pipeline_descriptor.vertex_shader_filename,
                        graphics_pipeline_descriptor.push_constants_size, vertex_shader_reflection.push_constant_blocks[0].size
                    );
                } else {
                    Log::print("[WARNING] Push constants are not found in \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename);

                    push_constants_shader_stage_flags ^= VK_SHADER_STAGE_VERTEX_BIT;
                }
            }
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            if ((push_constants_shader_stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT) {
                if (fragment_shader_reflection.push_constant_block_count == 1) {
                    KW_ERROR(
                        fragment_shader_reflection.push_constant_blocks[0].name != nullptr,
                        "Push constants have invalid name in \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
                    );

                    KW_ERROR(
                        strcmp(graphics_pipeline_descriptor.push_constants_name, fragment_shader_reflection.push_constant_blocks[0].name) == 0,
                        "Push constants name mismatch in \"%s\". Expected \"%s\", got \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename,
                        graphics_pipeline_descriptor.push_constants_name, fragment_shader_reflection.push_constant_blocks[0].name
                    );

                    KW_ERROR(
                        graphics_pipeline_descriptor.push_constants_size == fragment_shader_reflection.push_constant_blocks[0].size,
                        "Mismatching push constants size in \"%s\". Expected %zu, got %u.", graphics_pipeline_descriptor.fragment_shader_filename,
                        graphics_pipeline_descriptor.push_constants_size, fragment_shader_reflection.push_constant_blocks[0].size
                    );
                } else {
                    Log::print("[WARNING] Push constants are not found in \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename);

                    push_constants_shader_stage_flags ^= VK_SHADER_STAGE_FRAGMENT_BIT;
                }
            }
        }
    } else {
        /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
            KW_ERROR(
                vertex_shader_reflection.push_constant_block_count == 0,
                "Unexpected push constants in \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
            );
        }

        if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
            KW_ERROR(
                fragment_shader_reflection.push_constant_block_count == 0,
                "Unexpected push constants in \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
            );
        }

        push_constants_shader_stage_flags = 0;
    }

    VkPushConstantRange push_constants_range{};
    push_constants_range.stageFlags = push_constants_shader_stage_flags;
    push_constants_range.offset = 0;
    push_constants_range.size = graphics_pipeline_descriptor.push_constants_size;

    KW_ASSERT(graphics_pipeline_data.push_constants_size == 0);
    graphics_pipeline_data.push_constants_size = graphics_pipeline_descriptor.push_constants_size;

    KW_ASSERT(graphics_pipeline_data.push_constants_visibility == 0);
    graphics_pipeline_data.push_constants_visibility = push_constants_shader_stage_flags;

    //
    // Create pipeline layout.
    //

    VkPipelineLayoutCreateInfo pipeline_layout_create_info{};
    pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.flags = 0;
    if (!descriptor_set_layout_bindings.empty()) {
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = &graphics_pipeline_data.descriptor_set_layout;
    }
    if (push_constants_shader_stage_flags != 0) {
        pipeline_layout_create_info.pushConstantRangeCount = 1;
        pipeline_layout_create_info.pPushConstantRanges = &push_constants_range;
    }

    KW_ASSERT(graphics_pipeline_data.pipeline_layout == VK_NULL_HANDLE);
    VK_ERROR(
        vkCreatePipelineLayout(m_render.device, &pipeline_layout_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.pipeline_layout),
        "Failed to create pipeline layout \"%s\".", graphics_pipeline_descriptor.name
    );
    VK_NAME(
        m_render, graphics_pipeline_data.pipeline_layout,
        "Pipeline layout \"%s\"", graphics_pipeline_descriptor.name
    );

    //
    // Create shader modules for `VkPipelineShaderStageCreateInfo`. As long as `spvReflectRemoveGoogleExtensions`
    // changes the shader code, this must be done when shader modules are not needed anymore.
    //

    /*if (graphics_pipeline_descriptor.vertex_shader_filename != nullptr)*/ {
        SPV_ERROR(
            spvReflectRemoveGoogleExtensions(&vertex_shader_reflection),
            "Failed to remove google extensions from \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
        );

        VkShaderModuleCreateInfo vertex_shader_module_create_info{};
        vertex_shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertex_shader_module_create_info.flags = 0;
        vertex_shader_module_create_info.codeSize = spvReflectGetCodeSize(&vertex_shader_reflection);
        vertex_shader_module_create_info.pCode = spvReflectGetCode(&vertex_shader_reflection);

        KW_ASSERT(graphics_pipeline_data.vertex_shader_module == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateShaderModule(m_render.device, &vertex_shader_module_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.vertex_shader_module),
            "Failed to create vertex shader module from \"%s\".", graphics_pipeline_descriptor.vertex_shader_filename
        );
        VK_NAME(
            m_render, graphics_pipeline_data.vertex_shader_module,
            "Vertex shader \"%s\"", graphics_pipeline_descriptor.name
        );

        spvReflectDestroyShaderModule(&vertex_shader_reflection, &spv_allocator);
    }

    if (graphics_pipeline_descriptor.fragment_shader_filename != nullptr) {
        SPV_ERROR(
            spvReflectRemoveGoogleExtensions(&fragment_shader_reflection),
            "Failed to remove google extensions from \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
        );

        VkShaderModuleCreateInfo fragment_shader_module_create_info{};
        fragment_shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragment_shader_module_create_info.flags = 0;
        fragment_shader_module_create_info.codeSize = spvReflectGetCodeSize(&fragment_shader_reflection);
        fragment_shader_module_create_info.pCode = spvReflectGetCode(&fragment_shader_reflection);

        KW_ASSERT(graphics_pipeline_data.fragment_shader_module == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateShaderModule(m_render.device, &fragment_shader_module_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.fragment_shader_module),
            "Failed to create fragment shader module from \"%s\".", graphics_pipeline_descriptor.fragment_shader_filename
        );
        VK_NAME(
            m_render, graphics_pipeline_data.fragment_shader_module,
            "Fragment shader \"%s\"", graphics_pipeline_descriptor.name
        );

        spvReflectDestroyShaderModule(&fragment_shader_reflection, &spv_allocator);
    }

    VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_infos[2]{};
    pipeline_shader_stage_create_infos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_shader_stage_create_infos[0].flags = 0;
    pipeline_shader_stage_create_infos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    pipeline_shader_stage_create_infos[0].module = graphics_pipeline_data.vertex_shader_module;
    pipeline_shader_stage_create_infos[0].pName = "main";
    pipeline_shader_stage_create_infos[0].pSpecializationInfo = nullptr;
    pipeline_shader_stage_create_infos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_shader_stage_create_infos[1].flags = 0;
    pipeline_shader_stage_create_infos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    pipeline_shader_stage_create_infos[1].module = graphics_pipeline_data.fragment_shader_module;
    pipeline_shader_stage_create_infos[1].pName = "main";
    pipeline_shader_stage_create_infos[1].pSpecializationInfo = nullptr;

    //
    // Create graphics pipeline.
    //

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info{};
    graphics_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphics_pipeline_create_info.flags = 0;
    graphics_pipeline_create_info.stageCount = stage_count;
    graphics_pipeline_create_info.pStages = pipeline_shader_stage_create_infos;
    graphics_pipeline_create_info.pVertexInputState = &pipeline_vertex_input_state_create_info;
    graphics_pipeline_create_info.pInputAssemblyState = &pipeline_input_assembly_state_create_info;
    graphics_pipeline_create_info.pViewportState = &pipeline_viewport_state_create_info;
    graphics_pipeline_create_info.pRasterizationState = &pipeline_rasterization_state_create_info;
    graphics_pipeline_create_info.pMultisampleState = &pipeline_multisample_state_create_info;
    graphics_pipeline_create_info.pDepthStencilState = &pipeline_depth_stencil_state_create_info;
    graphics_pipeline_create_info.pColorBlendState = &pipeline_color_blend_state_create_info;
    graphics_pipeline_create_info.pDynamicState = &pipeline_dynamic_state_create_info;
    graphics_pipeline_create_info.layout = graphics_pipeline_data.pipeline_layout;
    graphics_pipeline_create_info.renderPass = render_pass_data.render_pass;
    graphics_pipeline_create_info.subpass = 0;

    KW_ASSERT(graphics_pipeline_data.pipeline == VK_NULL_HANDLE);
    VK_ERROR(
        vkCreateGraphicsPipelines(m_render.device, VK_NULL_HANDLE, 1, &graphics_pipeline_create_info, &m_render.allocation_callbacks, &graphics_pipeline_data.pipeline),
        "Failed to create graphics pipeline \"%s\".", graphics_pipeline_descriptor.name
    );
    VK_NAME(
        m_render, graphics_pipeline_data.pipeline,
        "Pipeline \"%s\"", graphics_pipeline_descriptor.name
    );

    render_pass_data.graphics_pipeline_data.push_back(std::move(graphics_pipeline_data));
}

void FrameGraphVulkan::create_synchronization(CreateContext& create_context) {
    VkSemaphoreCreateInfo semaphore_create_info{};
    semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_create_info{};
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        KW_ASSERT(m_image_acquired_binary_semaphores[swapchain_image_index] == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateSemaphore(m_render.device, &semaphore_create_info, &m_render.allocation_callbacks, &m_image_acquired_binary_semaphores[swapchain_image_index]),
            "Failed to create an image acquire binary semaphore."
        );

        KW_ASSERT(m_render_finished_binary_semaphores[swapchain_image_index] == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateSemaphore(m_render.device, &semaphore_create_info, &m_render.allocation_callbacks, &m_render_finished_binary_semaphores[swapchain_image_index]),
            "Failed to create a render finished binary semaphore."
        );

        KW_ASSERT(m_fences[swapchain_image_index] == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateFence(m_render.device, &fence_create_info, &m_render.allocation_callbacks, &m_fences[swapchain_image_index]),
            "Failed to create a fence."
        );
    }

    m_render_finished_timeline_semaphore = std::make_shared<TimelineSemaphore>(&m_render);

    // Render must wait for this frame to finish before destroying a resource that could be used in this frame.
    m_render.add_resource_dependency(m_render_finished_timeline_semaphore);
}

void FrameGraphVulkan::create_temporary_resources() {
    if (create_swapchain()) {
        create_swapchain_images();
        create_swapchain_image_views();

        create_attachment_images();
        allocate_attachment_memory();
        create_attachment_image_views();

        create_framebuffers();

        m_is_attachment_layout_set = false;
    }
}

void FrameGraphVulkan::destroy_temporary_resources() {
    for (RenderPassData& render_pass_data : m_render_pass_data) {
        for (VkFramebuffer& framebuffer: render_pass_data.framebuffers) {
            vkDestroyFramebuffer(m_render.device, framebuffer, &m_render.allocation_callbacks);
            framebuffer = VK_NULL_HANDLE;
        }
    }

    for (AllocationData& allocation_data : m_allocation_data) {
        m_render.deallocate_device_texture_memory(allocation_data.data_index, allocation_data.data_offset);
    }

    m_allocation_data.clear();

    for (AttachmentData& attachment_data : m_attachment_data) {
        if (attachment_data.sampled_view != attachment_data.image_view) {
            vkDestroyImageView(m_render.device, attachment_data.sampled_view, &m_render.allocation_callbacks);
        }
        attachment_data.sampled_view = VK_NULL_HANDLE;
        
        vkDestroyImageView(m_render.device, attachment_data.image_view, &m_render.allocation_callbacks);
        attachment_data.image_view = VK_NULL_HANDLE;

        vkDestroyImage(m_render.device, attachment_data.image, &m_render.allocation_callbacks);
        attachment_data.image = VK_NULL_HANDLE;
    }

    for (VkImageView& image_view : m_swapchain_image_views) {
        vkDestroyImageView(m_render.device, image_view, &m_render.allocation_callbacks);
        image_view = VK_NULL_HANDLE;
    }

    for (VkImage& image : m_swapchain_images) {
        image = VK_NULL_HANDLE;
    }

    // Spec states that `vkDestroySwapchainKHR` must silently ignore `m_swapchain == VK_NULL_HANDLE`, but on my hardware it crashes.
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_render.device, m_swapchain, &m_render.allocation_callbacks);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool FrameGraphVulkan::create_swapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    VK_ERROR(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_render.physical_device, m_surface, &capabilities),
        "Failed to query surface capabilities."
    );
    KW_ERROR(
        capabilities.minImageCount <= SWAPCHAIN_IMAGE_COUNT && (capabilities.maxImageCount >= SWAPCHAIN_IMAGE_COUNT || capabilities.maxImageCount == 0),
        "Incompatible surface (min %u, max %u).", capabilities.minImageCount, capabilities.maxImageCount
    );

    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent = capabilities.currentExtent;
    } else {
        extent = VkExtent2D{
            std::clamp(m_window.get_render_width(), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(m_window.get_render_height(), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }

    m_swapchain_width = extent.width;
    m_swapchain_height = extent.height;

    if (extent.width == 0 || extent.height == 0) {
        // Window is minimized.
        return false;
    }

    VkSwapchainCreateInfoKHR swapchain_create_info{};
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = m_surface;
    swapchain_create_info.minImageCount = static_cast<uint32_t>(SWAPCHAIN_IMAGE_COUNT);
    swapchain_create_info.imageFormat = m_surface_format;
    swapchain_create_info.imageColorSpace = m_color_space;
    swapchain_create_info.imageExtent = extent;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = nullptr;
    swapchain_create_info.preTransform = capabilities.currentTransform;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = m_present_mode;
    swapchain_create_info.clipped = VK_TRUE;

    KW_ASSERT(m_swapchain == VK_NULL_HANDLE);
    VK_ERROR(
        vkCreateSwapchainKHR(m_render.device, &swapchain_create_info, &m_render.allocation_callbacks, &m_swapchain),
        "Failed to create a swapchain."
    );
    VK_NAME(m_render, m_swapchain, "Swapchain");

    return true;
}

void FrameGraphVulkan::create_swapchain_images() {
    uint32_t image_count;
    VK_ERROR(
        vkGetSwapchainImagesKHR(m_render.device, m_swapchain, &image_count, nullptr),
        "Failed to get swapchain image count."
    );
    KW_ERROR(image_count == SWAPCHAIN_IMAGE_COUNT, "Invalid swapchain image count %u.", image_count);

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        KW_ASSERT(m_swapchain_images[swapchain_image_index] == VK_NULL_HANDLE);
    }

    VK_ERROR(
        vkGetSwapchainImagesKHR(m_render.device, m_swapchain, &image_count, m_swapchain_images),
        "Failed to get swapchain images."
    );

    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        VK_NAME(m_render, m_swapchain_images[swapchain_image_index], "Swapchain image");
    }
}

void FrameGraphVulkan::create_swapchain_image_views() {
    for (size_t swapchain_image_index = 0; swapchain_image_index < SWAPCHAIN_IMAGE_COUNT; swapchain_image_index++) {
        VkImageViewCreateInfo image_view_create_info{};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.image = m_swapchain_images[swapchain_image_index];
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = m_surface_format;
        image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_view_create_info.subresourceRange.baseMipLevel = 0;
        image_view_create_info.subresourceRange.levelCount = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount = 1;

        KW_ASSERT(m_swapchain_image_views[swapchain_image_index] == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateImageView(m_render.device, &image_view_create_info, &m_render.allocation_callbacks, &m_swapchain_image_views[swapchain_image_index]),
            "Failed to create image view."
        );
        VK_NAME(m_render, m_swapchain_image_views[swapchain_image_index], "Swapchain image view");
    }
}

void FrameGraphVulkan::create_attachment_images() {
    // Ignore the first attachment, because it's a swapchain attachment.
    for (size_t i = 1; i < m_attachment_descriptors.size(); i++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[i];
        AttachmentData& attachment_data = m_attachment_data[i];

        uint32_t width;
        uint32_t height;
        if (attachment_descriptor.size_class == SizeClass::RELATIVE) {
            width = static_cast<uint32_t>(attachment_descriptor.width * m_swapchain_width);
            height = static_cast<uint32_t>(attachment_descriptor.height * m_swapchain_height);
        } else {
            width = static_cast<uint32_t>(attachment_descriptor.width);
            height = static_cast<uint32_t>(attachment_descriptor.height);
        }

        VkImageCreateInfo image_create_info{};
        image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags = 0;
        image_create_info.imageType = VK_IMAGE_TYPE_2D;
        image_create_info.format = TextureFormatUtils::convert_format_vulkan(attachment_descriptor.format);
        image_create_info.extent.width = width;
        image_create_info.extent.height = height;
        image_create_info.extent.depth = 1;
        image_create_info.mipLevels = 1;
        image_create_info.arrayLayers = 1;
        image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_create_info.usage = attachment_data.usage_mask;
        image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_create_info.queueFamilyIndexCount = 0;
        image_create_info.pQueueFamilyIndices = nullptr;
        image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        KW_ASSERT(attachment_data.image == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateImage(m_render.device, &image_create_info, &m_render.allocation_callbacks, &attachment_data.image),
            "Failed to create attachment image \"%s\".", attachment_descriptor.name
        );
        VK_NAME(m_render, attachment_data.image, "Attachment \"%s\"", attachment_descriptor.name);
    }
}

void FrameGraphVulkan::allocate_attachment_memory() {
    //
    // Query attachment memory requirements.
    //

    Vector<VkMemoryRequirements> memory_requirements(m_attachment_data.size(), m_render.transient_memory_resource);

    // Ignore the first attachment, because it's a swapchain attachment.
    for (size_t i = 1; i < memory_requirements.size(); i++) {
        vkGetImageMemoryRequirements(m_render.device, m_attachment_data[i].image, &memory_requirements[i]);
    }

    //
    // Compute sorted attachment mapping.
    //

    Vector<uint32_t> sorted_attachment_indices(memory_requirements.size(), m_render.transient_memory_resource);

    std::iota(sorted_attachment_indices.begin(), sorted_attachment_indices.end(), 0);

    std::sort(sorted_attachment_indices.begin(), sorted_attachment_indices.end(), [&](uint32_t a, uint32_t b) {
        return memory_requirements[a].size > memory_requirements[b].size;
    });

    //
    // Allocate memory for attachments or alias other attachments.
    //

    struct AliasData {
        uint32_t attachment_index;

        VkDeviceMemory memory;

        size_t alias_index;
        size_t alias_offset;
        size_t alias_size_left;
    };

    KW_ASSERT(m_allocation_data.empty());
    m_allocation_data.reserve(m_attachment_data.size());

    Vector<AliasData> alias_data(m_render.transient_memory_resource);
    alias_data.reserve(sorted_attachment_indices.size());

    for (size_t i = 0; i < sorted_attachment_indices.size(); i++) {
        // Ignore the swapchain attachment.
        uint32_t attachment_index = sorted_attachment_indices[i];
        if (attachment_index != 0) {
            VkDeviceSize size = next_pow2(memory_requirements[attachment_index].size);
            VkDeviceSize alignment = memory_requirements[attachment_index].alignment;

            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize offset = 0;

            for (size_t j = 0; j < alias_data.size(); j++) {
                VkDeviceSize alignemnt_offset = align_up(alias_data[j].alias_offset, alignment) - alias_data[j].alias_offset;
                if (alias_data[j].alias_size_left >= size + alignemnt_offset) {
                    bool overlap = false;

                    size_t alias_index = j;
                    while (!overlap && alias_index != SIZE_MAX) {
                        uint32_t another_attachment_index = alias_data[alias_index].attachment_index;

                        uint32_t a = m_attachment_data[attachment_index].min_parallel_block_index;
                        uint32_t b = m_attachment_data[attachment_index].max_parallel_block_index;
                        uint32_t c = m_attachment_data[another_attachment_index].min_parallel_block_index;
                        uint32_t d = m_attachment_data[another_attachment_index].max_parallel_block_index;

                        if (a <= b) {
                            // Attachment range is non-looped.
                            if (c <= d) {
                                // Another attachment range is non-looped.
                                if (a <= d && b >= c) {
                                    overlap = true;
                                }
                            } else {
                                // Another attachment range is looped.
                                if (a <= d || b >= c) {
                                    overlap = true;
                                }
                            }
                        } else {
                            // Attachment range is looped.
                            if (c <= d) {
                                // Another attachment range is non-looped.
                                if (c <= b || d >= a) {
                                    overlap = true;
                                }
                            } else {
                                // Another attachment range is looped. Both looped ranges always overlap.
                                overlap = true;
                            }
                        }

                        alias_index = alias_data[alias_index].alias_index;
                    }

                    if (!overlap) {
                        memory = alias_data[j].memory;
                        offset = alias_data[j].alias_offset + alignemnt_offset;

                        alias_data[j].alias_size_left -= size + alignemnt_offset;
                        alias_data[j].alias_offset += size + alignemnt_offset;

                        alias_data.push_back(AliasData{ attachment_index, memory, j, offset, size });

                        break;
                    }
                }
            }

            if (memory == VK_NULL_HANDLE) {
                RenderVulkan::DeviceAllocation device_allocation = m_render.allocate_device_texture_memory(size, alignment);
                KW_ASSERT(device_allocation.memory != VK_NULL_HANDLE);

                m_allocation_data.push_back(AllocationData{ device_allocation.data_index, device_allocation.data_offset });

                memory = device_allocation.memory;
                offset = device_allocation.data_offset;

                alias_data.push_back(AliasData{ attachment_index, memory, SIZE_MAX, offset, size });
            }

            AttachmentData& attachment_data = m_attachment_data[attachment_index];
            KW_ASSERT(attachment_data.image != VK_NULL_HANDLE);

            const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
            KW_ASSERT(attachment_descriptor.name != nullptr);

            VK_ERROR(
                vkBindImageMemory(m_render.device, attachment_data.image, memory, offset),
                "Failed to bind attachment image \"%s\" to memory.", attachment_descriptor.name
            );
        }
    }
}

void FrameGraphVulkan::create_attachment_image_views() {
    // Ignore the first attachment, because it's a swapchain attachment.
    for (size_t i = 1; i < m_attachment_descriptors.size(); i++) {
        const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[i];
        KW_ASSERT(attachment_descriptor.name != nullptr);

        AttachmentData& attachment_data = m_attachment_data[i];
        KW_ASSERT(attachment_data.image != VK_NULL_HANDLE);

        VkImageAspectFlags aspect_mask;
        if (attachment_descriptor.format == TextureFormat::D24_UNORM_S8_UINT || attachment_descriptor.format == TextureFormat::D32_FLOAT_S8X24_UINT) {
            aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else if (attachment_descriptor.format == TextureFormat::D16_UNORM || attachment_descriptor.format == TextureFormat::D32_FLOAT) {
            aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else {
            aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkImageSubresourceRange image_subresource_range{};
        image_subresource_range.aspectMask = aspect_mask;
        image_subresource_range.baseMipLevel = 0;
        image_subresource_range.levelCount = 1;
        image_subresource_range.baseArrayLayer = 0;
        image_subresource_range.layerCount = 1;

        VkImageViewCreateInfo image_view_create_info{};
        image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.flags = 0;
        image_view_create_info.image = attachment_data.image;
        image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.format = TextureFormatUtils::convert_format_vulkan(attachment_descriptor.format);
        image_view_create_info.subresourceRange = image_subresource_range;

        KW_ASSERT(attachment_data.image_view == VK_NULL_HANDLE);
        VK_ERROR(
            vkCreateImageView(m_render.device, &image_view_create_info, &m_render.allocation_callbacks, &attachment_data.image_view),
            "Failed to create attachment image view \"%s\".", attachment_descriptor.name
        );
        VK_NAME(m_render, attachment_data.image_view, "Attachment view \"%s\"", attachment_descriptor.name);

        if (aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
            image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            KW_ASSERT(attachment_data.sampled_view == VK_NULL_HANDLE);
            VK_ERROR(
                vkCreateImageView(m_render.device, &image_view_create_info, &m_render.allocation_callbacks, &attachment_data.sampled_view),
                "Failed to create attachment image view \"%s\".", attachment_descriptor.name
            );
            VK_NAME(m_render, attachment_data.sampled_view, "Attachment sampled view \"%s\"", attachment_descriptor.name);
        } else {
            attachment_data.sampled_view = attachment_data.image_view;
        }
    }
}

void FrameGraphVulkan::create_framebuffers() {
    for (size_t render_pass_index = 0; render_pass_index < m_render_pass_data.size(); render_pass_index++) {
        RenderPassData& render_pass_data = m_render_pass_data[render_pass_index];
        KW_ASSERT(render_pass_data.render_pass != VK_NULL_HANDLE);
        KW_ASSERT(!render_pass_data.attachment_indices.empty());

        //
        // Query framebuffer size from any attachment, because they all must have equal size.
        //

        uint32_t framebuffer_count;

        {
            uint32_t attachment_index = render_pass_data.attachment_indices[0];
            KW_ASSERT(attachment_index < m_attachment_descriptors.size());

            const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
            KW_ASSERT(attachment_descriptor.count > 0);

            framebuffer_count = attachment_descriptor.count;
            
            if (attachment_descriptor.size_class == SizeClass::RELATIVE) {
                render_pass_data.framebuffer_width = static_cast<uint32_t>(attachment_descriptor.width * m_swapchain_width);
                render_pass_data.framebuffer_height = static_cast<uint32_t>(attachment_descriptor.height * m_swapchain_height);
            } else {
                render_pass_data.framebuffer_width = static_cast<uint32_t>(attachment_descriptor.width);
                render_pass_data.framebuffer_height = static_cast<uint32_t>(attachment_descriptor.height);
            }
        }

        //
        // Check whether there's a swapchain attachment in this framebuffer.
        //

        bool is_swapchain_attachment_present = false;

        for (size_t i = 0; i < render_pass_data.attachment_indices.size(); i++) {
            if (render_pass_data.attachment_indices[i] == 0) {
                is_swapchain_attachment_present = true;
            }
        }

        //
        // Create framebuffers.
        //

        if (is_swapchain_attachment_present) {
            KW_ASSERT(framebuffer_count == 1);
            render_pass_data.framebuffers.resize(static_cast<uint32_t>(SWAPCHAIN_IMAGE_COUNT));
        } else {
            render_pass_data.framebuffers.resize(framebuffer_count);
        }

        for (size_t framebuffer_index = 0; framebuffer_index < render_pass_data.framebuffers.size(); framebuffer_index++) {
            Vector<VkImageView> attachments(render_pass_data.attachment_indices.size(), m_render.transient_memory_resource);

            for (size_t i = 0; i < render_pass_data.attachment_indices.size(); i++) {
                uint32_t attachment_index = render_pass_data.attachment_indices[i];
                KW_ASSERT(attachment_index < m_attachment_descriptors.size());

                if (attachment_index == 0) {
                    // Swapchain attachment.
                    attachments[i] = m_swapchain_image_views[framebuffer_index];
                } else {
                    const AttachmentDescriptor& attachment_descriptor = m_attachment_descriptors[attachment_index];
                    if (attachment_descriptor.count > 1) {
                        // Attachments with count > 1 (swapchain attachment is not allowed).
                        attachments[i] = m_attachment_data[attachment_index + framebuffer_index].image_view;
                    } else {
                        // Swapchain attachment + other attachments.
                        attachments[i] = m_attachment_data[attachment_index].image_view;
                    }
                }
            }

            VkFramebufferCreateInfo framebuffer_create_info{};
            framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.renderPass = render_pass_data.render_pass;
            framebuffer_create_info.pAttachments = attachments.data();
            framebuffer_create_info.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebuffer_create_info.width = render_pass_data.framebuffer_width;
            framebuffer_create_info.height = render_pass_data.framebuffer_height;
            framebuffer_create_info.layers = 1;

            VK_ERROR(
                vkCreateFramebuffer(m_render.device, &framebuffer_create_info, &m_render.allocation_callbacks, &render_pass_data.framebuffers[framebuffer_index]),
                "Failed to create framebuffer."
            );
        }
    }
}

size_t FrameGraphVulkan::NoOpHash::operator()(uint64_t value) const {
    return value;
}

FrameGraphVulkan::AttachmentData::AttachmentData(MemoryResource& memory_resource)
    : image(VK_NULL_HANDLE)
    , image_view(VK_NULL_HANDLE)
    , sampled_view(VK_NULL_HANDLE)
    , min_parallel_block_index(0)
    , max_parallel_block_index(0)
    , usage_mask(0)
    , initial_access_mask(VK_ACCESS_NONE_KHR)
    , initial_layout(VK_IMAGE_LAYOUT_UNDEFINED)
    , blit_data(memory_resource)
{
}

FrameGraphVulkan::DescriptorSetData::DescriptorSetData(VkDescriptorSet descriptor_set_, uint64_t last_frame_usage_)
    : descriptor_set(descriptor_set_)
    , last_frame_usage(last_frame_usage_)
{
}

FrameGraphVulkan::DescriptorSetData::DescriptorSetData(const DescriptorSetData& other)
    : descriptor_set(other.descriptor_set)
    , last_frame_usage(other.last_frame_usage.load(std::memory_order_acquire))
{
}

FrameGraphVulkan::GraphicsPipelineData::GraphicsPipelineData(MemoryResource& memory_resource)
    : vertex_shader_module(VK_NULL_HANDLE)
    , fragment_shader_module(VK_NULL_HANDLE)
    , descriptor_set_layout(VK_NULL_HANDLE)
    , pipeline_layout(VK_NULL_HANDLE)
    , pipeline(VK_NULL_HANDLE)
    , vertex_buffer_count(0)
    , instance_buffer_count(0)
    , bound_descriptor_sets(memory_resource)
    , unbound_descriptor_sets(memory_resource)
    , descriptor_set_count(1)
    , uniform_attachment_descriptor_count(0)
    , uniform_attachment_indices(memory_resource)
    , uniform_attachment_mapping(memory_resource)
    , uniform_texture_descriptor_count(0)
    , uniform_texture_first_binding(0)
    , uniform_texture_mapping(memory_resource)
    , uniform_samplers(memory_resource)
    , uniform_buffer_descriptor_count(0)
    , uniform_buffer_first_binding(0)
    , uniform_buffer_mapping(memory_resource)
    , uniform_buffer_sizes(memory_resource)
    , push_constants_size(0)
    , push_constants_visibility(0)
{
}

FrameGraphVulkan::GraphicsPipelineData::GraphicsPipelineData(GraphicsPipelineData&& other)
    : vertex_shader_module(other.vertex_shader_module)
    , fragment_shader_module(other.fragment_shader_module)
    , descriptor_set_layout(other.descriptor_set_layout)
    , pipeline_layout(other.pipeline_layout)
    , pipeline(other.pipeline)
    , vertex_buffer_count(other.vertex_buffer_count)
    , instance_buffer_count(other.instance_buffer_count)
    , bound_descriptor_sets(std::move(other.bound_descriptor_sets))
    , unbound_descriptor_sets(std::move(other.unbound_descriptor_sets))
    , descriptor_set_count(other.descriptor_set_count)
    , uniform_attachment_descriptor_count(other.uniform_attachment_descriptor_count)
    , uniform_attachment_indices(std::move(other.uniform_attachment_indices))
    , uniform_attachment_mapping(std::move(other.uniform_attachment_mapping))
    , uniform_texture_descriptor_count(other.uniform_texture_descriptor_count)
    , uniform_texture_first_binding(other.uniform_texture_first_binding)
    , uniform_texture_mapping(std::move(other.uniform_texture_mapping))
    , uniform_samplers(std::move(other.uniform_samplers))
    , uniform_buffer_descriptor_count(other.uniform_buffer_descriptor_count)
    , uniform_buffer_first_binding(other.uniform_buffer_first_binding)
    , uniform_buffer_mapping(std::move(other.uniform_buffer_mapping))
    , uniform_buffer_sizes(std::move(other.uniform_buffer_sizes))
    , push_constants_size(other.push_constants_size)
    , push_constants_visibility(other.push_constants_visibility)
{
}

FrameGraphVulkan::CommandPoolData::CommandPoolData(MemoryResource& memory_resource)
    : command_pool(VK_NULL_HANDLE)
    , command_buffers(memory_resource)
    , current_command_buffer(0)
{
}

FrameGraphVulkan::CommandPoolData::CommandPoolData(CommandPoolData&& other)
    : command_pool(other.command_pool)
    , command_buffers(std::move(other.command_buffers))
    , current_command_buffer(other.current_command_buffer)
{
}

FrameGraphVulkan::RenderPassContextVulkan::RenderPassContextVulkan(FrameGraphVulkan& frame_graph, uint32_t render_pass_index, uint32_t framebuffer_index)
    : command_buffer(VK_NULL_HANDLE)
    , transfer_semaphore_value(0)
    , m_frame_graph(frame_graph)
    , m_render_pass_index(render_pass_index)
    , m_framebuffer_index(framebuffer_index)
    , m_framebuffer_width(frame_graph.m_render_pass_data[render_pass_index].framebuffer_width)
    , m_framebuffer_height(frame_graph.m_render_pass_data[render_pass_index].framebuffer_height)
    , m_graphics_pipeline_index(UINT32_MAX)
{
}

void FrameGraphVulkan::RenderPassContextVulkan::draw(const DrawCallDescriptor& descriptor) {
    RenderPassData& render_pass_data = m_frame_graph.m_render_pass_data[m_render_pass_index];

    KW_ASSERT(
        descriptor.graphics_pipeline_index < render_pass_data.graphics_pipeline_data.size(),
        "Invalid graphics pipeline index %u. The total number of graphics pipelines is %zu.", descriptor.graphics_pipeline_index, render_pass_data.graphics_pipeline_data.size()
    );

    GraphicsPipelineData& graphics_pipeline_data = render_pass_data.graphics_pipeline_data[descriptor.graphics_pipeline_index];

    //
    // Validate the draw call.
    //

    KW_ASSERT(
        descriptor.vertex_buffer_count == graphics_pipeline_data.vertex_buffer_count,
        "Mismatching vertex buffer count. Expected %u, got %zu.", graphics_pipeline_data.vertex_buffer_count, descriptor.vertex_buffer_count
    );

    KW_ASSERT(
        descriptor.instance_buffer_count == graphics_pipeline_data.instance_buffer_count,
        "Mismatching instance buffer count. Expected %u, got %zu.", graphics_pipeline_data.instance_buffer_count, descriptor.instance_buffer_count
    );

    KW_ASSERT(
        descriptor.uniform_attachment_index_count == 0 || descriptor.uniform_attachment_index_count == graphics_pipeline_data.uniform_attachment_descriptor_count,
        "Mismatching uniform attachment count. Expected %u, got %zu.", graphics_pipeline_data.uniform_attachment_descriptor_count, descriptor.uniform_attachment_index_count
    );

    KW_ASSERT(
        descriptor.uniform_texture_count == graphics_pipeline_data.uniform_texture_descriptor_count,
        "Mismatching uniform texture count. Expected %u, got %zu.", graphics_pipeline_data.uniform_texture_descriptor_count, descriptor.uniform_texture_count
    );

    KW_ASSERT(
        descriptor.uniform_buffer_count == graphics_pipeline_data.uniform_buffer_descriptor_count,
        "Mismatching uniform buffer count. Expected %u, got %zu.", graphics_pipeline_data.uniform_buffer_descriptor_count, descriptor.uniform_buffer_count
    );

    if (descriptor.push_constants != nullptr && graphics_pipeline_data.push_constants_size > 0) {
        KW_ASSERT(
            descriptor.push_constants_size == graphics_pipeline_data.push_constants_size,
            "Mismatching push constants size. Expected %u, got %zu.", graphics_pipeline_data.push_constants_size, descriptor.push_constants_size
        );
    } else {
        if (descriptor.push_constants == nullptr) {
            KW_ASSERT(
                graphics_pipeline_data.push_constants_size == 0,
                "Push constants are expected."
            );
        } else {
            KW_ASSERT(
                descriptor.push_constants == nullptr,
                "Push constants are not expected."
            );
        }
    }

    KW_ASSERT(descriptor.index_count > 0, "Zero indices are drawn. Perhaps forgot to specify?");

    //
    // Bind graphics pipeline.
    //

    if (descriptor.graphics_pipeline_index != m_graphics_pipeline_index) {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_data.pipeline);
        m_graphics_pipeline_index = descriptor.graphics_pipeline_index;
    }

    //
    // Bind vertex buffers.
    //

    Vector<VkBuffer> vertex_buffers(descriptor.vertex_buffer_count, m_frame_graph.m_render.transient_memory_resource);
    Vector<VkDeviceSize> vertex_buffer_offsets(descriptor.vertex_buffer_count, m_frame_graph.m_render.transient_memory_resource);

    for (size_t i = 0; i < descriptor.vertex_buffer_count; i++) {
        const VertexBufferVulkan* vertex_buffer_vulkan = reinterpret_cast<const VertexBufferVulkan*>(descriptor.vertex_buffers[i]);
        KW_ASSERT(vertex_buffer_vulkan != nullptr);

        vertex_buffers[i] = vertex_buffer_vulkan->buffer;

        if (vertex_buffer_vulkan->is_transient()) {
            vertex_buffer_offsets[i] = vertex_buffer_vulkan->transient_buffer_offset;
        } else {
            vertex_buffer_offsets[i] = 0;
        }
    }

    vkCmdBindVertexBuffers(command_buffer, 0, static_cast<uint32_t>(descriptor.vertex_buffer_count), vertex_buffers.data(), vertex_buffer_offsets.data());

    //
    // Bind instance buffers.
    //

    if (descriptor.instance_buffer_count > 0) {
        Vector<VkBuffer> instance_buffers(descriptor.instance_buffer_count, m_frame_graph.m_render.transient_memory_resource);
        Vector<VkDeviceSize> instance_buffer_offsets(descriptor.instance_buffer_count, m_frame_graph.m_render.transient_memory_resource);

        for (size_t i = 0; i < descriptor.instance_buffer_count; i++) {
            const VertexBufferVulkan* vertex_buffer_vulkan = reinterpret_cast<const VertexBufferVulkan*>(descriptor.instance_buffers[i]);
            KW_ASSERT(vertex_buffer_vulkan != nullptr);

            instance_buffers[i] = vertex_buffer_vulkan->buffer;

            if (vertex_buffer_vulkan->is_transient()) {
                instance_buffer_offsets[i] = vertex_buffer_vulkan->transient_buffer_offset;
            } else {
                instance_buffer_offsets[i] = 0;
            }
        }

        vkCmdBindVertexBuffers(command_buffer, graphics_pipeline_data.vertex_buffer_count, static_cast<uint32_t>(descriptor.instance_buffer_count), instance_buffers.data(), instance_buffer_offsets.data());
    }

    //
    // Bind index buffer.
    //

    const IndexBufferVulkan* index_buffer_vulkan = reinterpret_cast<const IndexBufferVulkan*>(descriptor.index_buffer);
    KW_ASSERT(index_buffer_vulkan != nullptr);

    VkIndexType index_type;
    if (index_buffer_vulkan->get_index_size() == IndexSize::UINT16) {
        index_type = VK_INDEX_TYPE_UINT16;
    } else {
        index_type = VK_INDEX_TYPE_UINT32;
    }

    VkDeviceSize index_buffer_offset;
    if (index_buffer_vulkan->is_transient()) {
        index_buffer_offset = index_buffer_vulkan->transient_buffer_offset;
    } else {
        index_buffer_offset = 0;
    }

    vkCmdBindIndexBuffer(command_buffer, index_buffer_vulkan->buffer, index_buffer_offset, index_type);

    //
    // Set scissor.
    //

    VkRect2D scissor{};
    if (descriptor.override_scissors) {
        scissor.offset.x = descriptor.scissors.x;
        scissor.offset.y = descriptor.scissors.y;
        scissor.extent.width = descriptor.scissors.width;
        scissor.extent.height = descriptor.scissors.height;
    } else {
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = m_framebuffer_width;
        scissor.extent.height = m_framebuffer_height;
    }

    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    //
    // Set stencil reference value.
    //

    vkCmdSetStencilReference(command_buffer, VK_STENCIL_FACE_FRONT_AND_BACK, descriptor.stencil_reference);

    //
    // Compute descriptor set's CRC.
    //

    uint64_t crc = 0;

    for (size_t i = 0; i < graphics_pipeline_data.uniform_attachment_mapping.size(); i++) {
        uint32_t uniform_attachment_mapping = graphics_pipeline_data.uniform_attachment_mapping[i];

        uint32_t attachment_index = graphics_pipeline_data.uniform_attachment_indices[i];
        KW_ASSERT(attachment_index < m_frame_graph.m_attachment_data.size());

        uint32_t attachment_offset;
        
        if (descriptor.uniform_attachment_index_count > 0) {
            KW_ASSERT(uniform_attachment_mapping < descriptor.uniform_attachment_index_count);

            attachment_offset = descriptor.uniform_attachment_indices[uniform_attachment_mapping];
            KW_ASSERT(attachment_index + attachment_offset < m_frame_graph.m_attachment_data.size());
        } else {
            attachment_offset = 0;
        }

        AttachmentData& attachment_data = m_frame_graph.m_attachment_data[attachment_index + attachment_offset];
        KW_ASSERT(attachment_data.image_view != VK_NULL_HANDLE);

        crc = CrcUtils::crc64(crc, &attachment_data.image_view, sizeof(VkImageView));
    }

    for (uint32_t uniform_texture_mapping : graphics_pipeline_data.uniform_texture_mapping) {
        KW_ASSERT(uniform_texture_mapping < descriptor.uniform_texture_count);

        const TextureVulkan* texture_vulkan = static_cast<const TextureVulkan*>(descriptor.uniform_textures[uniform_texture_mapping]);
        KW_ASSERT(texture_vulkan != nullptr);

        crc = CrcUtils::crc64(crc, &texture_vulkan->image_view, sizeof(VkImageView));

        transfer_semaphore_value = std::max(transfer_semaphore_value, texture_vulkan->transfer_semaphore_value);
    }

    for (uint32_t uniform_buffer_mapping : graphics_pipeline_data.uniform_buffer_mapping) {
        KW_ASSERT(uniform_buffer_mapping < descriptor.uniform_buffer_count);

        const UniformBufferVulkan* uniform_buffer_vulkan = static_cast<const UniformBufferVulkan*>(descriptor.uniform_buffers[uniform_buffer_mapping]);
        KW_ASSERT(uniform_buffer_vulkan != nullptr);

        VkBuffer transient_buffer = m_frame_graph.m_render.get_transient_buffer();
        crc = CrcUtils::crc64(crc, &transient_buffer, sizeof(VkBuffer));

        // `transfer_semaphore_value` for transient data is implicitly zero.
    }

    //
    // Find or create descriptor set.
    //

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    if (!graphics_pipeline_data.uniform_attachment_mapping.empty() || !graphics_pipeline_data.uniform_texture_mapping.empty() || !graphics_pipeline_data.uniform_buffer_mapping.empty()) {
        std::shared_lock bound_descriptor_sets_shared_lock(graphics_pipeline_data.bound_descriptor_sets_mutex);

        auto bound_descriptor_set_it = graphics_pipeline_data.bound_descriptor_sets.find(crc);
        if (bound_descriptor_set_it == graphics_pipeline_data.bound_descriptor_sets.end()) {
            // We won't access bound descriptor sets for a while. Let other threads to write to it.
            bound_descriptor_sets_shared_lock.unlock();

            //
            // Get a descriptor from unbound descriptor sets (may require to allocate new descriptors,
            // which may require to create new descriptor pools).
            //

            {
                std::lock_guard unbound_descriptor_sets_lock(graphics_pipeline_data.unbound_descriptor_sets_mutex);

                if (graphics_pipeline_data.unbound_descriptor_sets.empty()) {
                    // We don't have a descriptor to write to. Allocate more descriptors.
                    while (!allocate_descriptor_sets(graphics_pipeline_data)) {
                        // Failed to allocate more descriptors because descriptor pools are full. Create new pool.
                        std::lock_guard descriptor_pools_lock(m_frame_graph.m_descriptor_pools_mutex);

                        VkDescriptorPoolSize descriptor_pool_sizes[3]{};
                        descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                        descriptor_pool_sizes[0].descriptorCount = m_frame_graph.m_uniform_texture_count_per_descriptor_pool;
                        descriptor_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
                        descriptor_pool_sizes[1].descriptorCount = m_frame_graph.m_uniform_sampler_count_per_descriptor_pool;
                        descriptor_pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                        descriptor_pool_sizes[2].descriptorCount = m_frame_graph.m_uniform_buffer_count_per_descriptor_pool;

                        VkDescriptorPoolCreateInfo descriptor_pool_create_info{};
                        descriptor_pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                        descriptor_pool_create_info.flags = 0;
                        descriptor_pool_create_info.maxSets = m_frame_graph.m_descriptor_set_count_per_descriptor_pool;
                        descriptor_pool_create_info.poolSizeCount = static_cast<uint32_t>(std::size(descriptor_pool_sizes));
                        descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;

                        VkDescriptorPool descriptor_pool;
                        VK_ERROR(
                            vkCreateDescriptorPool(m_frame_graph.m_render.device, &descriptor_pool_create_info, &m_frame_graph.m_render.allocation_callbacks, &descriptor_pool),
                            "Failed to create a descriptor pool."
                        );

                        m_frame_graph.m_descriptor_pools.push_back(DescriptorPoolData{
                            descriptor_pool,
                            m_frame_graph.m_descriptor_set_count_per_descriptor_pool,
                            m_frame_graph.m_uniform_texture_count_per_descriptor_pool,
                            m_frame_graph.m_uniform_sampler_count_per_descriptor_pool,
                            m_frame_graph.m_uniform_buffer_count_per_descriptor_pool,
                        });
                    }
                }

                descriptor_set = graphics_pipeline_data.unbound_descriptor_sets.back();
                graphics_pipeline_data.unbound_descriptor_sets.pop_back();
            }

            Vector<VkWriteDescriptorSet> write_descriptor_sets(m_frame_graph.m_render.transient_memory_resource);
            write_descriptor_sets.reserve(3);

            //
            // Fill attachment descriptors.
            //

            Vector<VkDescriptorImageInfo> attachment_descriptors(graphics_pipeline_data.uniform_attachment_mapping.size(), m_frame_graph.m_render.transient_memory_resource);
            if (!attachment_descriptors.empty()) {
                for (size_t i = 0; i < graphics_pipeline_data.uniform_attachment_mapping.size(); i++) {
                    uint32_t uniform_attachment_mapping = graphics_pipeline_data.uniform_attachment_mapping[i];

                    uint32_t attachment_index = graphics_pipeline_data.uniform_attachment_indices[i];
                    KW_ASSERT(attachment_index < m_frame_graph.m_attachment_data.size());

                    uint32_t attachment_offset;

                    if (descriptor.uniform_attachment_index_count > 0) {
                        KW_ASSERT(uniform_attachment_mapping < descriptor.uniform_attachment_index_count);

                        attachment_offset = descriptor.uniform_attachment_indices[uniform_attachment_mapping];
                        KW_ASSERT(attachment_index + attachment_offset < m_frame_graph.m_attachment_data.size());
                    } else {
                        attachment_offset = 0;
                    }

                    AttachmentData& attachment_data = m_frame_graph.m_attachment_data[attachment_index + attachment_offset];

                    VkDescriptorImageInfo& descriptor_image_info = attachment_descriptors[i];
                    descriptor_image_info.sampler = VK_NULL_HANDLE;

                    if (attachment_index == 0) {
                        KW_ASSERT(m_frame_graph.m_swapchain_image_index < SWAPCHAIN_IMAGE_COUNT);
                        descriptor_image_info.imageView = m_frame_graph.m_swapchain_image_views[m_frame_graph.m_swapchain_image_index];
                    } else {
                        KW_ASSERT(attachment_data.sampled_view != VK_NULL_HANDLE);
                        descriptor_image_info.imageView = attachment_data.sampled_view;
                    }

                    if ((attachment_data.usage_mask & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
                        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    } else {
                        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                    }
                }

                VkWriteDescriptorSet write_descriptor_set{};
                write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_descriptor_set.dstSet = descriptor_set;
                write_descriptor_set.dstBinding = 0;
                write_descriptor_set.dstArrayElement = 0;
                write_descriptor_set.descriptorCount = static_cast<uint32_t>(attachment_descriptors.size());
                write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write_descriptor_set.pImageInfo = attachment_descriptors.data();
                write_descriptor_set.pBufferInfo = nullptr;
                write_descriptor_set.pTexelBufferView = nullptr;
                write_descriptor_sets.push_back(write_descriptor_set);
            }

            //
            // Fill texture descriptors.
            //

            Vector<VkDescriptorImageInfo> texture_descriptors(graphics_pipeline_data.uniform_texture_mapping.size(), m_frame_graph.m_render.transient_memory_resource);
            if (!texture_descriptors.empty()) {
                for (size_t i = 0; i < graphics_pipeline_data.uniform_texture_mapping.size(); i++) {
                    uint32_t uniform_texture_mapping = graphics_pipeline_data.uniform_texture_mapping[i];
                    KW_ASSERT(uniform_texture_mapping < descriptor.uniform_texture_count);

                    const TextureVulkan* texture_vulkan = reinterpret_cast<const TextureVulkan*>(descriptor.uniform_textures[uniform_texture_mapping]);
                    KW_ASSERT(texture_vulkan != nullptr);

                    VkDescriptorImageInfo& descriptor_image_info = texture_descriptors[i];
                    descriptor_image_info.sampler = VK_NULL_HANDLE;
                    descriptor_image_info.imageView = texture_vulkan->image_view;
                    descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                VkWriteDescriptorSet write_descriptor_set{};
                write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_descriptor_set.dstSet = descriptor_set;
                write_descriptor_set.dstBinding = graphics_pipeline_data.uniform_texture_first_binding;
                write_descriptor_set.dstArrayElement = 0;
                write_descriptor_set.descriptorCount = static_cast<uint32_t>(texture_descriptors.size());
                write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write_descriptor_set.pImageInfo = texture_descriptors.data();
                write_descriptor_set.pBufferInfo = nullptr;
                write_descriptor_set.pTexelBufferView = nullptr;

                write_descriptor_sets.push_back(write_descriptor_set);
            }

            //
            // Fill uniform buffer descriptors.
            //

            Vector<VkDescriptorBufferInfo> uniform_buffer_descriptors(graphics_pipeline_data.uniform_buffer_mapping.size(), m_frame_graph.m_render.transient_memory_resource);
            if (!uniform_buffer_descriptors.empty()) {
                for (size_t i = 0; i < graphics_pipeline_data.uniform_buffer_mapping.size(); i++) {
                    uint32_t uniform_buffer_mapping = graphics_pipeline_data.uniform_buffer_mapping[i];
                    KW_ASSERT(uniform_buffer_mapping < descriptor.uniform_buffer_count);

                    const BufferVulkan* buffer_vulkan = reinterpret_cast<const BufferVulkan*>(descriptor.uniform_buffers[uniform_buffer_mapping]);
                    KW_ASSERT(buffer_vulkan != nullptr);

                    VkDescriptorBufferInfo& descriptor_buffer_info = uniform_buffer_descriptors[i];
                    descriptor_buffer_info.buffer = buffer_vulkan->buffer;
                    descriptor_buffer_info.offset = 0;
                    descriptor_buffer_info.range = graphics_pipeline_data.uniform_buffer_sizes[i];
                }

                VkWriteDescriptorSet write_descriptor_set{};
                write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write_descriptor_set.dstSet = descriptor_set;
                write_descriptor_set.dstBinding = graphics_pipeline_data.uniform_buffer_first_binding;
                write_descriptor_set.dstArrayElement = 0;
                write_descriptor_set.descriptorCount = static_cast<uint32_t>(uniform_buffer_descriptors.size());
                write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                write_descriptor_set.pImageInfo = nullptr;
                write_descriptor_set.pBufferInfo = uniform_buffer_descriptors.data();
                write_descriptor_set.pTexelBufferView = nullptr;
                write_descriptor_sets.push_back(write_descriptor_set);
            }

            //
            // Update descriptor set and insert it to bound descriptor sets map.
            //

            vkUpdateDescriptorSets(m_frame_graph.m_render.device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);

            {
                std::lock_guard bound_descriptor_sets_lock(graphics_pipeline_data.bound_descriptor_sets_mutex);

                graphics_pipeline_data.bound_descriptor_sets.emplace(crc, DescriptorSetData(descriptor_set, m_frame_graph.m_frame_index));
            }
        } else {
            // Found matching descriptor set.
            descriptor_set = bound_descriptor_set_it->second.descriptor_set;

            // Postpone descriptor set's retirement.
            bound_descriptor_set_it->second.last_frame_usage = m_frame_graph.m_frame_index;
        }
    }

    //
    // Bind descriptor set.
    //

    if (descriptor_set != VK_NULL_HANDLE) {
        Vector<uint32_t> dynamic_offsets(graphics_pipeline_data.uniform_buffer_mapping.size(), m_frame_graph.m_render.transient_memory_resource);

        for (size_t i = 0; i < graphics_pipeline_data.uniform_buffer_mapping.size(); i++) {
            uint32_t uniform_buffer_mapping = graphics_pipeline_data.uniform_buffer_mapping[i];
            KW_ASSERT(uniform_buffer_mapping < descriptor.uniform_buffer_count);

            const UniformBufferVulkan* uniform_buffer_vulkan = static_cast<const UniformBufferVulkan*>(descriptor.uniform_buffers[uniform_buffer_mapping]);
            KW_ASSERT(uniform_buffer_vulkan != nullptr);

            dynamic_offsets[i] = uniform_buffer_vulkan->transient_buffer_offset;
        }

        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_data.pipeline_layout, 0,
            1, &descriptor_set, static_cast<uint32_t>(dynamic_offsets.size()), dynamic_offsets.data()
        );
    }

    //
    // Push constants.
    //

    if (graphics_pipeline_data.push_constants_visibility != 0) {
        vkCmdPushConstants(
            command_buffer, graphics_pipeline_data.pipeline_layout,
            graphics_pipeline_data.push_constants_visibility, 0, graphics_pipeline_data.push_constants_size, descriptor.push_constants
        );
    }

    //
    // Draw.
    //

    vkCmdDrawIndexed(command_buffer, descriptor.index_count, std::max(descriptor.instance_count, 1U), descriptor.index_offset, descriptor.vertex_offset, descriptor.instance_offset);
}

uint32_t FrameGraphVulkan::RenderPassContextVulkan::get_attachment_width() const {
    return m_framebuffer_width;
}

uint32_t FrameGraphVulkan::RenderPassContextVulkan::get_attachment_height() const {
    return m_framebuffer_height;
}

uint32_t FrameGraphVulkan::RenderPassContextVulkan::get_attachemnt_index() const {
    return m_framebuffer_index;
}

void FrameGraphVulkan::RenderPassContextVulkan::reset() {
    command_buffer = VK_NULL_HANDLE;
    transfer_semaphore_value = 0;
    m_framebuffer_width = m_frame_graph.m_render_pass_data[m_render_pass_index].framebuffer_width;
    m_framebuffer_height  = m_frame_graph.m_render_pass_data[m_render_pass_index].framebuffer_height;
    m_graphics_pipeline_index = UINT32_MAX;
}

bool FrameGraphVulkan::RenderPassContextVulkan::allocate_descriptor_sets(GraphicsPipelineData& graphics_pipeline_data) {
    std::lock_guard descriptor_pools_lock(m_frame_graph.m_descriptor_pools_mutex);

    for (DescriptorPoolData& descriptor_pool_data : m_frame_graph.m_descriptor_pools) {
        KW_ASSERT(graphics_pipeline_data.descriptor_set_count > 0);

        uint32_t descriptor_sets_to_allocate = std::min(graphics_pipeline_data.descriptor_set_count, descriptor_pool_data.descriptor_sets_left);

        if (!graphics_pipeline_data.uniform_attachment_mapping.empty() || !graphics_pipeline_data.uniform_texture_mapping.empty()) {
            descriptor_sets_to_allocate = std::min(descriptor_sets_to_allocate, descriptor_pool_data.textures_left / static_cast<uint32_t>(graphics_pipeline_data.uniform_attachment_mapping.size() + graphics_pipeline_data.uniform_texture_mapping.size()));
        }

        if (!graphics_pipeline_data.uniform_attachment_mapping.empty()) {
            descriptor_sets_to_allocate = std::min(descriptor_sets_to_allocate, descriptor_pool_data.uniform_buffers_left / static_cast<uint32_t>(graphics_pipeline_data.uniform_attachment_mapping.size()));
        }

        if (descriptor_sets_to_allocate > 0) {
            graphics_pipeline_data.descriptor_set_count += descriptor_sets_to_allocate;

            KW_ASSERT(descriptor_sets_to_allocate <= descriptor_pool_data.descriptor_sets_left);
            descriptor_pool_data.descriptor_sets_left -= descriptor_sets_to_allocate;

            KW_ASSERT(descriptor_sets_to_allocate * (graphics_pipeline_data.uniform_attachment_mapping.size() + graphics_pipeline_data.uniform_texture_mapping.size()) <= descriptor_pool_data.textures_left);
            descriptor_pool_data.textures_left -= descriptor_sets_to_allocate * static_cast<uint32_t>(graphics_pipeline_data.uniform_attachment_mapping.size() + graphics_pipeline_data.uniform_texture_mapping.size());
            
            KW_ASSERT(descriptor_sets_to_allocate * (graphics_pipeline_data.uniform_samplers.size()) <= descriptor_pool_data.samplers_left);
            descriptor_pool_data.samplers_left -= descriptor_sets_to_allocate * static_cast<uint32_t>(graphics_pipeline_data.uniform_samplers.size());

            KW_ASSERT(descriptor_sets_to_allocate * graphics_pipeline_data.uniform_buffer_mapping.size() <= descriptor_pool_data.uniform_buffers_left);
            descriptor_pool_data.uniform_buffers_left -= descriptor_sets_to_allocate * static_cast<uint32_t>(graphics_pipeline_data.uniform_buffer_mapping.size());

            Vector<VkDescriptorSetLayout> descriptor_set_layouts(m_frame_graph.m_render.transient_memory_resource);
            descriptor_set_layouts.resize(descriptor_sets_to_allocate, graphics_pipeline_data.descriptor_set_layout);

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info{};
            descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool = descriptor_pool_data.descriptor_pool;
            descriptor_set_allocate_info.descriptorSetCount = descriptor_sets_to_allocate;
            descriptor_set_allocate_info.pSetLayouts = descriptor_set_layouts.data();

            graphics_pipeline_data.unbound_descriptor_sets.resize(descriptor_sets_to_allocate, VK_NULL_HANDLE);

            VK_ERROR(
                vkAllocateDescriptorSets(m_frame_graph.m_render.device, &descriptor_set_allocate_info, graphics_pipeline_data.unbound_descriptor_sets.data()),
                "Failed to allocate descriptor sets."
            );

            return true;
        }
    }

    return false;
}

FrameGraphVulkan::RenderPassImplVulkan::RenderPassImplVulkan(FrameGraphVulkan& frame_graph, uint32_t render_pass_index)
    : contexts(frame_graph.m_render.persistent_memory_resource)
    , blit_command_buffer(VK_NULL_HANDLE)
    , m_frame_graph(frame_graph)
    , m_render_pass_index(render_pass_index)
{
    KW_ASSERT(m_render_pass_index < m_frame_graph.m_render_pass_data.size());

    RenderPassData& render_pass_data = m_frame_graph.m_render_pass_data[m_render_pass_index];
    KW_ASSERT(!render_pass_data.attachment_indices.empty());

    uint32_t attachment_index = render_pass_data.attachment_indices[0];
    KW_ASSERT(attachment_index < m_frame_graph.m_attachment_descriptors.size());

    const AttachmentDescriptor& attachment_descriptor = m_frame_graph.m_attachment_descriptors[attachment_index];
    KW_ASSERT(attachment_descriptor.count > 0);

    contexts.reserve(attachment_descriptor.count);

    for (uint32_t i = 0; i < attachment_descriptor.count; i++) {
        contexts.push_back(RenderPassContextVulkan{ m_frame_graph, render_pass_index, i });
    }
}

FrameGraphVulkan::RenderPassContextVulkan* FrameGraphVulkan::RenderPassImplVulkan::begin(uint32_t framebuffer_index) {
    KW_ASSERT(framebuffer_index < contexts.size());

    //
    // Swapchain image index is set to `UINT32_MAX` when window is minimized.
    //

    if (m_frame_graph.m_swapchain_image_index == UINT32_MAX) {
        return nullptr;
    }

    //
    // Acquire a command buffer and begin its recording.
    //

    VkCommandBuffer command_buffer = acquire_command_buffer();

    VkCommandBufferBeginInfo command_buffer_begin_info{};
    command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_ERROR(
        vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info),
        "Failed to begin frame command buffer."
    );

    //
    // After swapchain recreation attachment layouts must be set.
    //

    if (!m_frame_graph.m_is_attachment_layout_set && m_render_pass_index == 0 && framebuffer_index == 0) {
        Vector<VkImageMemoryBarrier> image_memory_barriers(m_frame_graph.m_attachment_data.size(), m_frame_graph.m_render.transient_memory_resource);
        for (size_t attachment_index = 0; attachment_index < m_frame_graph.m_attachment_data.size(); attachment_index++) {
            const AttachmentDescriptor& attachment_descriptor = m_frame_graph.m_attachment_descriptors[attachment_index];
            AttachmentData& attachment_data = m_frame_graph.m_attachment_data[attachment_index];

            VkAccessFlags initial_access_mask = attachment_data.initial_access_mask;
            VkImageLayout initial_layout = attachment_data.initial_layout;
            if (initial_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                if (attachment_index == 0) {
                    // Swapchain attachment is never written, just present garbage.
                    initial_access_mask = 0;
                    initial_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                } else {
                    // This happens only to attachments that are never read and written.
                    initial_access_mask = VK_ACCESS_SHADER_READ_BIT;
                    initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            }

            VkImage attachment_image = attachment_index == 0 ? m_frame_graph.m_swapchain_images[m_frame_graph.m_swapchain_image_index] : attachment_data.image;
            KW_ASSERT(attachment_image != VK_NULL_HANDLE);

            VkImageAspectFlags aspect_mask;
            if (attachment_descriptor.format == TextureFormat::D24_UNORM_S8_UINT || attachment_descriptor.format == TextureFormat::D32_FLOAT_S8X24_UINT) {
                aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            } else if (attachment_descriptor.format == TextureFormat::D16_UNORM || attachment_descriptor.format == TextureFormat::D32_FLOAT) {
                aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
            } else {
                aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
            }

            VkImageSubresourceRange image_subresource_range{};
            image_subresource_range.aspectMask = aspect_mask;
            image_subresource_range.baseMipLevel = 0;
            image_subresource_range.levelCount = VK_REMAINING_MIP_LEVELS;
            image_subresource_range.baseArrayLayer = 0;
            image_subresource_range.layerCount = VK_REMAINING_ARRAY_LAYERS;

            VkImageMemoryBarrier& image_memory_barrier = image_memory_barriers[attachment_index];
            image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_memory_barrier.srcAccessMask = 0;
            image_memory_barrier.dstAccessMask = initial_access_mask;
            image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            image_memory_barrier.newLayout = initial_layout;
            image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_memory_barrier.image = attachment_image;
            image_memory_barrier.subresourceRange = image_subresource_range;
        }

        vkCmdPipelineBarrier(
            command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0,
            0, nullptr, 0, nullptr, static_cast<uint32_t>(image_memory_barriers.size()), image_memory_barriers.data()
        );

        m_frame_graph.m_is_attachment_layout_set = true;
    }

    //
    // Begin render pass.
    //

    RenderPassData& render_pass_data = m_frame_graph.m_render_pass_data[m_render_pass_index];
    KW_ASSERT(render_pass_data.render_pass != VK_NULL_HANDLE);
    KW_ASSERT(render_pass_data.framebuffer_width > 0);
    KW_ASSERT(render_pass_data.framebuffer_height > 0);

    VkFramebuffer framebuffer;

    if (contexts.size() == 1 && render_pass_data.framebuffers.size() == SWAPCHAIN_IMAGE_COUNT) {
        KW_ASSERT(framebuffer_index == 0);
        framebuffer = render_pass_data.framebuffers[m_frame_graph.m_swapchain_image_index];
    } else {
        KW_ASSERT(framebuffer_index < render_pass_data.framebuffers.size());
        framebuffer = render_pass_data.framebuffers[framebuffer_index];
    }

    VkRect2D render_area{};
    render_area.offset.x = 0;
    render_area.offset.y = 0;
    render_area.extent.width = render_pass_data.framebuffer_width;
    render_area.extent.height = render_pass_data.framebuffer_height;

    Vector<VkClearValue> clear_values(render_pass_data.attachment_indices.size(), m_frame_graph.m_render.transient_memory_resource);
    for (size_t i = 0; i < render_pass_data.attachment_indices.size(); i++) {
        size_t attachment_index = render_pass_data.attachment_indices[i];
        KW_ASSERT(attachment_index < m_frame_graph.m_attachment_descriptors.size());

        AttachmentDescriptor& attachment_descriptor = m_frame_graph.m_attachment_descriptors[attachment_index];
        if (TextureFormatUtils::is_depth_stencil(attachment_descriptor.format)) {
            clear_values[i].depthStencil.depth = attachment_descriptor.clear_depth;
            clear_values[i].depthStencil.stencil = attachment_descriptor.clear_stencil;
        } else {
            clear_values[i].color.float32[0] = attachment_descriptor.clear_color[0];
            clear_values[i].color.float32[1] = attachment_descriptor.clear_color[1];
            clear_values[i].color.float32[2] = attachment_descriptor.clear_color[2];
            clear_values[i].color.float32[3] = attachment_descriptor.clear_color[3];
        }
    }

    VkRenderPassBeginInfo render_pass_begin_info{};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = render_pass_data.render_pass;
    render_pass_begin_info.framebuffer = framebuffer;
    render_pass_begin_info.renderArea = render_area;
    render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_begin_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    //
    // Viewport size is equal to framebuffer size.
    //

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = render_pass_data.framebuffer_width;
    viewport.height = render_pass_data.framebuffer_height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    //
    // Set context's command buffer. Must be null handle, because only one begin per context is allowed.
    //

    KW_ASSERT(contexts[framebuffer_index].command_buffer == VK_NULL_HANDLE);
    contexts[framebuffer_index].command_buffer = command_buffer;

    return &contexts[framebuffer_index];
}

uint64_t FrameGraphVulkan::RenderPassImplVulkan::blit(const char* source_attachment, HostTexture* destination_host_texture) {
    KW_ASSERT(source_attachment != nullptr, "Source attachment must be a valid string.");
    KW_ASSERT(destination_host_texture != nullptr, "Destination host texture must be a valid HostTexture.");

    if (blit_command_buffer == VK_NULL_HANDLE) {
        blit_command_buffer = acquire_command_buffer();
        KW_ERROR(blit_command_buffer != VK_NULL_HANDLE);

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_ERROR(
            vkBeginCommandBuffer(blit_command_buffer, &command_buffer_begin_info),
            "Failed to begin blit command buffer."
        );
    }

    HostTextureVulkan* host_texture_vulkan = static_cast<HostTextureVulkan*>(destination_host_texture);
    KW_ASSERT(host_texture_vulkan != nullptr);

    uint32_t attachment_index = 0;
    for (; attachment_index < m_frame_graph.m_attachment_descriptors.size(); attachment_index++) {
        if (std::strcmp(m_frame_graph.m_attachment_descriptors[attachment_index].name, source_attachment) == 0) {
            break;
        }
    }

    KW_ASSERT(
        attachment_index < m_frame_graph.m_attachment_descriptors.size(),
        "Invalid source attachment \"%s\".", source_attachment
    );

    AttachmentDescriptor& attachment_descriptor = m_frame_graph.m_attachment_descriptors[attachment_index];
    AttachmentData& attachment_data = m_frame_graph.m_attachment_data[attachment_index];

    KW_ASSERT(
        attachment_descriptor.format == host_texture_vulkan->get_format(),
        "Blit formats must match."
    );

    VkImageAspectFlags aspect_mask;
    if (TextureFormatUtils::is_depth(attachment_descriptor.format)) {
        // Depth-stencil blit is performed only for depth component.
        aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    uint32_t width;
    uint32_t height;
    if (attachment_descriptor.size_class == SizeClass::RELATIVE) {
        width = static_cast<uint32_t>(attachment_descriptor.width * m_frame_graph.m_swapchain_width);
        height = static_cast<uint32_t>(attachment_descriptor.height * m_frame_graph.m_swapchain_height);
    } else {
        width = static_cast<uint32_t>(attachment_descriptor.width);
        height = static_cast<uint32_t>(attachment_descriptor.height);
    }

    VkImageSubresourceRange image_subresource_range{};
    image_subresource_range.aspectMask = aspect_mask;
    image_subresource_range.baseMipLevel = 0;
    image_subresource_range.levelCount = 1;
    image_subresource_range.baseArrayLayer = 0;
    image_subresource_range.layerCount = 1;

    VkImage attachment_image = attachment_data.image;
    if (attachment_index == 0) {
        attachment_image = m_frame_graph.m_swapchain_images[m_frame_graph.m_swapchain_image_index];
    }
    KW_ASSERT(attachment_image != VK_NULL_HANDLE);

    KW_ASSERT(m_render_pass_index < attachment_data.blit_data.size());
    const AttachmentBarrierData& attachment_barrier_data = attachment_data.blit_data[m_render_pass_index];

    VkBufferMemoryBarrier buffer_acquire_barrier{};
    buffer_acquire_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_acquire_barrier.srcAccessMask = VK_ACCESS_NONE_KHR;
    buffer_acquire_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_acquire_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_acquire_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_acquire_barrier.buffer = host_texture_vulkan->buffer;
    buffer_acquire_barrier.offset = 0;
    buffer_acquire_barrier.size = VK_WHOLE_SIZE;

    VkImageMemoryBarrier image_acquire_barrier{};
    image_acquire_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_acquire_barrier.srcAccessMask = attachment_barrier_data.source_access_mask;
    image_acquire_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_acquire_barrier.oldLayout = attachment_barrier_data.destination_image_layout;
    image_acquire_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_acquire_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_acquire_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_acquire_barrier.image = attachment_image;
    image_acquire_barrier.subresourceRange = image_subresource_range;

    vkCmdPipelineBarrier(
        blit_command_buffer, attachment_barrier_data.source_pipeline_stage_mask, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 1, &buffer_acquire_barrier, 1, &image_acquire_barrier
    );

    VkBufferImageCopy buffer_image_copy{};
    buffer_image_copy.bufferOffset = 0;
    buffer_image_copy.bufferRowLength = 0;
    buffer_image_copy.bufferImageHeight = 0;
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    buffer_image_copy.imageSubresource.layerCount = 1;
    buffer_image_copy.imageOffset = VkOffset3D{};
    buffer_image_copy.imageExtent.width = std::min(width, host_texture_vulkan->get_width());
    buffer_image_copy.imageExtent.height = std::min(height, host_texture_vulkan->get_height());
    buffer_image_copy.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(
        blit_command_buffer, attachment_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host_texture_vulkan->buffer,
        1, &buffer_image_copy
    );

    VkBufferMemoryBarrier buffer_release_barrier{};
    buffer_release_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_release_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_release_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    buffer_release_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_release_barrier.buffer = host_texture_vulkan->buffer;
    buffer_release_barrier.offset = 0;
    buffer_release_barrier.size = VK_WHOLE_SIZE;

    VkImageMemoryBarrier image_release_barrier{};
    image_release_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_release_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_release_barrier.dstAccessMask = attachment_barrier_data.destination_access_mask;
    image_release_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_release_barrier.newLayout = attachment_barrier_data.destination_image_layout;
    image_release_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_release_barrier.image = attachment_image;
    image_release_barrier.subresourceRange = image_subresource_range;

    vkCmdPipelineBarrier(
        blit_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, attachment_barrier_data.destination_pipeline_stage_mask, 0,
        0, nullptr, 1, &buffer_release_barrier, 1, &image_release_barrier
    );

    return m_frame_graph.m_render_finished_timeline_semaphore->value;
}

FrameGraphVulkan::CommandPoolData& FrameGraphVulkan::RenderPassImplVulkan::acquire_command_pool() {
    UnorderedMap<std::thread::id, CommandPoolData>& command_pool_map = m_frame_graph.m_command_pool_data[m_frame_graph.m_semaphore_index];

    {
        std::shared_lock lock(m_frame_graph.m_command_pool_mutex);

        auto result = command_pool_map.find(std::this_thread::get_id());
        if (result != command_pool_map.end()) {
            return result->second;
        }
    }

    {
        std::unique_lock lock(m_frame_graph.m_command_pool_mutex);

        VkCommandPoolCreateInfo command_pool_create_info{};
        command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        command_pool_create_info.queueFamilyIndex = m_frame_graph.m_render.graphics_queue_family_index;

        CommandPoolData command_pool_data(m_frame_graph.m_render.persistent_memory_resource);
        VK_ERROR(
            vkCreateCommandPool(m_frame_graph.m_render.device, &command_pool_create_info, &m_frame_graph.m_render.allocation_callbacks, &command_pool_data.command_pool),
            "Failed to create a command pool."
        );

        return command_pool_map.emplace(std::this_thread::get_id(), std::move(command_pool_data)).first->second;
    }
}

VkCommandBuffer FrameGraphVulkan::RenderPassImplVulkan::acquire_command_buffer() {
    CommandPoolData& command_pool_data = acquire_command_pool();

    size_t command_buffer_index = command_pool_data.current_command_buffer++;

    if (command_buffer_index == command_pool_data.command_buffers.size()) {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.commandPool = command_pool_data.command_pool;
        command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        VK_ERROR(
            vkAllocateCommandBuffers(m_frame_graph.m_render.device, &command_buffer_allocate_info, &command_buffer),
            "Failed to allocate a command buffer."
        );

        command_pool_data.command_buffers.push_back(command_buffer);
    }

    return command_pool_data.command_buffers[command_buffer_index];
}

FrameGraphVulkan::RenderPassData::RenderPassData(MemoryResource& memory_resource)
    : render_pass(VK_NULL_HANDLE)
    , graphics_pipeline_data(memory_resource)
    , framebuffer_width(0)
    , framebuffer_height(0)
    , framebuffers(memory_resource)
    , parallel_block_index(0)
    , attachment_indices(memory_resource)
{
}

FrameGraphVulkan::RenderPassData::RenderPassData(RenderPassData&& other)
    : render_pass(other.render_pass)
    , graphics_pipeline_data(std::move(other.graphics_pipeline_data))
    , framebuffer_width(other.framebuffer_width)
    , framebuffer_height(other.framebuffer_height)
    , framebuffers(std::move(other.framebuffers))
    , parallel_block_index(other.parallel_block_index)
    , attachment_indices(std::move(other.attachment_indices))
    , render_pass_impl(std::move(other.render_pass_impl))
{
}

FrameGraphVulkan::AcquireTask::AcquireTask(FrameGraphVulkan& frame_graph)
    : m_frame_graph(frame_graph)
{
}

void FrameGraphVulkan::AcquireTask::run() {
    //
    // Check whether there's a swapchain to render to.
    //

    if (m_frame_graph.m_swapchain == VK_NULL_HANDLE) {
        m_frame_graph.recreate_swapchain();

        if (m_frame_graph.m_swapchain == VK_NULL_HANDLE) {
            // Most likely the window is minimized. Invalid swapchain image index notifies other subsystems that
            // frame must be skipped.
            m_frame_graph.m_swapchain_image_index = UINT32_MAX;
            return;
        }
    }

    //
    // Wait until command buffers are available for new submission.
    //

    m_frame_graph.m_semaphore_index = m_frame_graph.m_frame_index++ % SWAPCHAIN_IMAGE_COUNT;

    VK_ERROR(
        vkWaitForFences(m_frame_graph.m_render.device, 1, &m_frame_graph.m_fences[m_frame_graph.m_semaphore_index], VK_TRUE, UINT64_MAX),
        "Failed to wait for fences."
    );

    //
    // Wait until swapchain image is available for render.
    //

    VkResult acquire_result = vkAcquireNextImageKHR(
        m_frame_graph.m_render.device,
        m_frame_graph.m_swapchain,
        UINT64_MAX,
        m_frame_graph.m_image_acquired_binary_semaphores[m_frame_graph.m_semaphore_index],
        VK_NULL_HANDLE,
        &m_frame_graph.m_swapchain_image_index
    );

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_frame_graph.recreate_swapchain();

        // Semaphore wasn't signaled, so we don't need to present. Invalid swapchain image index notifies other
        // subsystems that frame must be skipped.
        m_frame_graph.m_swapchain_image_index = UINT32_MAX;

        return;
    } else if (acquire_result != VK_SUBOPTIMAL_KHR) {
        VK_ERROR(acquire_result, "Failed to acquire a swapchain image.");
    }

    //
    // Once we're guaranteed to submit the frame, we can transition the fence to unsignaled state.
    //

    VK_ERROR(
        vkResetFences(m_frame_graph.m_render.device, 1, &m_frame_graph.m_fences[m_frame_graph.m_semaphore_index]),
        "Failed to reset fences."
    );

    //
    // Increment timeline semaphore value, which provides a guarantee that no resources available right now
    // will be destroyed until the frame execution on device is finished.
    //

    m_frame_graph.m_render_finished_timeline_semaphore->value++;

    //
    // Reset command pools.
    //

    for (auto& [_, command_pool_data] : m_frame_graph.m_command_pool_data[m_frame_graph.m_semaphore_index]) {
        KW_ASSERT(command_pool_data.command_pool != VK_NULL_HANDLE);
        VK_ERROR(
            vkResetCommandPool(m_frame_graph.m_render.device, command_pool_data.command_pool, 0),
            "Failed to reset frame command pool."
        );

        // All command buffers are available again.
        command_pool_data.current_command_buffer = 0;
    }

    //
    // Reset render pass contexts.
    //

    for (RenderPassData& render_pass_data : m_frame_graph.m_render_pass_data) {
        for (RenderPassContextVulkan& render_pass_context : render_pass_data.render_pass_impl->contexts) {
            render_pass_context.reset();
        }
        render_pass_data.render_pass_impl->blit_command_buffer = VK_NULL_HANDLE;
    }
}

FrameGraphVulkan::PresentTask::PresentTask(FrameGraphVulkan& frame_graph)
    : m_frame_graph(frame_graph)
{
}

void FrameGraphVulkan::PresentTask::run() {
    //
    // If window is minimized, don't do anything here.
    //

    if (m_frame_graph.m_swapchain_image_index == UINT32_MAX) {
        return;
    }

    //
    // Query render pass command buffers and required transfer semaphore value.
    //

    size_t framebuffer_count = 0;

    for (RenderPassData& render_pass_data : m_frame_graph.m_render_pass_data) {
        KW_ASSERT(render_pass_data.render_pass_impl != nullptr);
        framebuffer_count += render_pass_data.render_pass_impl->contexts.size();

        if (render_pass_data.render_pass_impl->blit_command_buffer != VK_NULL_HANDLE) {
            framebuffer_count++;
        }
    }

    Vector<VkCommandBuffer> render_pass_command_buffers(m_frame_graph.m_render.transient_memory_resource);
    render_pass_command_buffers.reserve(framebuffer_count);

    uint64_t transfer_semaphore_value = 0;

    for (size_t render_pass_index = 0; render_pass_index < m_frame_graph.m_render_pass_data.size(); render_pass_index++) {
        RenderPassData& render_pass_data = m_frame_graph.m_render_pass_data[render_pass_index];
        KW_ASSERT(render_pass_data.parallel_block_index < m_frame_graph.m_parallel_block_data.size());
        KW_ASSERT(render_pass_data.render_pass_impl != nullptr);

        for (size_t framebuffer_index = 0; framebuffer_index < render_pass_data.render_pass_impl->contexts.size(); framebuffer_index++) {
            RenderPassContextVulkan& render_pass_context = render_pass_data.render_pass_impl->contexts[framebuffer_index];

            // Every render pass and framebuffer must begin, because besides draw calls there's a synchronization layer
            // that must execute.
            if (render_pass_context.command_buffer == VK_NULL_HANDLE) {
                render_pass_data.render_pass_impl->begin(static_cast<uint32_t>(framebuffer_index));
                KW_ASSERT(render_pass_context.command_buffer != VK_NULL_HANDLE);
            }

            // Insert pipeline barrier between two render passes.
            if (render_pass_data.render_pass_impl->blit_command_buffer == VK_NULL_HANDLE &&
                framebuffer_index + 1 == render_pass_data.render_pass_impl->contexts.size() &&
                render_pass_index + 1 < m_frame_graph.m_render_pass_data.size() &&
                m_frame_graph.m_render_pass_data[render_pass_index + 1].parallel_block_index != render_pass_data.parallel_block_index)
            {
                ParallelBlockData& parallel_block_data = m_frame_graph.m_parallel_block_data[render_pass_data.parallel_block_index];

                VkMemoryBarrier memory_barrier{};
                memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                memory_barrier.srcAccessMask = parallel_block_data.source_access_mask;
                memory_barrier.dstAccessMask = parallel_block_data.destination_access_mask;

                vkCmdPipelineBarrier(
                    render_pass_context.command_buffer, parallel_block_data.source_stage_mask, parallel_block_data.destination_stage_mask, 0,
                    1, &memory_barrier, 0, nullptr, 0, nullptr
                );
            }

            // End all render passes sequentially here.
            vkCmdEndRenderPass(render_pass_context.command_buffer);
            vkEndCommandBuffer(render_pass_context.command_buffer);

            render_pass_command_buffers.push_back(render_pass_context.command_buffer);

            transfer_semaphore_value = std::max(transfer_semaphore_value, render_pass_context.transfer_semaphore_value);
        }

        if (render_pass_data.render_pass_impl->blit_command_buffer != VK_NULL_HANDLE) {
            // Insert pipeline barrier between two render passes.
            if (render_pass_index + 1 < m_frame_graph.m_render_pass_data.size() &&
                m_frame_graph.m_render_pass_data[render_pass_index + 1].parallel_block_index != render_pass_data.parallel_block_index)
            {
                ParallelBlockData& parallel_block_data = m_frame_graph.m_parallel_block_data[render_pass_data.parallel_block_index];

                VkMemoryBarrier memory_barrier{};
                memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                memory_barrier.srcAccessMask = parallel_block_data.source_access_mask;
                memory_barrier.dstAccessMask = parallel_block_data.destination_access_mask;

                vkCmdPipelineBarrier(
                    render_pass_data.render_pass_impl->blit_command_buffer, parallel_block_data.source_stage_mask, parallel_block_data.destination_stage_mask, 0,
                    1, &memory_barrier, 0, nullptr, 0, nullptr
                );
            }
            
            vkEndCommandBuffer(render_pass_data.render_pass_impl->blit_command_buffer);

            render_pass_command_buffers.push_back(render_pass_data.render_pass_impl->blit_command_buffer);
        }
    }

    //
    // Submit.
    //

    const uint64_t wait_semaphore_values[] = {
        // Wait for image acquire.
        0,
        // Wait for transfer queue.
        transfer_semaphore_value,
        // Wait for previous frame.
        m_frame_graph.m_render_finished_timeline_semaphore->value - 1,
    };

    const uint64_t signal_semaphore_values[] = {
        // Signal render finished for present.
        0,
        // Signal render finished for resource destroy.
        m_frame_graph.m_render_finished_timeline_semaphore->value,
    };

    VkTimelineSemaphoreSubmitInfo timeline_semaphore_submit_info{};
    timeline_semaphore_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_semaphore_submit_info.waitSemaphoreValueCount = static_cast<uint32_t>(std::size(wait_semaphore_values));
    timeline_semaphore_submit_info.pWaitSemaphoreValues = wait_semaphore_values;
    timeline_semaphore_submit_info.signalSemaphoreValueCount = static_cast<uint32_t>(std::size(signal_semaphore_values));
    timeline_semaphore_submit_info.pSignalSemaphoreValues = signal_semaphore_values;

    const VkSemaphore wait_semaphores[] = {
        // Wait for image acquire.
        m_frame_graph.m_image_acquired_binary_semaphores[m_frame_graph.m_semaphore_index],
        // Wait for transfer queue.
        m_frame_graph.m_render.semaphore->semaphore,
        // Wait for previous frame.
        m_frame_graph.m_render_finished_timeline_semaphore->semaphore,
    };

    const VkPipelineStageFlags wait_stage_masks[] = {
        // We will write to acquired image only on color attachment output stage.
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // We may use transfered resources on vertex shader stage and later.
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        // First write access to attachment memory happens in later fragment tests.
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    };

    const VkSemaphore signal_semaphores[] = {
        // Signal render finished for present.
        m_frame_graph.m_render_finished_binary_semaphores[m_frame_graph.m_semaphore_index],
        // Signal render finished for resource destroy.
        m_frame_graph.m_render_finished_timeline_semaphore->semaphore,
    };

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = &timeline_semaphore_submit_info;
    submit_info.waitSemaphoreCount = static_cast<uint32_t>(std::size(wait_semaphores));
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stage_masks;
    submit_info.commandBufferCount = static_cast<uint32_t>(render_pass_command_buffers.size());
    submit_info.pCommandBuffers = render_pass_command_buffers.data();
    submit_info.signalSemaphoreCount = static_cast<uint32_t>(std::size(signal_semaphores));
    submit_info.pSignalSemaphores = signal_semaphores;

    {
        std::lock_guard lock(*m_frame_graph.m_render.graphics_queue_spinlock);

        VK_ERROR(
            vkQueueSubmit(m_frame_graph.m_render.graphics_queue, 1, &submit_info, m_frame_graph.m_fences[m_frame_graph.m_semaphore_index]),
            "Failed to submit."
        );
    }

    //
    // Present.
    //

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &m_frame_graph.m_render_finished_binary_semaphores[m_frame_graph.m_semaphore_index];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &m_frame_graph.m_swapchain;
    present_info.pImageIndices = &m_frame_graph.m_swapchain_image_index;
    present_info.pResults = nullptr;

    VkResult present_result;
    {
        std::lock_guard lock(*m_frame_graph.m_render.graphics_queue_spinlock);

        present_result = vkQueuePresentKHR(m_frame_graph.m_render.graphics_queue, &present_info);
    }

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
        m_frame_graph.recreate_swapchain();
    } else if (present_result != VK_SUBOPTIMAL_KHR) {
        VK_ERROR(present_result, "Failed to present.");
    }

    // These values are valid only between acquire/present calls.
    m_frame_graph.m_semaphore_index = UINT64_MAX;
    m_frame_graph.m_swapchain_image_index = UINT32_MAX;
}

} // namespace kw
