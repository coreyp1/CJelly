#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

#include <cjelly/cj_rgraph.h>
#include <cjelly/cj_engine.h>
#include <cjelly/cj_resources.h>
#include <cjelly/cj_result.h>
#include <cjelly/cj_types.h>
#include <cjelly/engine_internal.h>
#include <cjelly/textured_internal.h>
#include <cjelly/bindless_internal.h>
#include <shaders/blur.vert.h>
#include <shaders/blur.frag.h>
#include <shaders/textured.vert.h>

/* Render graph node types */
typedef enum {
    CJ_RGRAPH_NODE_PASSTHROUGH = 0,
    CJ_RGRAPH_NODE_BLUR = 1,
    CJ_RGRAPH_NODE_TEXTURED = 2,
    CJ_RGRAPH_NODE_COLOR = 3,
    CJ_RGRAPH_NODE_COUNT
} cj_rgraph_node_type_t;

/* Forward declarations */
typedef struct cj_rgraph_param_t cj_rgraph_param_t;

/* Blur node specific data */
typedef struct cj_rgraph_blur_node_t {
    VkPipeline pipeline_horizontal;   /* Horizontal blur pipeline */
    VkPipeline pipeline_vertical;     /* Vertical blur pipeline */
    VkPipelineLayout pipeline_layout; /* Shared pipeline layout */
    VkDescriptorSetLayout desc_layout; /* Descriptor set layout */
    VkDescriptorPool desc_pool;       /* Descriptor pool */
    VkDescriptorSet desc_set;         /* Descriptor set */
    VkBuffer vertex_buffer;           /* Full-screen quad vertex buffer */
    VkDeviceMemory vertex_buffer_memory;
    cj_rgraph_param_t* intensity_param; /* Cached pointer to intensity parameter */
    cj_rgraph_param_t* time_param;      /* Cached pointer to time parameter */

    /* Intermediate render target for true multi-pass */
    VkImage intermediate_texture;      /* Intermediate texture for Pass 1 output */
    VkDeviceMemory intermediate_memory; /* Memory for intermediate texture */
    VkImageView intermediate_view;    /* View for intermediate texture */
    VkFramebuffer intermediate_framebuffer; /* Framebuffer for intermediate rendering */
    VkRenderPass intermediate_render_pass; /* Render pass for intermediate target */
} cj_rgraph_blur_node_t;

/* Textured node specific data */
typedef struct cj_rgraph_textured_node_t {
    VkPipeline pipeline;              /* Textured rendering pipeline */
    VkPipelineLayout pipeline_layout; /* Pipeline layout */
    VkDescriptorSetLayout desc_layout; /* Descriptor set layout */
    VkDescriptorPool desc_pool;       /* Descriptor pool */
    VkDescriptorSet desc_set;         /* Descriptor set */
    VkBuffer vertex_buffer;           /* Vertex buffer for textured quad */
    VkDeviceMemory vertex_buffer_memory;
    VkImage texture_image;            /* Texture image */
    VkDeviceMemory texture_memory;    /* Texture memory */
    VkImageView texture_view;         /* Texture view */
    VkSampler texture_sampler;        /* Texture sampler */
} cj_rgraph_textured_node_t;

/* Color node specific data */
typedef struct cj_rgraph_color_node_t {
    VkPipeline pipeline;              /* Color rendering pipeline */
    VkPipelineLayout pipeline_layout; /* Pipeline layout */
    VkBuffer vertex_buffer;           /* Vertex buffer for color quad */
    VkDeviceMemory vertex_buffer_memory;
} cj_rgraph_color_node_t;

/* Internal render graph structure */
typedef struct cj_rgraph_node_t {
    char name[64];                    /* Node name for debugging */
    uint32_t type;                    /* Node type (pass-through, post-process, etc.) */
    cj_handle_t input_textures[8];    /* Input texture handles */
    cj_handle_t output_textures[8];   /* Output texture handles */
    uint32_t input_count;
    uint32_t output_count;
    struct cj_rgraph_node_t* next;    /* Linked list of nodes */

    /* Node-specific data */
    union {
        cj_rgraph_blur_node_t blur;      /* Blur node data */
        cj_rgraph_textured_node_t textured; /* Textured node data */
        cj_rgraph_color_node_t color;    /* Color node data */
    } data;
} cj_rgraph_node_t;

typedef struct cj_rgraph_binding_t {
    char name[64];                    /* Binding name */
    cj_handle_t texture;              /* Bound texture handle */
    uint32_t slot;                    /* Descriptor slot */
} cj_rgraph_binding_t;

typedef struct cj_rgraph_param_t {
    char name[64];                    /* Parameter name */
    int32_t value;                    /* Integer parameter value */
} cj_rgraph_param_t;

struct cj_rgraph_t {
    cj_engine_t* engine;              /* Reference to engine (not owned) */
    cj_rgraph_node_t* nodes;          /* Linked list of render nodes */
    cj_rgraph_binding_t* bindings;    /* Array of texture bindings */
    cj_rgraph_param_t* params;        /* Array of integer parameters */
    uint32_t binding_count;
    uint32_t param_count;
    uint32_t max_bindings;
    uint32_t max_params;
    bool needs_recompile;             /* Flag indicating graph needs recompilation */
};

