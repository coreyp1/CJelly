#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <cjelly/cj_engine.h>
#include <cjelly/engine_internal.h>
#include <cjelly/runtime.h>
#include <vulkan/vulkan.h>
#include <cjelly/textured_internal.h>
#include <cjelly/bindless_internal.h>
#include <cjelly/bindless_state_internal.h>
#include <cjelly/basic_state_internal.h>
#include <cjelly/cj_handle.h>
#include <cjelly/cj_resources.h>
#include <cjelly/resource_helpers_internal.h>

// Generated shader headers - use extern declarations to avoid multiple definitions
extern unsigned char color_vert_spv[];
extern unsigned int color_vert_spv_len;
extern unsigned char color_frag_spv[];
extern unsigned int color_frag_spv_len;


/* Internal definition of the opaque engine type */
struct cj_engine_t {
  uint32_t selected_device_index;
  uint32_t flags;

  /* Vulkan globals during migration */
  VkInstance instance;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  VkRenderPass render_pass;
  VkCommandPool command_pool;
  VkFormat color_format;

  /* Phase 3: simple resource tables */
  cj_res_entry_t textures[CJ_ENGINE_MAX_TEXTURES];
  cj_res_entry_t buffers[CJ_ENGINE_MAX_BUFFERS];
  cj_res_entry_t samplers[CJ_ENGINE_MAX_SAMPLERS];

  /* Internal-only textured resources (migration) */
  CJellyTexturedResources textured;
  /* Internal-only bindless state (migration) */
  CJellyBindlessState bindless;
  /* Internal-only basic pipeline state (migration) */
  CJellyBasicState basic;

  /* Color-only pipeline state (migration) */
  CJellyBindlessResources color_pipeline;

  /* Shared bindless descriptor resources (engine-owned) */
  VkDescriptorSetLayout bindless_layout;
  VkDescriptorPool      bindless_pool;
};

static cj_engine_t* g_current_engine = NULL;

/* --- Engine-owned Vulkan bootstrap (migration of legacy init) --- */
static int eng_create_instance(cj_engine_t* e, int use_validation) {
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "CJelly";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "CJellyEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  const char* extensions[8];
  uint32_t extCount = 0;
  extensions[extCount++] = "VK_KHR_surface";
#ifdef _WIN32
  extensions[extCount++] = "VK_KHR_win32_surface";
#else
  extensions[extCount++] = "VK_KHR_xlib_surface";
#endif

  VkInstanceCreateInfo ci = {0};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &appInfo;
  ci.enabledExtensionCount = extCount;
  ci.ppEnabledExtensionNames = extensions;
  const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
  if (use_validation) {
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = layers;
  }
  if (vkCreateInstance(&ci, NULL, &e->instance) != VK_SUCCESS) return 0;
  return 1;
}

static int eng_pick_physical_device(cj_engine_t* e) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(e->instance, &count, NULL);
  if (count == 0) return 0;
  VkPhysicalDevice devices[16];
  if (count > 16) count = 16;
  vkEnumeratePhysicalDevices(e->instance, &count, devices);

  /* Simple selection: prefer discrete, else first */
  VkPhysicalDevice best = devices[0];
  int bestScore = -1;
  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(devices[i], &props);
    int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 0;
    score += (int)props.limits.maxImageDimension2D;
    if (score > bestScore) { bestScore = score; best = devices[i]; }
  }
  e->physical_device = best;
  return 1;
}

static int eng_create_logical_device(cj_engine_t* e) {
  uint32_t qCount = 0; VkQueueFamilyProperties qProps[16];
  vkGetPhysicalDeviceQueueFamilyProperties(e->physical_device, &qCount, NULL);
  if (qCount > 16) qCount = 16;
  vkGetPhysicalDeviceQueueFamilyProperties(e->physical_device, &qCount, qProps);
  uint32_t gfxIndex = 0; int found = 0;
  for (uint32_t i = 0; i < qCount; ++i) {
    if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfxIndex = i; found = 1; break; }
  }
  if (!found) return 0;
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {0};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = gfxIndex;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  const char* devExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkDeviceCreateInfo dci = {0};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = devExt;
  if (vkCreateDevice(e->physical_device, &dci, NULL, &e->device) != VK_SUCCESS) return 0;
  vkGetDeviceQueue(e->device, gfxIndex, 0, &e->graphics_queue);
  e->present_queue = e->graphics_queue;
  return 1;
}

