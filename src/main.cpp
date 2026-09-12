// Core application/runtime (implementation step 1): instance -> device ->
// VMA-allocated image -> render pass. Exercises: vulkan loader, VMA 3.x, glm.
// Runs on lavapipe (software) when no GPU. This is the foundation the rest of
// the engine builds on; it will grow into the main loop, world systems, etc.
#include <vulkan/vulkan.h>
// VMA 3.x is single-header: pull the implementation into this one TU.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x)                                               \
  do {                                                         \
    VkResult _r = (x);                                         \
    if (_r != VK_SUCCESS) {                                    \
      fprintf(stderr, "Vulkan error %d at %s:%d (%s)\n",       \
              (int)_r, __FILE__, __LINE__, #x);                \
      exit(1);                                                 \
    }                                                          \
  } while (0)

static void* vkAlloc(void* /*pUserData*/, size_t size, size_t alignment,
                     VkSystemAllocationScope /*allocationScope*/) {
  size_t a = alignment > 8 ? alignment : 8;
  return aligned_alloc(a, (size + a - 1) / a * a);
}
static void* vkRealloc(void* /*pUserData*/, void* /*pOriginal*/, size_t size, size_t alignment,
                       VkSystemAllocationScope /*allocationScope*/) {
  // Demo: fresh allocation (VMA does not use realloc in practice).
  return vkAlloc(nullptr, size, alignment, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
}
static void vkFree(void* /*pUserData*/, void* p) { free(p); }

int main() {
  // --- Instance (with validation layer if available) ---
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> layers(layerCount);
  if (layerCount) vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
  bool hasValidation = false;
  for (auto& l : layers)
    if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) hasValidation = true;

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "vulkan_plane";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "vulkan_plane";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  static const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &appInfo;
  if (hasValidation) {
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = &kValidationLayer;
  }
  VkInstance instance;
  CHECK(vkCreateInstance(&ici, nullptr, &instance));
  fprintf(stderr, "instance created (validation layer: %s)\n", hasValidation ? "on" : "off");

  // --- Physical device with a graphics queue ---
  uint32_t devCount = 0;
  vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
  std::vector<VkPhysicalDevice> devices(devCount);
  vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

  VkPhysicalDevice phys = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  for (auto d : devices) {
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qf.data());
    for (uint32_t i = 0; i < n && !phys; i++) {
      if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        phys = d;
        qfam = i;
      }
    }
  }
  if (!phys) {
    fprintf(stderr, "no device with a graphics queue\n");
    return 1;
  }
  char name[256] = {0};
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(phys, &props);
  fprintf(stderr, "physical device: %s (queue family %u)\n", props.deviceName, qfam);

  // --- Logical device ---
  float priority = 1.0f;
  VkDeviceQueueCreateInfo qc{};
  qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qc.queueFamilyIndex = qfam;
  qc.queueCount = 1;
  qc.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qc;
  VkDevice device;
  CHECK(vkCreateDevice(phys, &dci, nullptr, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, qfam, 0, &queue);

  // --- Command pool + command buffer ---
  VkCommandPoolCreateInfo cpci{};
  cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpci.queueFamilyIndex = qfam;
  VkCommandPool cmdPool;
  CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));
  VkCommandBufferAllocateInfo cba{};
  cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cba.commandPool = cmdPool;
  cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cba.commandBufferCount = 1;
  VkCommandBuffer cmd;
  CHECK(vkAllocateCommandBuffers(device, &cba, &cmd));

  // --- VMA allocator ---
  VkAllocationCallbacks cb{};
  cb.pfnAllocation = vkAlloc;
  cb.pfnReallocation = vkRealloc;
  cb.pfnFree = vkFree;
  VmaAllocatorCreateInfo vci{};
  vci.vulkanApiVersion = VK_API_VERSION_1_1;
  vci.instance = instance;
  vci.physicalDevice = phys;
  vci.device = device;
  vci.pAllocationCallbacks = &cb;
  VmaAllocator vma;
  if (vmaCreateAllocator(&vci, &vma) != VK_SUCCESS) {
    fprintf(stderr, "vmaCreateAllocator failed\n");
    return 1;
  }
  fprintf(stderr, "VMA allocator created\n");

  // --- Image via VMA ---
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {256, 256, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.queueFamilyIndexCount = 1;
  imageInfo.pQueueFamilyIndices = &qfam;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkImage image;
  VmaAllocation allocation;
  VmaAllocationInfo allocResult;
  CHECK(vmaCreateImage(vma, &imageInfo, &allocInfo, &image, &allocation, &allocResult));
  fprintf(stderr, "image 256x256 allocated (memory type %u)\n", allocResult.memoryType);

  // Framebuffer attachments are VkImageView in the 1.4-style headers.
  VkImageViewCreateInfo ivci{};
  ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ivci.image = image;
  ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
  ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageView imageView;
  CHECK(vkCreateImageView(device, &ivci, nullptr, &imageView));

  // --- Layout transition: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL ---
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  // --- Render pass (clear only, final layout SHADER_READ_ONLY) ---
  VkAttachmentDescription att{};
  att.format = VK_FORMAT_R8G8B8A8_UNORM;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &ref;
  VkRenderPassCreateInfo rpci{};
  rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = 1;
  rpci.pAttachments = &att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  VkRenderPass rp;
  CHECK(vkCreateRenderPass(device, &rpci, nullptr, &rp));

  VkFramebufferCreateInfo fbi{};
  fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbi.renderPass = rp;
  fbi.width = 256;
  fbi.height = 256;
  fbi.layers = 1;
  fbi.attachmentCount = 1;
  fbi.pAttachments = &imageView;
  VkFramebuffer fb;
  CHECK(vkCreateFramebuffer(device, &fbi, nullptr, &fb));

  // --- Record + submit: clear the image with a glm color ---
  glm::vec4 clearColor(0.15f, 0.40f, 0.75f, 1.0f);
  VkClearValue cv;
  memcpy(cv.color.float32, &clearColor, sizeof(float) * 4);
  VkRenderPassBeginInfo rpbi{};
  rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpbi.renderPass = rp;
  rpbi.framebuffer = fb;
  rpbi.renderArea = {{0, 0}, {256, 256}};
  rpbi.clearValueCount = 1;
  rpbi.pClearValues = &cv;
  VkCommandBufferBeginInfo cbbi{};
  cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  CHECK(vkBeginCommandBuffer(cmd, &cbbi));
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(cmd);
  CHECK(vkEndCommandBuffer(cmd));

  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
  CHECK(vkDeviceWaitIdle(device));
  fprintf(stderr, "render pass completed — image cleared to glm::vec4(%.2f, %.2f, %.2f, %.2f)\n",
          clearColor.r, clearColor.g, clearColor.b, clearColor.a);

  // --- Cleanup ---
  vkDestroyImageView(device, imageView, nullptr);
  vkDestroyFramebuffer(device, fb, nullptr);
  vkDestroyRenderPass(device, rp, nullptr);
  vmaDestroyImage(vma, image, allocation);
  vmaDestroyAllocator(vma);
  vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
  vkDestroyCommandPool(device, cmdPool, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);
  printf("OK: headless Vulkan render completed on %s\n", props.deviceName);
  return 0;
}