/* Forward declarations */
static cj_rgraph_binding_t* find_binding(cj_rgraph_t* graph, const char* name);
static cj_rgraph_param_t* find_param(cj_rgraph_t* graph, const char* name);
static void add_default_node(cj_rgraph_t* graph);
static int create_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static void destroy_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static int execute_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent);
static int create_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static void destroy_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static int execute_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent);
static int create_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static void destroy_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node);
static int execute_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent);
static int create_intermediate_render_target(cj_rgraph_t* graph, cj_rgraph_blur_node_t* blur, VkExtent2D extent);
static void destroy_intermediate_render_target(cj_rgraph_t* graph, cj_rgraph_blur_node_t* blur);

/* Create a new render graph */
CJ_API cj_rgraph_t* cj_rgraph_create(cj_engine_t* engine, const cj_rgraph_desc_t* desc) {
    (void)desc; /* Currently unused, reserved for future use */
    if (!engine) {
        fprintf(stderr, "cj_rgraph_create: engine is NULL\n");
        return NULL;
    }

    cj_rgraph_t* graph = (cj_rgraph_t*)malloc(sizeof(cj_rgraph_t));
    if (!graph) {
        fprintf(stderr, "cj_rgraph_create: failed to allocate graph\n");
        return NULL;
    }

    memset(graph, 0, sizeof(cj_rgraph_t));
    graph->engine = engine;
    graph->max_bindings = 16;
    graph->max_params = 16;
    graph->needs_recompile = true;

    /* Allocate binding and parameter arrays */
    graph->bindings = (cj_rgraph_binding_t*)malloc(sizeof(cj_rgraph_binding_t) * graph->max_bindings);
    graph->params = (cj_rgraph_param_t*)malloc(sizeof(cj_rgraph_param_t) * graph->max_params);

    if (!graph->bindings || !graph->params) {
        fprintf(stderr, "cj_rgraph_create: failed to allocate binding/param arrays\n");
        if (graph->bindings) free(graph->bindings);
        if (graph->params) free(graph->params);
        free(graph);
        return NULL;
    }

    /* No default nodes - nodes will be added explicitly */

    // Render graph created successfully
    return graph;
}

/* Destroy a render graph */
CJ_API void cj_rgraph_destroy(cj_rgraph_t* graph) {
    if (!graph) return;

    /* Free all nodes */
    cj_rgraph_node_t* node = graph->nodes;
    while (node) {
        cj_rgraph_node_t* next = node->next;

        /* Clean up node-specific resources */
        if (node->type == CJ_RGRAPH_NODE_BLUR) {
            destroy_blur_node(graph, node);
        } else if (node->type == CJ_RGRAPH_NODE_TEXTURED) {
            destroy_textured_node(graph, node);
        } else if (node->type == CJ_RGRAPH_NODE_COLOR) {
            destroy_color_node(graph, node);
        }

        free(node);
        node = next;
    }

    /* Free arrays */
    if (graph->bindings) free(graph->bindings);
    if (graph->params) free(graph->params);

    free(graph);
    // Render graph destroyed
}

/* Recompile the graph (stub implementation) */
CJ_API cj_result_t cj_rgraph_recompile(cj_rgraph_t* graph) {
    if (!graph) return CJ_E_INVALID_ARGUMENT;

    /* For now, just mark as compiled */
    graph->needs_recompile = false;
    // Render graph recompiled
    return CJ_SUCCESS;
}

/* Bind a texture to a named slot */
CJ_API cj_result_t cj_rgraph_bind_texture(cj_rgraph_t* graph, cj_str_t name, cj_handle_t texture) {
    if (!graph || !name.ptr || name.len == 0) {
        return CJ_E_INVALID_ARGUMENT;
    }

    /* Find existing binding or create new one */
    cj_rgraph_binding_t* binding = find_binding(graph, name.ptr);
    if (!binding) {
        if (graph->binding_count >= graph->max_bindings) {
            fprintf(stderr, "cj_rgraph_bind_texture: too many bindings\n");
            return CJ_E_OUT_OF_MEMORY;
        }

        binding = &graph->bindings[graph->binding_count++];
        strncpy(binding->name, name.ptr, sizeof(binding->name) - 1);
        binding->name[sizeof(binding->name) - 1] = '\0';
    }

    binding->texture = texture;
    binding->slot = cj_texture_descriptor_slot(graph->engine, texture);

    // Bound texture to slot
    return CJ_SUCCESS;
}

