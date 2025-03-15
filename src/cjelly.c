// Define the proper platform macro for Vulkan surface creation.
#ifdef _WIN32
  #define VK_USE_PLATFORM_WIN32_KHR
#else
  #define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include <cjelly/macros.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <X11/Xlib.h>
  #include <X11/Xatom.h>
#endif

// Enable validation layers if not in NDEBUG
#ifndef NDEBUG
  const int enableValidationLayers = 1;
#else
  const int enableValidationLayers = 0;
#endif

// Global debug messenger handle.
VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

// Global window variables
#ifdef _WIN32
  HWND window;
  HINSTANCE hInstance;
#else
  Display* display;
  Window window;
#endif


// Vertex structure for the square.
typedef struct Vertex {
  float pos[2];    // Position at location 0, a vec2.
  float color[3];  // Color at location 1, a vec3.
} Vertex;


// Vertices for a square, with positions and colors.
Vertex vertices[] = {
  { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
  { {  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
  { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
  { { -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } },
};

VkBuffer vertexBuffer;
VkDeviceMemory vertexBufferMemory;


// Flag to indicate that the window should close.
int shouldClose = 0;

const int WIDTH = 800;
const int HEIGHT = 600;

// Vulkan globals
VkInstance instance;
VkSurfaceKHR surface;
VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
VkDevice device;
VkQueue graphicsQueue;
VkQueue presentQueue;

VkSwapchainKHR swapChain;
uint32_t swapChainImageCount;
VkImage* swapChainImages;
VkFormat swapChainImageFormat;
VkExtent2D swapChainExtent;
VkImageView* swapChainImageViews;

VkRenderPass renderPass;
VkPipelineLayout pipelineLayout;
VkPipeline graphicsPipeline;
VkFramebuffer* swapChainFramebuffers;

VkCommandPool commandPool;
VkCommandBuffer* commandBuffers;

VkSemaphore imageAvailableSemaphore;
VkSemaphore renderFinishedSemaphore;
VkFence inFlightFence;

// Forward declarations for helper functions
void initWindow();
void initVulkan();
void mainLoop();
void cleanup();

void createInstance();
void createDebugMessenger();
void destroyDebugMessenger();
void createSurface();
void pickPhysicalDevice();
void createLogicalDevice();
void createSwapChain();
void createImageViews();
void createRenderPass();
void createGraphicsPipeline();
void createFramebuffers();
void createCommandPool();
void createCommandBuffers();
void createSyncObjects();
void drawFrame();
void processWindowEvents();

size_t readFile(const char *, char * *);
VkShaderModule createShaderModuleFromFile(VkDevice, const char *);


// Finds a suitable memory type based on typeFilter and desired properties.
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && 
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  fprintf(stderr, "Failed to find suitable memory type!\n");
  exit(EXIT_FAILURE);
}


// Creates a vertex buffer and uploads the vertex data for a square (two triangles).
void createVertexBuffer() {
  // Define 6 vertices to draw two triangles forming a square.
  Vertex vertices[] = {
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } },
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
  };
  VkDeviceSize bufferSize = sizeof(vertices);

  VkBufferCreateInfo bufferInfo = {0};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bufferSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create vertex buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &allocInfo, NULL, &vertexBufferMemory) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate vertex buffer memory\n");
    exit(EXIT_FAILURE);
  }

  vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

  void* data;
  vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
  memcpy(data, vertices, (size_t)bufferSize);
  vkUnmapMemory(device, vertexBufferMemory);
}


// Reads an entire file into memory.
size_t readFile(const char * filename, char * * buffer) {
  FILE* file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "Failed to open file: %s\n", filename);
    return 0;
  }
  fseek(file, 0, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);
  *buffer = malloc(fileSize);
  if (!*buffer || fread(*buffer, 1, fileSize, file) != fileSize) {
    fprintf(stderr, "Failed to read file: %s\n", filename);
    fclose(file);
    return 0;
  }
  fclose(file);
  return fileSize;
}


VkShaderModule createShaderModuleFromFile(VkDevice device, const char* filename) {
  char* code;
  size_t codeSize = readFile(filename, &code);
  if (codeSize == 0)
    return VK_NULL_HANDLE;

  VkShaderModuleCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = codeSize;
  createInfo.pCode = (const uint32_t*)code;

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create shader module from file: %s\n", filename);
    free(code);
    return VK_NULL_HANDLE;
  }
  free(code);
  return shaderModule;
}