static int eng_create_render_pass(cj_engine_t* e) {
  VkAttachmentDescription color = {0};
  VkFormat fmt = (e->color_format != 0) ? e->color_format : VK_FORMAT_B8G8R8A8_SRGB;
  color.format = fmt;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference colorRef = {0}; colorRef.attachment = 0; colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription sub = {0}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &colorRef;
  VkRenderPassCreateInfo rp = {0}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rp.attachmentCount = 1; rp.pAttachments = &color; rp.subpassCount = 1; rp.pSubpasses = &sub;
  if (vkCreateRenderPass(e->device, &rp, NULL, &e->render_pass) != VK_SUCCESS) return 0;
  return 1;
}

/* Recreate engine render pass if format changed or not created */
CJ_API int cj_engine_ensure_render_pass(cj_engine_t* e, VkFormat fmt) {
  if (!e) return 0;
  if (e->render_pass != VK_NULL_HANDLE && e->color_format == fmt) return 1;
  if (e->render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(e->device, e->render_pass, NULL);
    e->render_pass = VK_NULL_HANDLE;
  }
  e->color_format = fmt;
  return eng_create_render_pass(e);
}

static int eng_create_command_pool(cj_engine_t* e) {
  VkCommandPoolCreateInfo pci = {0}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pci.queueFamilyIndex = 0;
  if (vkCreateCommandPool(e->device, &pci, NULL, &e->command_pool) != VK_SUCCESS) return 0;
  return 1;
}

static int eng_ensure_bindless_descriptors(cj_engine_t* e) {
  if (!e) return 0;
  if (e->bindless_layout != VK_NULL_HANDLE && e->bindless_pool != VK_NULL_HANDLE) return 1;
  if (e->bindless_layout == VK_NULL_HANDLE) {
    VkDescriptorSetLayoutBinding layoutBinding = {0};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;
    if (vkCreateDescriptorSetLayout(e->device, &layoutInfo, NULL, &e->bindless_layout) != VK_SUCCESS) return 0;
  }
  if (e->bindless_pool == VK_NULL_HANDLE) {
    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = CJ_ENGINE_MAX_TEXTURES;
    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(e->device, &poolInfo, NULL, &e->bindless_pool) != VK_SUCCESS) return 0;
  }
  return 1;
}

CJ_API cj_engine_t* cj_engine_create(const cj_engine_desc_t* desc) {
  cj_engine_t* engine = (cj_engine_t*)malloc(sizeof(*engine));
  if (!engine) return NULL;
  memset(engine, 0, sizeof(*engine));
  engine->flags = desc ? desc->flags : 0u;
  if (desc && desc->device_select == CJ_DEVICE_SELECT_INDEX) {
    engine->selected_device_index = desc->requested_device_index;
  } else {
    engine->selected_device_index = 0u; /* default device index for now */
  }
  return engine;
}

CJ_API void cj_engine_shutdown(cj_engine_t* engine) {
  if (!engine) return;
  if (g_current_engine == engine) g_current_engine = NULL;
  free(engine);
}

CJ_API void cj_engine_wait_idle(cj_engine_t* engine) {
  (void)engine; /* no-op in stub */
}