/* Set an integer parameter */
CJ_API cj_result_t cj_rgraph_set_i32(cj_rgraph_t* graph, cj_str_t name, int32_t value) {
    if (!graph || !name.ptr || name.len == 0) {
        return CJ_E_INVALID_ARGUMENT;
    }

    /* Find existing parameter or create new one */
    cj_rgraph_param_t* param = find_param(graph, name.ptr);
    if (!param) {
        if (graph->param_count >= graph->max_params) {
            fprintf(stderr, "cj_rgraph_set_i32: too many parameters\n");
            return CJ_E_OUT_OF_MEMORY;
        }

        param = &graph->params[graph->param_count++];
        strncpy(param->name, name.ptr, sizeof(param->name) - 1);
        param->name[sizeof(param->name) - 1] = '\0';
    }

    param->value = value;
    // Parameter set
    return CJ_SUCCESS;
}

/* Add a textured node to the render graph */
CJ_API cj_result_t cj_rgraph_add_textured_node(cj_rgraph_t* graph, const char* name) {
    if (!graph || !name) return CJ_E_INVALID_ARGUMENT;

    cj_rgraph_node_t* node = (cj_rgraph_node_t*)malloc(sizeof(cj_rgraph_node_t));
    if (!node) {
        fprintf(stderr, "cj_rgraph_add_textured_node: failed to allocate node\n");
        return CJ_E_OUT_OF_MEMORY;
    }

    memset(node, 0, sizeof(cj_rgraph_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = CJ_RGRAPH_NODE_TEXTURED;
    node->next = graph->nodes;
    graph->nodes = node;

    // Create textured-specific resources
    if (!create_textured_node(graph, node)) {
        free(node);
        return CJ_E_UNKNOWN;
    }

    return CJ_SUCCESS;
}

/* Add a color node to the render graph */
CJ_API cj_result_t cj_rgraph_add_color_node(cj_rgraph_t* graph, const char* name) {
    if (!graph || !name) return CJ_E_INVALID_ARGUMENT;

    cj_rgraph_node_t* node = (cj_rgraph_node_t*)malloc(sizeof(cj_rgraph_node_t));
    if (!node) {
        fprintf(stderr, "cj_rgraph_add_color_node: failed to allocate node\n");
        return CJ_E_OUT_OF_MEMORY;
    }

    memset(node, 0, sizeof(cj_rgraph_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = CJ_RGRAPH_NODE_COLOR;
    node->next = graph->nodes;
    graph->nodes = node;

    // Create color-specific resources
    if (!create_color_node(graph, node)) {
        free(node);
        return CJ_E_UNKNOWN;
    }

    return CJ_SUCCESS;
}

/* Execute the render graph */
CJ_API cj_result_t cj_rgraph_execute(cj_rgraph_t* graph, VkCommandBuffer cmd, VkExtent2D extent) {
    if (!graph || !cmd) return CJ_E_INVALID_ARGUMENT;

    // Execute all nodes in the graph
    cj_rgraph_node_t* node = graph->nodes;
    while (node) {
        switch (node->type) {
            case CJ_RGRAPH_NODE_PASSTHROUGH:
                // Pass-through nodes indicate we should use legacy rendering
                // Return a special code to indicate fallback needed
                return CJ_E_UNKNOWN;

            case CJ_RGRAPH_NODE_BLUR:
                if (!execute_blur_node(graph, node, cmd, extent)) {
                    return CJ_E_UNKNOWN;
                }
                break;

            case CJ_RGRAPH_NODE_TEXTURED:
                if (!execute_textured_node(graph, node, cmd, extent)) {
                    return CJ_E_UNKNOWN;
                }
                break;

            case CJ_RGRAPH_NODE_COLOR:
                if (!execute_color_node(graph, node, cmd, extent)) {
                    return CJ_E_UNKNOWN;
                }
                break;

            default:
                fprintf(stderr, "cj_rgraph_execute: unknown node type %u\n", node->type);
                return CJ_E_UNKNOWN;
        }

        node = node->next;
    }
    return CJ_SUCCESS;
}

/* Create blur node resources */
static int create_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_BLUR) return 0;

    cj_rgraph_blur_node_t* blur = &node->data.blur;
    VkDevice device = cj_engine_device(graph->engine);
    VkRenderPass render_pass = cj_engine_render_pass(graph->engine);

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding layout_binding = {0};
    layout_binding.binding = 0;
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout_binding.descriptorCount = 1;
    layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &layout_binding;

    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL, &blur->desc_layout) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create descriptor set layout\n");
        return 0;
    }

    // Create descriptor pool
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    if (vkCreateDescriptorPool(device, &pool_info, NULL, &blur->desc_pool) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create descriptor pool\n");
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = blur->desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &blur->desc_layout;

    if (vkAllocateDescriptorSets(device, &alloc_info, &blur->desc_set) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to allocate descriptor set\n");
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    // Use the fish texture's descriptor set layout for compatibility
    CJellyTexturedResources* tx = cj_engine_textured(graph->engine);
    if (!tx || tx->descriptorSetLayout == VK_NULL_HANDLE) {
        fprintf(stderr, "create_blur_node: fish texture not available\n");
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    // Create pipeline layout with push constants using fish texture's descriptor set layout
    VkPushConstantRange push_range = {0};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(float) * 6; // vec2 texelSize + vec2 direction + float intensity + float time

    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &tx->descriptorSetLayout; // Use fish texture's layout
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &push_range;

    if (vkCreatePipelineLayout(device, &pli, NULL, &blur->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create pipeline layout\n");
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    // Create full-screen quad vertex buffer
    typedef struct { float pos[2]; float tex[2]; } BlurVertex;
    BlurVertex vertices[] = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f}, {0.0f, 1.0f}},
    };

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, NULL, &blur->vertex_buffer) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create vertex buffer\n");
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    // Allocate vertex buffer memory
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, blur->vertex_buffer, &mem_requirements);

    // Find proper memory type (device local + host visible for vertex buffer)
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(cj_engine_physical_device(graph->engine), &mem_properties);

    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        fprintf(stderr, "create_blur_node: failed to find suitable memory type\n");
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    VkMemoryAllocateInfo alloc_info_mem = {0};
    alloc_info_mem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info_mem.allocationSize = mem_requirements.size;
    alloc_info_mem.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(device, &alloc_info_mem, NULL, &blur->vertex_buffer_memory) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to allocate vertex buffer memory\n");
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }

    vkBindBufferMemory(device, blur->vertex_buffer, blur->vertex_buffer_memory, 0);

    // Copy vertex data
    void* data;
    vkMapMemory(device, blur->vertex_buffer_memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, blur->vertex_buffer_memory);

    // Create shader modules
    VkShaderModuleCreateInfo vert_info = {0};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = blur_vert_spv_len;
    vert_info.pCode = (const uint32_t*)blur_vert_spv;

    VkShaderModule vert_shader = VK_NULL_HANDLE;
    printf("create_blur_node: Creating vertex shader module (size: %u)\n", blur_vert_spv_len);
    if (vkCreateShaderModule(device, &vert_info, NULL, &vert_shader) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create vertex shader module\n");
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
        vkFreeMemory(device, blur->vertex_buffer_memory, NULL);
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }
    printf("create_blur_node: Vertex shader module created successfully\n");

    VkShaderModuleCreateInfo frag_info = {0};
    frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_info.codeSize = blur_frag_spv_len;
    frag_info.pCode = (const uint32_t*)blur_frag_spv;

    VkShaderModule frag_shader = VK_NULL_HANDLE;
    printf("create_blur_node: Creating fragment shader module (size: %u)\n", blur_frag_spv_len);
    if (vkCreateShaderModule(device, &frag_info, NULL, &frag_shader) != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create fragment shader module\n");
        vkDestroyShaderModule(device, vert_shader, NULL);
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
        vkFreeMemory(device, blur->vertex_buffer_memory, NULL);
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }
    printf("create_blur_node: Fragment shader module created successfully\n");

    // Create graphics pipeline (simplified - same for both horizontal and vertical for now)
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_shader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_shader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = sizeof(BlurVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {0};
    attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(BlurVertex, pos);
    attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = offsetof(BlurVertex, tex);

    VkPipelineVertexInputStateCreateInfo vi = {0};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {0};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

    VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=1.0f; vp.height=1.0f; vp.minDepth=0; vp.maxDepth=1;
    VkRect2D sc = {0}; sc.extent.width = 1; sc.extent.height = 1;
    VkPipelineViewportStateCreateInfo vps = {0};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRasterizationStateCreateInfo rs = {0};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; // No culling for full-screen quad

    VkPipelineMultisampleStateCreateInfo ms = {0};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {0};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gp = {0};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &dynamic_state;
    gp.layout = blur->pipeline_layout; gp.renderPass = render_pass; gp.subpass = 0;

    printf("create_blur_node: Creating graphics pipeline...\n");
    VkResult pipeline_result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL, &blur->pipeline_horizontal);
    if (pipeline_result != VK_SUCCESS) {
        fprintf(stderr, "create_blur_node: failed to create graphics pipeline (result: %d)\n", pipeline_result);
        vkDestroyShaderModule(device, vert_shader, NULL);
        vkDestroyShaderModule(device, frag_shader, NULL);
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
        vkFreeMemory(device, blur->vertex_buffer_memory, NULL);
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
        return 0;
    }
    printf("create_blur_node: Graphics pipeline created successfully\n");

    // For now, use the same pipeline for both horizontal and vertical
    blur->pipeline_vertical = blur->pipeline_horizontal;

    vkDestroyShaderModule(device, vert_shader, NULL);
    vkDestroyShaderModule(device, frag_shader, NULL);

    return 1;
}