// Debug callback function for validation layers.
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
  GCJ_MAYBE_UNUSED(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity),
  GCJ_MAYBE_UNUSED(VkDebugUtilsMessageTypeFlagsEXT messageTypes),
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  GCJ_MAYBE_UNUSED(void* pUserData)) {

  fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
  return VK_FALSE;
}

// Helper functions to load extension functions.
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
const VkAllocationCallbacks* pAllocator,
VkDebugUtilsMessengerEXT* pDebugMessenger) {

PFN_vkCreateDebugUtilsMessengerEXT func = 
  (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != NULL) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}


void DestroyDebugUtilsMessengerEXT(VkInstance instance,
VkDebugUtilsMessengerEXT debugMessenger,
const VkAllocationCallbacks* pAllocator) {

PFN_vkDestroyDebugUtilsMessengerEXT func = 
  (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != NULL) {
    func(instance, debugMessenger, pAllocator);
  }
}


//
// Main run function that sets up everything and enters the render loop.
//
void cjellyRun() {
  initWindow();
  initVulkan();
  mainLoop();
  cleanup();
}


//
// === PLATFORM-SPECIFIC WINDOW CREATION & EVENT HANDLING ===
//

#ifdef _WIN32
// Windows-specific window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch(uMsg) {
    case WM_CLOSE:
      shouldClose = 1;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}


void initWindow() {
  hInstance = GetModuleHandle(NULL);
  WNDCLASS wc = {0};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "VulkanWindowClass";
  RegisterClass(&wc);
  window = CreateWindowEx(
    0,
    "VulkanWindowClass",
    "Vulkan Square",
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT,
    WIDTH, HEIGHT,
    NULL, NULL,
    hInstance,
    NULL
  );
  ShowWindow(window, SW_SHOW);
}


void processWindowEvents() {
  MSG msg;
  while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

#else  // Linux (using Xlib)

void initWindow() {
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "Failed to open X display\n");
    exit(EXIT_FAILURE);
  }
  int screen = DefaultScreen(display);
  window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, WIDTH, HEIGHT, 1,
                                BlackPixel(display, screen), WhitePixel(display, screen));
  // Enable window close events.
  Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wmDelete, 1);
  XMapWindow(display, window);
}

void processWindowEvents() {
  while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    if (event.type == ClientMessage)
      shouldClose = 1;
  }
}
#endif


//
// === VULKAN INITIALIZATION & RENDERING FUNCTIONS ===
//

void createInstance() {
  VkApplicationInfo appInfo = {0};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Vulkan Square";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "CjellyEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  // Specify required extensions for the platform.
  const char* extensions[10];
  uint32_t extCount = 0;
  extensions[extCount++] = "VK_KHR_surface";
#ifdef _WIN32
  extensions[extCount++] = "VK_KHR_win32_surface";
#else
  extensions[extCount++] = "VK_KHR_xlib_surface";
#endif
  if (enableValidationLayers) {
    extensions[extCount++] = "VK_EXT_debug_utils";
  }

  VkInstanceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = extCount;
  createInfo.ppEnabledExtensionNames = extensions;

  if (enableValidationLayers) {
    const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;

    // Set up debug messenger info so that it is used during instance creation.
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = NULL;
  }

  if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create Vulkan instance\n");
    exit(EXIT_FAILURE);
  }

  if (enableValidationLayers) {
    createDebugMessenger();
  }
}


void createDebugMessenger() {
  VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;

  if (CreateDebugUtilsMessengerEXT(instance, &createInfo, NULL, &debugMessenger) != VK_SUCCESS) {
    fprintf(stderr, "Failed to set up debug messenger!\n");
  }
}

void destroyDebugMessenger() {
  if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
    DestroyDebugUtilsMessengerEXT(instance, debugMessenger, NULL);
  }
}


void createSurface() {
#ifdef _WIN32
  VkWin32SurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.hinstance = hInstance;
  createInfo.hwnd = window;
  if (vkCreateWin32SurfaceKHR(instance, &createInfo, NULL, &surface) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create Win32 surface\n");
    exit(EXIT_FAILURE);
  }
#else
  VkXlibSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  createInfo.dpy = display;
  createInfo.window = window;
  if (vkCreateXlibSurfaceKHR(instance, &createInfo, NULL, &surface) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create Xlib surface\n");
    exit(EXIT_FAILURE);
  }
#endif
}


void pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
  if (deviceCount == 0) {
    fprintf(stderr, "Failed to find GPUs with Vulkan support\n");
    exit(EXIT_FAILURE);
  }
  VkPhysicalDevice devices[deviceCount];
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
  physicalDevice = devices[0];
}


void createLogicalDevice() {
  uint32_t queueFamilyIndex = 0; // Simplified: assume family 0 supports both graphics and present.
  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {0};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  // Specify the swapchain extension.
  const char* deviceExtensions[] = { "VK_KHR_swapchain" };

  VkDeviceCreateInfo createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.enabledExtensionCount = 1;
  createInfo.ppEnabledExtensionNames = deviceExtensions;

  if (vkCreateDevice(physicalDevice, &createInfo, NULL, &device) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create logical device\n");
    exit(EXIT_FAILURE);
  }

  vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);
  presentQueue = graphicsQueue;
}


void createSwapChain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
  fprintf(stderr, "Surface Capabilities:\n  minImageCount: %u\n  maxImageCount: %u\n  currentExtent: %ux%u\n",
          capabilities.minImageCount, capabilities.maxImageCount,
          capabilities.currentExtent.width, capabilities.currentExtent.height);

  // Set the global swapChainExtent to what the surface supports.
  swapChainExtent = capabilities.currentExtent;

  VkSwapchainCreateInfoKHR createInfo = {0};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;
  createInfo.minImageCount = 2;
  createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  createInfo.imageExtent = capabilities.currentExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  createInfo.clipped = VK_TRUE;
  createInfo.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device, &createInfo, NULL, &swapChain) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create swap chain\n");
    exit(EXIT_FAILURE);
  }
}


void createImageViews() {
  vkGetSwapchainImagesKHR(device, swapChain, &swapChainImageCount, NULL);
  swapChainImages = malloc(sizeof(VkImage) * swapChainImageCount);
  vkGetSwapchainImagesKHR(device, swapChain, &swapChainImageCount, swapChainImages);

  swapChainImageViews = malloc(sizeof(VkImageView) * swapChainImageCount);
  for (uint32_t i = 0; i < swapChainImageCount; i++) {
    VkImageViewCreateInfo viewInfo = {0};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = swapChainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, NULL, &swapChainImageViews[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create image view\n");
      exit(EXIT_FAILURE);
    }
  }
}


void createRenderPass() {
  VkAttachmentDescription colorAttachment = {0};
  colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {0};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {0};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkRenderPassCreateInfo renderPassInfo = {0};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;

  if (vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create render pass\n");
    exit(EXIT_FAILURE);
  }
}


void createGraphicsPipeline() {
  // Load SPIR-V binaries and create shader modules.
  VkShaderModule vertShaderModule = createShaderModuleFromFile(device, "shaders/basic.vert.spv");
  VkShaderModule fragShaderModule = createShaderModuleFromFile(device, "shaders/basic.frag.spv");

  if (vertShaderModule == VK_NULL_HANDLE || fragShaderModule == VK_NULL_HANDLE) {
      fprintf(stderr, "Failed to create shader modules\n");
      exit(EXIT_FAILURE);
  }

  VkPipelineShaderStageCreateInfo shaderStages[2] = {0};

  // Vertex shader stage.
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertShaderModule;
  shaderStages[0].pName = "main";

  // Fragment shader stage.
  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragShaderModule;
  shaderStages[1].pName = "main";

  // Define a binding description for our Vertex structure.
  VkVertexInputBindingDescription bindingDescription = {0};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  // Define attribute descriptions for the vertex shader inputs.
  VkVertexInputAttributeDescription attributeDescriptions[2] = {0};

  // Attribute 0: position (vec2)
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, pos);

  // Attribute 1: color (vec3)
  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = 2;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Instead of using hard-coded WIDTH/HEIGHT, use the swapchain extent.
  VkViewport viewport = {0};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float) swapChainExtent.width;
  viewport.height = (float) swapChainExtent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor = {0};
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  scissor.extent = swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState = {0};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer = {0};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling = {0};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending = {0};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {0};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, NULL, &pipelineLayout) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create pipeline layout\n");
      exit(EXIT_FAILURE);
  }

  VkGraphicsPipelineCreateInfo pipelineInfo = {0};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &graphicsPipeline) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create graphics pipeline\n");
      exit(EXIT_FAILURE);
  }

  // Clean up shader modules after pipeline creation.
  vkDestroyShaderModule(device, vertShaderModule, NULL);
  vkDestroyShaderModule(device, fragShaderModule, NULL);
}