static uint32_t eng_find_memory_type(cj_engine_t* e, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(e->physical_device, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  return 0; // fallback
}

static int eng_create_color_pipeline(cj_engine_t* e) {
  if (!e) return 0;
  CJellyBindlessResources* cp = &e->color_pipeline;

  // Create vertex buffer for color-only quad
  typedef struct { float pos[2]; float color[3]; uint32_t textureID; } VertexBindless;
  VertexBindless vertices[] = {
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{ 0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, 0},
    {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, 0},
  };
  VkDeviceSize vbSize = sizeof(vertices);

  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = vbSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(e->device, &bufferInfo, NULL, &cp->vertexBuffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create color pipeline vertex buffer\n");
    return 0;
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(e->device, cp->vertexBuffer, &memRequirements);
  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = eng_find_memory_type(e, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (vkAllocateMemory(e->device, &allocInfo, NULL, &cp->vertexBufferMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate color pipeline vertex buffer memory\n");
    return 0;
  }
  vkBindBufferMemory(e->device, cp->vertexBuffer, cp->vertexBufferMemory, 0);

  void* vdata = NULL;
  vkMapMemory(e->device, cp->vertexBufferMemory, 0, vbSize, 0, &vdata);
  memcpy(vdata, vertices, (size_t)vbSize);
  vkUnmapMemory(e->device, cp->vertexBufferMemory);

  // Create pipeline layout with push constants only
  VkPushConstantRange pushRange = {0};
  pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(float) * 8; // uv vec4 + colorMul vec4

  VkPipelineLayoutCreateInfo pli = {0};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 0;
  pli.pSetLayouts = NULL;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(e->device, &pli, NULL, &cp->pipelineLayout) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create color pipeline layout\n");
    return 0;
  }

  // Create shader modules (using generated headers)

  VkShaderModuleCreateInfo vertInfo = {0};
  vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  vertInfo.codeSize = color_vert_spv_len;
  vertInfo.pCode = (const uint32_t*)color_vert_spv;
  VkShaderModule vert = VK_NULL_HANDLE;
  if (vkCreateShaderModule(e->device, &vertInfo, NULL, &vert) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create color vertex shader module\n");
    return 0;
  }

  VkShaderModuleCreateInfo fragInfo = {0};
  fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  fragInfo.codeSize = color_frag_spv_len;
  fragInfo.pCode = (const uint32_t*)color_frag_spv;
  VkShaderModule frag = VK_NULL_HANDLE;
  if (vkCreateShaderModule(e->device, &fragInfo, NULL, &frag) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create color fragment shader module\n");
    vkDestroyShaderModule(e->device, vert, NULL);
    return 0;
  }

  // Create pipeline
  VkPipelineShaderStageCreateInfo stages[2] = {0};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding = {0};
  binding.binding = 0;
  binding.stride = sizeof(VertexBindless);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[3] = {0};
  attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(VertexBindless, pos);
  attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(VertexBindless, color);
  attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32_UINT; attrs[2].offset = offsetof(VertexBindless, textureID);

  VkPipelineVertexInputStateCreateInfo vi = {0};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia = {0};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport vp = {0}; vp.x=0; vp.y=0; vp.width=1.0f; vp.height=1.0f; vp.minDepth=0; vp.maxDepth=1;
  VkRect2D sc = {0}; sc.extent.width = 1; sc.extent.height = 1;
  VkPipelineViewportStateCreateInfo vps = {0};
  vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

  // Enable dynamic viewport and scissor
  VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState = {0};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkPipelineRasterizationStateCreateInfo rs = {0};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.lineWidth = 1.0f;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  rs.frontFace = VK_FRONT_FACE_CLOCKWISE;

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
  gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vps; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &dynamicState;
  gp.layout = cp->pipelineLayout; gp.renderPass = e->render_pass; gp.subpass = 0;

  if (vkCreateGraphicsPipelines(e->device, VK_NULL_HANDLE, 1, &gp, NULL, &cp->pipeline) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create color graphics pipeline\n");
    vkDestroyShaderModule(e->device, vert, NULL);
    vkDestroyShaderModule(e->device, frag, NULL);
    return 0;
  }

  vkDestroyShaderModule(e->device, vert, NULL);
  vkDestroyShaderModule(e->device, frag, NULL);

  cp->uv[0]=1.0f; cp->uv[1]=1.0f; cp->uv[2]=0.0f; cp->uv[3]=0.0f;
  cp->colorMul[0]=1.0f; cp->colorMul[1]=1.0f; cp->colorMul[2]=1.0f; cp->colorMul[3]=1.0f;
  return 1;
}

/* Initialize Vulkan into the engine using the existing context bootstrap */
CJ_API int cj_engine_init_vulkan(cj_engine_t* engine, int use_validation) {
  if (!engine) return 0;
  if (!eng_create_instance(engine, use_validation)) return 0;
  if (!eng_pick_physical_device(engine)) return 0;
  if (!eng_create_logical_device(engine)) return 0;
  if (!eng_create_render_pass(engine)) return 0;
  if (!eng_create_command_pool(engine)) return 0;
  if (!eng_ensure_bindless_descriptors(engine)) return 0;
  if (!eng_create_color_pipeline(engine)) {
    fprintf(stderr, "Failed to create color pipeline\n");
    return 0;
  }
  return 1;
}

/* Shutdown Vulkan from the engine */
CJ_API void cj_engine_shutdown_vulkan(cj_engine_t* engine) {
  if (!engine) return;
  VkDevice dev = engine->device;
  if (dev != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(dev);
    /* Destroy internal pipelines/buffers owned by engine (migration containers) */
    /* Textured */
    {
      CJellyTexturedResources* tx = &engine->textured;
      if (tx->pipeline) vkDestroyPipeline(dev, tx->pipeline, NULL);
      if (tx->pipelineLayout) vkDestroyPipelineLayout(dev, tx->pipelineLayout, NULL);
      if (tx->vertexBuffer) vkDestroyBuffer(dev, tx->vertexBuffer, NULL);
      if (tx->vertexBufferMemory) vkFreeMemory(dev, tx->vertexBufferMemory, NULL);
      if (tx->imageView) vkDestroyImageView(dev, tx->imageView, NULL);
      if (tx->sampler) vkDestroySampler(dev, tx->sampler, NULL);
      if (tx->image) vkDestroyImage(dev, tx->image, NULL);
      if (tx->imageMemory) vkFreeMemory(dev, tx->imageMemory, NULL);
      if (tx->descriptorPool) vkDestroyDescriptorPool(dev, tx->descriptorPool, NULL);
      if (tx->descriptorSetLayout) vkDestroyDescriptorSetLayout(dev, tx->descriptorSetLayout, NULL);
      memset(tx, 0, sizeof(*tx));
    }
    /* Bindless */
    {
      CJellyBindlessState* bl = &engine->bindless;
      if (bl->pipeline) vkDestroyPipeline(dev, bl->pipeline, NULL);
      if (bl->pipelineLayout) vkDestroyPipelineLayout(dev, bl->pipelineLayout, NULL);
      if (bl->vertexBuffer) vkDestroyBuffer(dev, bl->vertexBuffer, NULL);
      if (bl->vertexBufferMemory) vkFreeMemory(dev, bl->vertexBufferMemory, NULL);
      memset(bl, 0, sizeof(*bl));
    }
    /* Color pipeline */
    {
      CJellyBindlessResources* cp = &engine->color_pipeline;
      if (cp->pipeline) vkDestroyPipeline(dev, cp->pipeline, NULL);
      if (cp->pipelineLayout) vkDestroyPipelineLayout(dev, cp->pipelineLayout, NULL);
      if (cp->vertexBuffer) vkDestroyBuffer(dev, cp->vertexBuffer, NULL);
      if (cp->vertexBufferMemory) vkFreeMemory(dev, cp->vertexBufferMemory, NULL);
      memset(cp, 0, sizeof(*cp));
    }
    /* Basic */
    {
      CJellyBasicState* bs = &engine->basic;
      if (bs->pipeline) vkDestroyPipeline(dev, bs->pipeline, NULL);
      if (bs->pipelineLayout) vkDestroyPipelineLayout(dev, bs->pipelineLayout, NULL);
      if (bs->vertexBuffer) vkDestroyBuffer(dev, bs->vertexBuffer, NULL);
      if (bs->vertexBufferMemory) vkFreeMemory(dev, bs->vertexBufferMemory, NULL);
      memset(bs, 0, sizeof(*bs));
    }
    /* Destroy all remaining resources in resource tables */
    for (uint32_t i = 0; i < CJ_ENGINE_MAX_TEXTURES; i++) {
      if (engine->textures[i].in_use) {
        cj_engine_destroy_texture(engine, i);
      }
    }
    for (uint32_t i = 0; i < CJ_ENGINE_MAX_BUFFERS; i++) {
      if (engine->buffers[i].in_use) {
        cj_engine_destroy_buffer(engine, i);
      }
    }
    for (uint32_t i = 0; i < CJ_ENGINE_MAX_SAMPLERS; i++) {
      if (engine->samplers[i].in_use) {
        cj_engine_destroy_sampler(engine, i);
      }
    }

    if (engine->command_pool) { vkDestroyCommandPool(dev, engine->command_pool, NULL); engine->command_pool = VK_NULL_HANDLE; }
    if (engine->bindless_pool) { vkDestroyDescriptorPool(dev, engine->bindless_pool, NULL); engine->bindless_pool = VK_NULL_HANDLE; }
    if (engine->bindless_layout) { vkDestroyDescriptorSetLayout(dev, engine->bindless_layout, NULL); engine->bindless_layout = VK_NULL_HANDLE; }
    if (engine->render_pass) { vkDestroyRenderPass(dev, engine->render_pass, NULL); engine->render_pass = VK_NULL_HANDLE; }
    vkDestroyDevice(dev, NULL);
    engine->device = VK_NULL_HANDLE;
  }
  if (engine->instance != VK_NULL_HANDLE) {
    vkDestroyInstance(engine->instance, NULL);
    engine->instance = VK_NULL_HANDLE;
  }
}

/* Public API aliases */
CJ_API int  cj_engine_init(cj_engine_t* engine, int use_validation) { return cj_engine_init_vulkan(engine, use_validation); }
CJ_API void cj_engine_shutdown_device(cj_engine_t* engine) { cj_engine_shutdown_vulkan(engine); }
CJ_API void cj_engine_export_context(cj_engine_t* engine, CJellyVulkanContext* out_ctx) {
  if (!engine || !out_ctx) return;
  out_ctx->instance = cj_engine_instance(engine);
  out_ctx->physicalDevice = cj_engine_physical_device(engine);
  out_ctx->device = cj_engine_device(engine);
  out_ctx->graphicsQueue = cj_engine_graphics_queue(engine);
  out_ctx->presentQueue = cj_engine_present_queue(engine);
  out_ctx->renderPass = cj_engine_render_pass(engine);
  out_ctx->commandPool = cj_engine_command_pool(engine);
}

CJ_API uint32_t cj_engine_device_index(const cj_engine_t* engine) {
  return engine ? engine->selected_device_index : 0u;
}

CJ_API void cj_engine_get_bindless_info(const cj_engine_t* engine, cj_bindless_info_t* out_info) {
  (void)engine;
  if (!out_info) return;
  out_info->images_capacity = 0u;
  out_info->buffers_capacity = 0u;
  out_info->samplers_capacity = 0u;
}

CJ_API void cj_engine_set_current(cj_engine_t* engine) { g_current_engine = engine; }
CJ_API cj_engine_t* cj_engine_get_current(void) { return g_current_engine; }

CJ_API VkInstance cj_engine_instance(const cj_engine_t* e) { return e ? e->instance : VK_NULL_HANDLE; }
CJ_API VkPhysicalDevice cj_engine_physical_device(const cj_engine_t* e) { return e ? e->physical_device : VK_NULL_HANDLE; }
CJ_API VkDevice cj_engine_device(const cj_engine_t* e) { return e ? e->device : VK_NULL_HANDLE; }
CJ_API VkQueue cj_engine_graphics_queue(const cj_engine_t* e) { return e ? e->graphics_queue : VK_NULL_HANDLE; }
CJ_API VkQueue cj_engine_present_queue(const cj_engine_t* e) { return e ? e->present_queue : VK_NULL_HANDLE; }
CJ_API VkRenderPass cj_engine_render_pass(const cj_engine_t* e) { return e ? e->render_pass : VK_NULL_HANDLE; }
CJ_API VkCommandPool cj_engine_command_pool(const cj_engine_t* e) { return e ? e->command_pool : VK_NULL_HANDLE; }
CJ_API VkDescriptorSetLayout cj_engine_bindless_layout(const cj_engine_t* e) { return e ? e->bindless_layout : VK_NULL_HANDLE; }
CJ_API VkDescriptorPool      cj_engine_bindless_pool(const cj_engine_t* e) { return e ? e->bindless_pool : VK_NULL_HANDLE; }
CJ_API CJellyBindlessResources* cj_engine_color_pipeline(const cj_engine_t* e) { return e ? (CJellyBindlessResources*)&e->color_pipeline : NULL; }

/* Internal access to textured resources */
CJ_API CJellyTexturedResources* cj_engine_textured(const cj_engine_t* e) { return (CJellyTexturedResources*)(e ? &e->textured : NULL); }
CJ_API CJellyBindlessState* cj_engine_bindless(const cj_engine_t* e) { return (CJellyBindlessState*)(e ? &e->bindless : NULL); }
CJ_API CJellyBasicState* cj_engine_basic(const cj_engine_t* e) { return (CJellyBasicState*)(e ? &e->basic : NULL); }

CJ_API void cj_engine_import_context(cj_engine_t* engine, const CJellyVulkanContext* ctx) {
  if (!engine || !ctx) return;
  engine->instance = ctx->instance;
  engine->physical_device = ctx->physicalDevice;
  engine->device = ctx->device;
  engine->graphics_queue = ctx->graphicsQueue;
  engine->present_queue = ctx->presentQueue;
  engine->render_pass = ctx->renderPass;
  engine->command_pool = ctx->commandPool;

  /* Initialize resource tables */
  memset(engine->textures, 0, sizeof(engine->textures));
  memset(engine->buffers, 0, sizeof(engine->buffers));
  memset(engine->samplers, 0, sizeof(engine->samplers));

  /* Initialize internal textured container */
  memset(&engine->textured, 0, sizeof(engine->textured));
  /* Initialize internal bindless container */
  memset(&engine->bindless, 0, sizeof(engine->bindless));
  /* Initialize internal basic container */
  memset(&engine->basic, 0, sizeof(engine->basic));
}

static inline cj_res_entry_t* table_for(cj_engine_t* e, cj_res_kind_t kind, size_t* out_cap) {
  switch (kind) {
    case CJ_RES_TEX: *out_cap = CJ_ENGINE_MAX_TEXTURES; return e->textures;
    case CJ_RES_BUF: *out_cap = CJ_ENGINE_MAX_BUFFERS;  return e->buffers;
    case CJ_RES_SMP: *out_cap = CJ_ENGINE_MAX_SAMPLERS; return e->samplers;
  }
  *out_cap = 0; return NULL;
}

static inline uint64_t make_handle(uint32_t index, uint32_t gen) {
  return ((uint64_t)index << 32) | (uint64_t)gen;
}

static inline void split_handle(uint64_t h, uint32_t* out_index, uint32_t* out_gen) {
  *out_index = (uint32_t)(h >> 32);
  *out_gen = (uint32_t)(h & 0xffffffffu);
}

CJ_API uint64_t cj_engine_res_alloc(cj_engine_t* e, cj_res_kind_t kind, uint32_t* out_slot) {
  if (!e) return 0;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  if (!table || cap == 0) return 0;
  for (uint32_t i = 1; i < cap; ++i) { /* start at 1 so 0 stays null */
    if (!table[i].in_use) {
      table[i].in_use = 1;
      table[i].refcount = 1;
      uint32_t newgen = table[i].generation + 1u;
      if (newgen == 0u) newgen = 1u;
      table[i].generation = newgen;
      table[i].slot = i; /* trivial mapping in stub */
      if (out_slot) *out_slot = table[i].slot;
      return make_handle(i, table[i].generation);
    }
  }
  return 0;
}

CJ_API void cj_engine_res_retain(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return;
  if (ent->refcount < 0xfffffff0u) ++ent->refcount;
}

CJ_API void cj_engine_res_release(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return;
  if (ent->refcount > 0) --ent->refcount;
  if (ent->refcount == 0) {
    ent->in_use = 0;
    ent->slot = 0;
  }
}

CJ_API uint32_t cj_engine_res_slot(cj_engine_t* e, cj_res_kind_t kind, uint64_t handle) {
  if (!e || handle == 0) return 0;
  size_t cap = 0; cj_res_entry_t* table = table_for(e, kind, &cap);
  uint32_t idx, gen; split_handle(handle, &idx, &gen);
  if (idx >= cap) return 0;
  cj_res_entry_t* ent = &table[idx];
  if (!ent->in_use || ent->generation != gen) return 0;
  return ent->slot;
}

/* Handle API thin wrappers */
CJ_API cj_handle_t cj_handle_alloc(cj_engine_t* e, cj_handle_kind_t kind, uint32_t* out_slot) {
  uint64_t raw = cj_engine_res_alloc(e, (cj_res_kind_t)kind, out_slot);
  cj_handle_t h;
  h.idx = (uint32_t)(raw >> 32);
  h.gen = (uint32_t)(raw & 0xffffffffu);
  return h;
}
CJ_API void cj_handle_retain(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h) {
  uint64_t raw = ((uint64_t)h.idx << 32) | (uint64_t)h.gen;
  cj_engine_res_retain(e, (cj_res_kind_t)kind, raw);
}
CJ_API void cj_handle_release(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h) {
  uint64_t raw = ((uint64_t)h.idx << 32) | (uint64_t)h.gen;
  cj_engine_res_release(e, (cj_res_kind_t)kind, raw);
}
CJ_API uint32_t cj_handle_slot(cj_engine_t* e, cj_handle_kind_t kind, cj_handle_t h) {
  uint64_t raw = ((uint64_t)h.idx << 32) | (uint64_t)h.gen;
  return cj_engine_res_slot(e, (cj_res_kind_t)kind, raw);
}

/* Public resource API already implemented in resources.c */

/* Vulkan resource creation helpers */
CJ_API int cj_engine_create_texture(cj_engine_t* e, uint32_t slot, const cj_texture_desc_t* desc) {
  if (!e || !desc || slot >= CJ_ENGINE_MAX_TEXTURES) return 0;
  cj_res_entry_t* entry = &e->textures[slot];
  if (!entry->in_use) return 0;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return 0;

  // Convert CJelly format to Vulkan format
  VkFormat vk_format = VK_FORMAT_R8G8B8A8_UNORM; // Default
  switch (desc->format) {
    case CJ_FORMAT_RGBA8_UNORM: vk_format = VK_FORMAT_R8G8B8A8_UNORM; break;
    case CJ_FORMAT_BGRA8_UNORM: vk_format = VK_FORMAT_B8G8R8A8_UNORM; break;
    case CJ_FORMAT_RGBA32_FLOAT: vk_format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
    default: vk_format = VK_FORMAT_R8G8B8A8_UNORM; break;
  }

  // Convert usage flags
  VkImageUsageFlags usage = 0;
  if (desc->usage & CJ_IMAGE_SAMPLED) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
  if (desc->usage & CJ_IMAGE_STORAGE) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
  if (desc->usage & CJ_IMAGE_COLOR_RT) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (desc->usage & CJ_IMAGE_DEPTH_RT) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  // Create image
  VkImageCreateInfo imageInfo = {0};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = desc->width;
  imageInfo.extent.height = desc->height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = desc->mips ? desc->mips : 1;
  imageInfo.arrayLayers = desc->layers ? desc->layers : 1;
  imageInfo.format = vk_format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(dev, &imageInfo, NULL, &entry->vulkan.texture.image) != VK_SUCCESS) {
    return 0;
  }

  // Allocate memory
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(dev, entry->vulkan.texture.image, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = eng_find_memory_type(e, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(dev, &allocInfo, NULL, &entry->vulkan.texture.memory) != VK_SUCCESS) {
    vkDestroyImage(dev, entry->vulkan.texture.image, NULL);
    return 0;
  }

  vkBindImageMemory(dev, entry->vulkan.texture.image, entry->vulkan.texture.memory, 0);

  // Create image view
  VkImageViewCreateInfo viewInfo = {0};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = entry->vulkan.texture.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = vk_format;
  viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = desc->mips ? desc->mips : 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = desc->layers ? desc->layers : 1;

  if (vkCreateImageView(dev, &viewInfo, NULL, &entry->vulkan.texture.imageView) != VK_SUCCESS) {
    vkFreeMemory(dev, entry->vulkan.texture.memory, NULL);
    vkDestroyImage(dev, entry->vulkan.texture.image, NULL);
    return 0;
  }

  // Create sampler
  VkSamplerCreateInfo samplerInfo = {0};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(dev, &samplerInfo, NULL, &entry->vulkan.texture.sampler) != VK_SUCCESS) {
    vkDestroyImageView(dev, entry->vulkan.texture.imageView, NULL);
    vkFreeMemory(dev, entry->vulkan.texture.memory, NULL);
    vkDestroyImage(dev, entry->vulkan.texture.image, NULL);
    return 0;
  }

  return 1;
}

CJ_API int cj_engine_create_buffer(cj_engine_t* e, uint32_t slot, const cj_buffer_desc_t* desc) {
  if (!e || !desc || slot >= CJ_ENGINE_MAX_BUFFERS) return 0;
  cj_res_entry_t* entry = &e->buffers[slot];
  if (!entry->in_use) return 0;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return 0;

  // Convert usage flags
  VkBufferUsageFlags usage = 0;
  if (desc->usage & CJ_BUFFER_VERTEX) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (desc->usage & CJ_BUFFER_INDEX) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (desc->usage & CJ_BUFFER_UNIFORM) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (desc->usage & CJ_BUFFER_STORAGE) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  if (desc->usage & CJ_BUFFER_TRANSFER_SRC) usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (desc->usage & CJ_BUFFER_TRANSFER_DST) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  // Create buffer
  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = desc->size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(dev, &bufferInfo, NULL, &entry->vulkan.buffer.buffer) != VK_SUCCESS) {
    return 0;
  }

  // Allocate memory
  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(dev, entry->vulkan.buffer.buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;

  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (desc->host_visible) {
    memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  allocInfo.memoryTypeIndex = eng_find_memory_type(e, memRequirements.memoryTypeBits, memProps);

  if (vkAllocateMemory(dev, &allocInfo, NULL, &entry->vulkan.buffer.memory) != VK_SUCCESS) {
    vkDestroyBuffer(dev, entry->vulkan.buffer.buffer, NULL);
    return 0;
  }

  vkBindBufferMemory(dev, entry->vulkan.buffer.buffer, entry->vulkan.buffer.memory, 0);

  return 1;
}

CJ_API int cj_engine_create_sampler(cj_engine_t* e, uint32_t slot, const cj_sampler_desc_t* desc) {
  if (!e || !desc || slot >= CJ_ENGINE_MAX_SAMPLERS) return 0;
  cj_res_entry_t* entry = &e->samplers[slot];
  if (!entry->in_use) return 0;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return 0;

  // Convert filter modes
  VkFilter min_filter = (desc->min_filter == CJ_FILTER_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  VkFilter mag_filter = (desc->mag_filter == CJ_FILTER_LINEAR) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

  // Convert address modes
  VkSamplerAddressMode address_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  switch (desc->address_u) {
    case CJ_ADDRESS_CLAMP: address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
    case CJ_ADDRESS_REPEAT: address_u = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case CJ_ADDRESS_MIRROR: address_u = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    case CJ_ADDRESS_BORDER: address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; break;
  }

  VkSamplerAddressMode address_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  switch (desc->address_v) {
    case CJ_ADDRESS_CLAMP: address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
    case CJ_ADDRESS_REPEAT: address_v = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case CJ_ADDRESS_MIRROR: address_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    case CJ_ADDRESS_BORDER: address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; break;
  }

  VkSamplerAddressMode address_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  switch (desc->address_w) {
    case CJ_ADDRESS_CLAMP: address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
    case CJ_ADDRESS_REPEAT: address_w = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case CJ_ADDRESS_MIRROR: address_w = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    case CJ_ADDRESS_BORDER: address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; break;
  }

  // Create sampler
  VkSamplerCreateInfo samplerInfo = {0};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = mag_filter;
  samplerInfo.minFilter = min_filter;
  samplerInfo.addressModeU = address_u;
  samplerInfo.addressModeV = address_v;
  samplerInfo.addressModeW = address_w;
  samplerInfo.anisotropyEnable = (desc->max_anisotropy > 0.0f) ? VK_TRUE : VK_FALSE;
  samplerInfo.maxAnisotropy = desc->max_anisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = desc->mip_lod_bias;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;

  if (vkCreateSampler(dev, &samplerInfo, NULL, &entry->vulkan.sampler.sampler) != VK_SUCCESS) {
    return 0;
  }

  return 1;
}

CJ_API void cj_engine_destroy_texture(cj_engine_t* e, uint32_t slot) {
  if (!e || slot >= CJ_ENGINE_MAX_TEXTURES) return;
  cj_res_entry_t* entry = &e->textures[slot];
  if (!entry->in_use) return;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return;

  if (entry->vulkan.texture.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(dev, entry->vulkan.texture.sampler, NULL);
    entry->vulkan.texture.sampler = VK_NULL_HANDLE;
  }
  if (entry->vulkan.texture.imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(dev, entry->vulkan.texture.imageView, NULL);
    entry->vulkan.texture.imageView = VK_NULL_HANDLE;
  }
  if (entry->vulkan.texture.image != VK_NULL_HANDLE) {
    vkDestroyImage(dev, entry->vulkan.texture.image, NULL);
    entry->vulkan.texture.image = VK_NULL_HANDLE;
  }
  if (entry->vulkan.texture.memory != VK_NULL_HANDLE) {
    vkFreeMemory(dev, entry->vulkan.texture.memory, NULL);
    entry->vulkan.texture.memory = VK_NULL_HANDLE;
  }
}

CJ_API void cj_engine_destroy_buffer(cj_engine_t* e, uint32_t slot) {
  if (!e || slot >= CJ_ENGINE_MAX_BUFFERS) return;
  cj_res_entry_t* entry = &e->buffers[slot];
  if (!entry->in_use) return;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return;

  if (entry->vulkan.buffer.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(dev, entry->vulkan.buffer.buffer, NULL);
    entry->vulkan.buffer.buffer = VK_NULL_HANDLE;
  }
  if (entry->vulkan.buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(dev, entry->vulkan.buffer.memory, NULL);
    entry->vulkan.buffer.memory = VK_NULL_HANDLE;
  }
}

CJ_API void cj_engine_destroy_sampler(cj_engine_t* e, uint32_t slot) {
  if (!e || slot >= CJ_ENGINE_MAX_SAMPLERS) return;
  cj_res_entry_t* entry = &e->samplers[slot];
  if (!entry->in_use) return;

  VkDevice dev = e->device;
  if (dev == VK_NULL_HANDLE) return;

  if (entry->vulkan.sampler.sampler != VK_NULL_HANDLE) {
    vkDestroySampler(dev, entry->vulkan.sampler.sampler, NULL);
    entry->vulkan.sampler.sampler = VK_NULL_HANDLE;
  }
}