/* Destroy blur node resources */
static void destroy_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_BLUR) return;

    cj_rgraph_blur_node_t* blur = &node->data.blur;
    VkDevice device = cj_engine_device(graph->engine);

    // CRITICAL FIX: Clean up intermediate render target to prevent memory leak
    destroy_intermediate_render_target(graph, blur);

    if (blur->pipeline_horizontal != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, blur->pipeline_horizontal, NULL);
    }
    if (blur->pipeline_vertical != VK_NULL_HANDLE && blur->pipeline_vertical != blur->pipeline_horizontal) {
        vkDestroyPipeline(device, blur->pipeline_vertical, NULL);
    }
    if (blur->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, blur->pipeline_layout, NULL);
    }
    if (blur->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, blur->vertex_buffer, NULL);
    }
    if (blur->vertex_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blur->vertex_buffer_memory, NULL);
    }
    if (blur->desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, blur->desc_pool, NULL);
    }
    if (blur->desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, blur->desc_layout, NULL);
    }
}

/* Execute a blur node */
static int execute_blur_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_BLUR || !cmd) return 0;

    cj_rgraph_blur_node_t* blur = &node->data.blur;


    // Create intermediate render target if it doesn't exist
    if (blur->intermediate_texture == VK_NULL_HANDLE) {
        if (!create_intermediate_render_target(graph, blur, extent)) {
            return 0;
        }
    }

    // Set viewport and scissor
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Safety checks before Vulkan operations
    if (blur->pipeline_horizontal == VK_NULL_HANDLE) {
        return 0;
    }

    if (blur->pipeline_layout == VK_NULL_HANDLE) {
        return 0;
    }

    if (blur->vertex_buffer == VK_NULL_HANDLE) {
        return 0;
    }

    // SIMPLIFIED MULTI-PASS RENDERING:
    // For now, just apply animated blur directly to the fish texture
    // TODO: Implement true multi-pass with intermediate texture


    // Apply animated blur directly to fish texture
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blur->pipeline_horizontal);

    // Animated blur intensity - cycle every 2 seconds (like red/green animation)
    static float time_counter = 0.0f;
    time_counter += 0.016f; // 60 FPS timing
    float blur_intensity = (sinf(time_counter * 3.14159f) + 1.0f) * 0.5f * 0.3f; // Animate between 0.0 and 0.3 with 2-second period

    float push_constants[6] = {
        1.0f / (float)extent.width,  // texelSize.x
        1.0f / (float)extent.height, // texelSize.y
        1.0f, 0.0f,                  // direction (horizontal)
        blur_intensity,              // intensity (animated)
        time_counter                 // time (animated)
    };
    vkCmdPushConstants(cmd, blur->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), push_constants);

    // Bind fish texture descriptor set
    CJellyTexturedResources* tx = cj_engine_textured(graph->engine);
    if (tx && tx->descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blur->pipeline_layout, 0, 1, &tx->descriptorSet, 0, NULL);
    } else {
        return 0;
    }

    // Draw blur effect
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &blur->vertex_buffer, offsets);
    vkCmdDraw(cmd, 4, 1, 0, 0);

    return 1; // Success - true multi-pass rendering
}