void createFramebuffers() {
  swapChainFramebuffers = malloc(sizeof(VkFramebuffer) * swapChainImageCount);
  for (uint32_t i = 0; i < swapChainImageCount; i++) {
    VkImageView attachments[] = { swapChainImageViews[i] };
    VkFramebufferCreateInfo framebufferInfo = {0};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device, &framebufferInfo, NULL, &swapChainFramebuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create framebuffer\n");
      exit(EXIT_FAILURE);
    }
  }
}


void createCommandPool() {
  VkCommandPoolCreateInfo poolInfo = {0};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = 0;
  if (vkCreateCommandPool(device, &poolInfo, NULL, &commandPool) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create command pool\n");
    exit(EXIT_FAILURE);
  }
}


void createCommandBuffers() {
  commandBuffers = malloc(sizeof(VkCommandBuffer) * swapChainImageCount);
  VkCommandBufferAllocateInfo allocInfo = {0};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = swapChainImageCount;

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers) != VK_SUCCESS) {
    fprintf(stderr, "Failed to allocate command buffers\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < swapChainImageCount; i++) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
      fprintf(stderr, "Failed to begin recording command buffer\n");
      exit(EXIT_FAILURE);
    }

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[i];
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &vertexBuffer, offsets);

    vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdDraw(commandBuffers[i], 6, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffers[i]);

    if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
      fprintf(stderr, "Failed to record command buffer\n");
      exit(EXIT_FAILURE);
    }
  }
}



void createSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo = {0};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &imageAvailableSemaphore) != VK_SUCCESS ||
    vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphore) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create semaphores\n");
    exit(EXIT_FAILURE);
  }

  VkFenceCreateInfo fenceInfo = {0};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  if (vkCreateFence(device, &fenceInfo, NULL, &inFlightFence) != VK_SUCCESS) {
    fprintf(stderr, "Failed to create fence\n");
    exit(EXIT_FAILURE);
  }
}


void initVulkan() {
  createInstance();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createVertexBuffer();
  createSwapChain();
  createImageViews();
  createRenderPass();
  createGraphicsPipeline();
  createFramebuffers();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();
}


void drawFrame() {
  vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &inFlightFence);

  uint32_t imageIndex;
  vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

  VkSubmitInfo submitInfo = {0};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
  VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
    fprintf(stderr, "Failed to submit draw command buffer\n");
  }

  VkPresentInfoKHR presentInfo = {0};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapChain;
  presentInfo.pImageIndices = &imageIndex;

  vkQueuePresentKHR(presentQueue, &presentInfo);
}


void mainLoop() {
  while (!shouldClose) {
    processWindowEvents();
    drawFrame();
  }
  vkDeviceWaitIdle(device);
}


void cleanup() {
  vkDestroySemaphore(device, renderFinishedSemaphore, NULL);
  vkDestroySemaphore(device, imageAvailableSemaphore, NULL);
  vkDestroyFence(device, inFlightFence, NULL);

  vkDestroyCommandPool(device, commandPool, NULL);

  for (uint32_t i = 0; i < swapChainImageCount; i++) {
    vkDestroyFramebuffer(device, swapChainFramebuffers[i], NULL);
    vkDestroyImageView(device, swapChainImageViews[i], NULL);
  }
  free(swapChainFramebuffers);
  free(swapChainImageViews);
  free(swapChainImages);
  free(commandBuffers);

  // Destroy the vertex buffer and free its memory.
  vkDestroyBuffer(device, vertexBuffer, NULL);
  vkFreeMemory(device, vertexBufferMemory, NULL);

  vkDestroyPipeline(device, graphicsPipeline, NULL);
  vkDestroyPipelineLayout(device, pipelineLayout, NULL);
  vkDestroyRenderPass(device, renderPass, NULL);
  vkDestroySwapchainKHR(device, swapChain, NULL);
  vkDestroyDevice(device, NULL);
  vkDestroySurfaceKHR(instance, surface, NULL);
  if (enableValidationLayers) {
    destroyDebugMessenger();
  }
  vkDestroyInstance(instance, NULL);

#ifdef _WIN32
  DestroyWindow(window);
#else
  XDestroyWindow(display, window);
  XCloseDisplay(display);
#endif
}