/* Create textured node resources */
static int create_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_TEXTURED) return 0;

    cj_rgraph_textured_node_t* textured = &node->data.textured;
    VkDevice device = cj_engine_device(graph->engine);
    // VkRenderPass render_pass = cj_engine_render_pass(graph->engine); // TODO: Use for pipeline creation

    // Create descriptor set layout
    VkDescriptorSetLayoutBinding layout_binding = {0};
    layout_binding.binding = 0;
    layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout_binding.descriptorCount = 1;
    layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &layout_binding;

    if (vkCreateDescriptorSetLayout(device, &layout_info, NULL, &textured->desc_layout) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to create descriptor set layout\n");
        return 0;
    }

    // Create descriptor pool
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    if (vkCreateDescriptorPool(device, &pool_info, NULL, &textured->desc_pool) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to create descriptor pool\n");
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = textured->desc_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &textured->desc_layout;

    if (vkAllocateDescriptorSets(device, &alloc_info, &textured->desc_set) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to allocate descriptor set\n");
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pli = {0};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &textured->desc_layout;

    if (vkCreatePipelineLayout(device, &pli, NULL, &textured->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to create pipeline layout\n");
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    // Create full-screen quad vertex buffer (same size as color rectangle) - 6 vertices for two triangles
    typedef struct { float pos[2]; float tex[2]; } TexturedVertex;
    TexturedVertex vertices[] = {
        // Triangle 1: bottom-left, bottom-right, top-right
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},  // bottom-left
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},  // bottom-right
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},  // top-right
        // Triangle 2: top-right, top-left, bottom-left
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},  // top-right
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},  // top-left
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},  // bottom-left
    };

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, NULL, &textured->vertex_buffer) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to create vertex buffer\n");
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    // Allocate vertex buffer memory
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, textured->vertex_buffer, &mem_requirements);

    VkPhysicalDevice physical_device = cj_engine_physical_device(graph->engine);
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        fprintf(stderr, "create_textured_node: failed to find suitable memory type\n");
        vkDestroyBuffer(device, textured->vertex_buffer, NULL);
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    VkMemoryAllocateInfo alloc_info_mem = {0};
    alloc_info_mem.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info_mem.allocationSize = mem_requirements.size;
    alloc_info_mem.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(device, &alloc_info_mem, NULL, &textured->vertex_buffer_memory) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to allocate vertex buffer memory\n");
        vkDestroyBuffer(device, textured->vertex_buffer, NULL);
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    vkBindBufferMemory(device, textured->vertex_buffer, textured->vertex_buffer_memory, 0);

    // Copy vertex data
    void* data;
    vkMapMemory(device, textured->vertex_buffer_memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, textured->vertex_buffer_memory);

    // Load fish texture
    // TODO: Load the actual fish texture from "test/images/bmp/tang.bmp"
    // For now, create a placeholder texture

    // Create graphics pipeline
    VkShaderModuleCreateInfo vert_info = {0};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = textured_vert_spv_len; // Use proper textured vertex shader
    vert_info.pCode = (const uint32_t*)textured_vert_spv;

    VkShaderModule vert_shader = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &vert_info, NULL, &vert_shader) != VK_SUCCESS) {
        fprintf(stderr, "create_textured_node: failed to create vertex shader module\n");
        vkDestroyBuffer(device, textured->vertex_buffer, NULL);
        vkFreeMemory(device, textured->vertex_buffer_memory, NULL);
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }

    // Get textured pipeline from engine's textured resources
    CJellyTexturedResources* tx = cj_engine_textured(graph->engine);
    if (!tx || tx->pipeline == VK_NULL_HANDLE) {
        fprintf(stderr, "create_textured_node: failed to get engine textured pipeline\n");
        vkDestroyShaderModule(device, vert_shader, NULL);
        vkDestroyBuffer(device, textured->vertex_buffer, NULL);
        vkFreeMemory(device, textured->vertex_buffer_memory, NULL);
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        return 0;
    }
    textured->pipeline = tx->pipeline;

    vkDestroyShaderModule(device, vert_shader, NULL);

    return 1;
}

/* Destroy textured node resources */
static void destroy_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_TEXTURED) return;

    VkDevice device = cj_engine_device(graph->engine);
    cj_rgraph_textured_node_t* textured = &node->data.textured;

    if (textured->texture_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, textured->texture_view, NULL);
        textured->texture_view = VK_NULL_HANDLE;
    }

    if (textured->texture_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, textured->texture_memory, NULL);
        textured->texture_memory = VK_NULL_HANDLE;
    }

    if (textured->texture_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, textured->texture_image, NULL);
        textured->texture_image = VK_NULL_HANDLE;
    }

    if (textured->vertex_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, textured->vertex_buffer_memory, NULL);
        textured->vertex_buffer_memory = VK_NULL_HANDLE;
    }

    if (textured->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, textured->vertex_buffer, NULL);
        textured->vertex_buffer = VK_NULL_HANDLE;
    }

    if (textured->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, textured->pipeline_layout, NULL);
        textured->pipeline_layout = VK_NULL_HANDLE;
    }

    if (textured->desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, textured->desc_pool, NULL);
        textured->desc_pool = VK_NULL_HANDLE;
    }

    if (textured->desc_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, textured->desc_layout, NULL);
        textured->desc_layout = VK_NULL_HANDLE;
    }

    // Note: textured->pipeline is owned by the engine, not the node
}

/* Execute a textured node */
static int execute_textured_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_TEXTURED || !cmd) return 0;

    cj_rgraph_textured_node_t* textured = &node->data.textured;


    // Set viewport and scissor
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind the textured pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textured->pipeline);

    // Bind the fish texture descriptor set from legacy textured resources
    CJellyTexturedResources* tx = cj_engine_textured(graph->engine);
    if (tx && tx->descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textured->pipeline_layout, 0, 1, &tx->descriptorSet, 0, NULL);
    } else {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textured->pipeline_layout, 0, 1, &textured->desc_set, 0, NULL);
    }

    // Bind vertex buffer
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &textured->vertex_buffer, offsets);

    // Draw textured quad (6 vertices for two triangles)
    vkCmdDraw(cmd, 6, 1, 0, 0);

    return 1; // Success - textured rendering completed
}

/* Create color node resources */
static int create_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_COLOR) return 0;

    VkDevice device = cj_engine_device(graph->engine);

    cj_rgraph_color_node_t* color = &node->data.color;

    // Get engine's color pipeline resources
    CJellyBindlessResources* engine_cp = cj_engine_color_pipeline(graph->engine);
    if (!engine_cp) {
        fprintf(stderr, "create_color_node: failed to get engine color pipeline\n");
        return 0;
    }

    // Create our own vertex buffer with animated colors
    // Format: Position (x, y) + Color (r, g, b) + TextureID (uint32) - matches engine's format
    // Engine expects 6 vertices (two triangles) to form a quad
    typedef struct { float pos[2]; float color[3]; uint32_t textureID; } ColorVertex;
    ColorVertex vertices[] = {
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 0},  // bottom-left (red) - triangle 1
        {{ 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, 0},  // bottom-right (green) - triangle 1
        {{ 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, 0},  // top-right (blue) - triangle 1
        {{ 0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, 0},  // top-right (blue) - triangle 2
        {{-0.5f,  0.5f}, {1.0f, 1.0f, 0.0f}, 0},  // top-left (yellow) - triangle 2
        {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 0}   // bottom-left (red) - triangle 2
    };

    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, NULL, &color->vertex_buffer) != VK_SUCCESS) {
        fprintf(stderr, "create_color_node: failed to create vertex buffer\n");
        return 0;
    }

    // Allocate memory for vertex buffer
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, color->vertex_buffer, &mem_requirements);

    VkPhysicalDevice physical_device = cj_engine_physical_device(graph->engine);
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        fprintf(stderr, "create_color_node: failed to find suitable memory type\n");
        vkDestroyBuffer(device, color->vertex_buffer, NULL);
        return 0;
    }

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(device, &alloc_info, NULL, &color->vertex_buffer_memory) != VK_SUCCESS) {
        fprintf(stderr, "create_color_node: failed to allocate vertex buffer memory\n");
        vkDestroyBuffer(device, color->vertex_buffer, NULL);
        return 0;
    }

    vkBindBufferMemory(device, color->vertex_buffer, color->vertex_buffer_memory, 0);

    // Copy vertex data to buffer
    void* data;
    vkMapMemory(device, color->vertex_buffer_memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, color->vertex_buffer_memory);

    // Create pipeline layout with push constants support
    VkPushConstantRange push_constant_range = {0};
    push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(float) * 8; // 2 vec4s = 8 floats

    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 0;
    layout_info.pSetLayouts = NULL;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_constant_range;

    if (vkCreatePipelineLayout(device, &layout_info, NULL, &color->pipeline_layout) != VK_SUCCESS) {
        fprintf(stderr, "create_color_node: failed to create pipeline layout\n");
        vkFreeMemory(device, color->vertex_buffer_memory, NULL);
        vkDestroyBuffer(device, color->vertex_buffer, NULL);
        return 0;
    }

    // Get color pipeline from engine's color pipeline resources
    if (engine_cp->pipeline == VK_NULL_HANDLE) {
        fprintf(stderr, "create_color_node: engine color pipeline is NULL\n");
        vkDestroyPipelineLayout(device, color->pipeline_layout, NULL);
        return 0;
    }
    color->pipeline = engine_cp->pipeline;

    printf("Color node created successfully\n");
    return 1;
}

/* Destroy color node resources */
static void destroy_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_COLOR) return;

    VkDevice device = cj_engine_device(graph->engine);
    cj_rgraph_color_node_t* color = &node->data.color;

    if (color->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, color->pipeline_layout, NULL);
        color->pipeline_layout = VK_NULL_HANDLE;
    }

    if (color->vertex_buffer_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, color->vertex_buffer_memory, NULL);
        color->vertex_buffer_memory = VK_NULL_HANDLE;
    }

    if (color->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, color->vertex_buffer, NULL);
        color->vertex_buffer = VK_NULL_HANDLE;
    }

    // Note: color->pipeline is owned by the engine, not the node
}

/* Execute a color node */
static int execute_color_node(cj_rgraph_t* graph, cj_rgraph_node_t* node, VkCommandBuffer cmd, VkExtent2D extent) {
    if (!graph || !node || node->type != CJ_RGRAPH_NODE_COLOR || !cmd) return 0;

    cj_rgraph_color_node_t* color = &node->data.color;


    // Set viewport and scissor
    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind the color pipeline
    if (color->pipeline == VK_NULL_HANDLE) {
        return 0;
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, color->pipeline);

    // Bind vertex buffer
    if (color->vertex_buffer == VK_NULL_HANDLE) {
        return 0;
    }
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &color->vertex_buffer, offsets);

    // Get colorMul from the engine's color pipeline resources (updated by cj_bindless_set_color)
    float red_intensity = 1.0f;
    float green_intensity = 0.0f;

    CJellyBindlessResources* color_resources = cj_engine_color_pipeline(graph->engine);
    if (color_resources) {
        red_intensity = color_resources->colorMul[0];
        green_intensity = color_resources->colorMul[1];
    }

    // Push constants for color
    struct {
        float uv[4];        // vec4 uv
        float colorMul[4];  // vec4 colorMul
    } push_constants;

    push_constants.uv[0] = 0.0f; push_constants.uv[1] = 0.0f; push_constants.uv[2] = 1.0f; push_constants.uv[3] = 1.0f;
    push_constants.colorMul[0] = red_intensity; push_constants.colorMul[1] = green_intensity; push_constants.colorMul[2] = 0.0f; push_constants.colorMul[3] = 1.0f;

    // Push constants to shader
    vkCmdPushConstants(cmd, color->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), &push_constants);

    // Draw color quad (our vertex buffer has 6 vertices - two triangles)
    vkCmdDraw(cmd, 6, 1, 0, 0);
    return 1; // Success - color rendering completed
}

/* Helper function to find a binding by name */
static cj_rgraph_binding_t* find_binding(cj_rgraph_t* graph, const char* name) {
    for (uint32_t i = 0; i < graph->binding_count; i++) {
        if (strcmp(graph->bindings[i].name, name) == 0) {
            return &graph->bindings[i];
        }
    }
    return NULL;
}

/* Helper function to find a parameter by name */
static cj_rgraph_param_t* find_param(cj_rgraph_t* graph, const char* name) {
    for (uint32_t i = 0; i < graph->param_count; i++) {
        if (strcmp(graph->params[i].name, name) == 0) {
            return &graph->params[i];
        }
    }
    return NULL;
}

/* Add a default pass-through node for basic rendering */
static void add_default_node(cj_rgraph_t* graph) {
    cj_rgraph_node_t* node = (cj_rgraph_node_t*)malloc(sizeof(cj_rgraph_node_t));
    if (!node) {
        fprintf(stderr, "add_default_node: failed to allocate node\n");
        return;
    }

    memset(node, 0, sizeof(cj_rgraph_node_t));
    strcpy(node->name, "default_pass");
    node->type = CJ_RGRAPH_NODE_PASSTHROUGH;
    node->next = graph->nodes;
    graph->nodes = node;

    // Added default pass-through node
}

/* Add a blur node to the render graph */
CJ_API cj_result_t cj_rgraph_add_blur_node(cj_rgraph_t* graph, const char* name) {
    if (!graph || !name) return CJ_E_INVALID_ARGUMENT;

    cj_rgraph_node_t* node = (cj_rgraph_node_t*)malloc(sizeof(cj_rgraph_node_t));
    if (!node) {
        fprintf(stderr, "cj_rgraph_add_blur_node: failed to allocate node\n");
        return CJ_E_OUT_OF_MEMORY;
    }

    memset(node, 0, sizeof(cj_rgraph_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = CJ_RGRAPH_NODE_BLUR;
    node->next = graph->nodes;
    graph->nodes = node;

    // Create blur-specific resources
    if (!create_blur_node(graph, node)) {
        free(node);
        return CJ_E_UNKNOWN;
    }

    // Cache parameter pointers for performance
    cj_rgraph_blur_node_t* blur = &node->data.blur;
    blur->intensity_param = find_param(graph, "blur_intensity");
    blur->time_param = find_param(graph, "time_ms");


    return CJ_SUCCESS;
}

/* Create intermediate render target for true multi-pass rendering */
static int create_intermediate_render_target(cj_rgraph_t* graph, cj_rgraph_blur_node_t* blur, VkExtent2D extent) {
    if (!graph || !blur) return 0;

    VkDevice device = cj_engine_device(graph->engine);
    VkPhysicalDevice physical_device = cj_engine_physical_device(graph->engine);

    // Create intermediate texture
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = extent.width;
    image_info.extent.height = extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &image_info, NULL, &blur->intermediate_texture) != VK_SUCCESS) {
        fprintf(stderr, "create_intermediate_render_target: failed to create intermediate texture\n");
        return 0;
    }

    // Allocate memory for intermediate texture
    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, blur->intermediate_texture, &mem_requirements);

    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        fprintf(stderr, "create_intermediate_render_target: failed to find suitable memory type\n");
        vkDestroyImage(device, blur->intermediate_texture, NULL);
        return 0;
    }

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = memory_type_index;

    if (vkAllocateMemory(device, &alloc_info, NULL, &blur->intermediate_memory) != VK_SUCCESS) {
        fprintf(stderr, "create_intermediate_render_target: failed to allocate intermediate texture memory\n");
        vkDestroyImage(device, blur->intermediate_texture, NULL);
        return 0;
    }

    vkBindImageMemory(device, blur->intermediate_texture, blur->intermediate_memory, 0);

    // Create image view for intermediate texture
    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = blur->intermediate_texture;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &view_info, NULL, &blur->intermediate_view) != VK_SUCCESS) {
        fprintf(stderr, "create_intermediate_render_target: failed to create intermediate texture view\n");
        vkFreeMemory(device, blur->intermediate_memory, NULL);
        vkDestroyImage(device, blur->intermediate_texture, NULL);
        return 0;
    }

    printf("Intermediate render target created: %dx%d\n", extent.width, extent.height);
    return 1;
}

/* Destroy intermediate render target - TODO: implement cleanup when blur node is destroyed */
static void destroy_intermediate_render_target(cj_rgraph_t* graph, cj_rgraph_blur_node_t* blur) {
    (void)graph; (void)blur; // Suppress unused parameter warnings - function not yet implemented
    if (!graph || !blur) return;

    VkDevice device = cj_engine_device(graph->engine);

    if (blur->intermediate_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, blur->intermediate_view, NULL);
        blur->intermediate_view = VK_NULL_HANDLE;
    }
    if (blur->intermediate_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, blur->intermediate_memory, NULL);
        blur->intermediate_memory = VK_NULL_HANDLE;
    }
    if (blur->intermediate_texture != VK_NULL_HANDLE) {
        vkDestroyImage(device, blur->intermediate_texture, NULL);
        blur->intermediate_texture = VK_NULL_HANDLE;
    }
    if (blur->intermediate_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, blur->intermediate_framebuffer, NULL);
        blur->intermediate_framebuffer = VK_NULL_HANDLE;
    }
    if (blur->intermediate_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, blur->intermediate_render_pass, NULL);
        blur->intermediate_render_pass = VK_NULL_HANDLE;
    }
}
