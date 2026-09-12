// Windowed Vulkan + GLFW + Dear ImGui demo (build with -DBUILD_WINDOWED=ON).
// Needs a display (X11). Uses the cloned glfw + imgui repos; ImGui's Vulkan
// and GLFW backends are compiled in.
#include <vulkan/vulkan.h> // must come before glfw3.h (GLFW_INCLUDE_NONE)
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/glm.hpp>

#include <cstdio>
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

static void onResize(GLFWwindow* w, int, int) {
  *(int*)glfwGetWindowUserPointer(w) = 1; // resized flag
}

int main() {
  if (!glfwInit()) {
    fprintf(stderr, "glfwInit failed\n");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  GLFWwindow* window = glfwCreateWindow(960, 640, "vulkan-dev: glfw + imgui", nullptr, nullptr);
  if (!window) {
    fprintf(stderr, "window creation failed (no display?)\n");
    glfwTerminate();
    return 1;
  }
  int resized = 0;
  glfwSetWindowUserPointer(window, &resized);
  glfwSetFramebufferSizeCallback(window, onResize);

  // --- Instance ---
  uint32_t glfwExtCount = 0;
  const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
  static const char* kValidation = "VK_LAYER_KHRONOS_validation";
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "vk_windowed";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = 1;
  ici.ppEnabledLayerNames = &kValidation;
  ici.enabledExtensionCount = glfwExtCount;
  ici.ppEnabledExtensionNames = glfwExts;
  VkInstance instance;
  CHECK(vkCreateInstance(&ici, nullptr, &instance));

  // --- Surface + device ---
  VkSurfaceKHR surface;
  CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
  uint32_t devCount = 0;
  vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
  std::vector<VkPhysicalDevice> devices(devCount);
  vkEnumeratePhysicalDevices(instance, &devCount, devices.data());

  VkPhysicalDevice phys = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  for (auto d : devices) {
    VkBool32 ok = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(d, 0, surface, &ok) != VK_SUCCESS || !ok) continue;
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qf.data());
    for (uint32_t i = 0; i < n && !phys; i++)
      if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        phys = d;
        qfam = i;
      }
  }
  if (!phys) {
    fprintf(stderr, "no surface-capable device\n");
    return 1;
  }
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(phys, &props);
  fprintf(stderr, "device: %s\n", props.deviceName);

  float priority = 1.0f;
  VkDeviceQueueCreateInfo qc{};
  qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qc.queueFamilyIndex = qfam;
  qc.queueCount = 1;
  qc.pQueuePriorities = &priority;
  static const char* kDevExt = "VK_KHR_surface";
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qc;
  dci.enabledExtensionCount = 1;
  dci.ppEnabledExtensionNames = &kDevExt;
  VkDevice device;
  CHECK(vkCreateDevice(phys, &dci, nullptr, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, qfam, 0, &queue);

  // --- Swapchain ---
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> swapImages;
  std::vector<VkImageView> swapViews;
  VkFormat swapFormat;
  VkExtent2D swapExtent;
  auto makeViews = [&]() {
    for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
    swapViews.resize(swapImages.size());
    for (size_t i = 0; i < swapImages.size(); i++) {
      VkImageViewCreateInfo ivci{};
      ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      ivci.image = swapImages[i];
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivci.format = swapFormat;
      ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      CHECK(vkCreateImageView(device, &ivci, nullptr, &swapViews[i]));
    }
  };
  auto createSwapchain = [&]() {
    if (swapchain) {
      vkDeviceWaitIdle(device);
      vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, fmts.data());
    swapFormat = fmts[0].format;
    uint32_t w = 0, h = 0;
    glfwGetFramebufferSize(window, (int*)&w, (int*)&h);
    swapExtent = {w ? w : 1, h ? h : 1};
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = imgCount;
    sci.imageFormat = swapFormat;
    sci.imageColorSpace = fmts[0].colorSpace;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    CHECK(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain));
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    swapImages.resize(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data());
    makeViews();
  };
  createSwapchain();

  // --- Render pass + framebuffers ---
  VkAttachmentDescription att{};
  att.format = swapFormat;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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
  VkRenderPass renderPass;
  CHECK(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));

  std::vector<VkFramebuffer> frames(swapImages.size());
  auto makeFramebuffers = [&]() {
    for (size_t i = 0; i < frames.size(); i++) {
      if (frames[i]) vkDestroyFramebuffer(device, frames[i], nullptr);
      VkFramebufferCreateInfo fbi{};
      fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbi.renderPass = renderPass;
      fbi.width = swapExtent.width;
      fbi.height = swapExtent.height;
      fbi.layers = 1;
      fbi.attachmentCount = 1;
      fbi.pAttachments = &swapViews[i];
      CHECK(vkCreateFramebuffer(device, &fbi, nullptr, &frames[i]));
    }
  };
  makeFramebuffers();

  // --- Command pool/buffers + fence ---
  VkCommandPoolCreateInfo cpci{};
  cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpci.queueFamilyIndex = qfam;
  VkCommandPool cmdPool;
  CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));
  std::vector<VkCommandBuffer> cmds(frames.size());
  {
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmdPool;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = (uint32_t)frames.size();
    CHECK(vkAllocateCommandBuffers(device, &cba, cmds.data()));
  }
  VkFence fence;
  VkSemaphore imageAcquired, renderComplete;
  {
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CHECK(vkCreateFence(device, &fci, nullptr, &fence));
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    CHECK(vkCreateSemaphore(device, &sci, nullptr, &imageAcquired));
    CHECK(vkCreateSemaphore(device, &sci, nullptr, &renderComplete));
  }

  // --- ImGui (Vulkan + GLFW backends) ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplVulkan_InitInfo vii{};
  vii.Instance = instance;
  vii.PhysicalDevice = phys;
  vii.Device = device;
  vii.QueueFamily = qfam;
  vii.Queue = queue;
  vii.MinImageCount = 2;
  vii.ImageCount = (uint32_t)swapImages.size();
  vii.PipelineInfoMain.RenderPass = renderPass;
  vii.PipelineInfoMain.Subpass = 0;
  vii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&vii);
  ImGui_ImplGlfw_InitForVulkan(window, VK_TRUE);

  // --- Frame loop ---
  bool showDemo = true;
  glm::vec3 color(0.4f, 0.6f, 0.9f);
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (resized) {
      resized = 0;
      createSwapchain();
      makeFramebuffers();
      // Re-init ImGui Vulkan state for the new swapchain images.
      ImGui_ImplVulkan_Init(&vii);
    }
    uint32_t imageIndex;
    CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquired, VK_NULL_HANDLE, &imageIndex));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("vulkan-dev");
    ImGui::Text("device: %s", props.deviceName);
    ImGui::Text("swapchain: %ux%u, %u images", swapExtent.width, swapExtent.height,
                (uint32_t)swapImages.size());
    ImGui::ColorEdit3("clear color", &color.x);
    ImGui::Checkbox("show demo window", &showDemo);
    ImGui::End();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
    ImGui::Render();

    VkCommandBuffer cmd = cmds[imageIndex];
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    CHECK(vkBeginCommandBuffer(cmd, &cbi));
    VkClearValue cv;
    cv.color.float32[0] = color.r;
    cv.color.float32[1] = color.g;
    cv.color.float32[2] = color.b;
    cv.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = renderPass;
    rpbi.framebuffer = frames[imageIndex];
    rpbi.renderArea = {{0, 0}, swapExtent};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &cv;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd->TotalVtxCount > 0) ImGui_ImplVulkan_RenderDrawData(dd, cmd);
    vkCmdEndRenderPass(cmd);
    CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAcquired;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderComplete;
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    CHECK(vkResetFences(device, 1, &fence));

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderComplete;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    CHECK(vkQueuePresentKHR(queue, &pi));
  }

  // --- Cleanup ---
  vkDeviceWaitIdle(device);
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  for (auto fb : frames) vkDestroyFramebuffer(device, fb, nullptr);
  for (auto v : swapViews) vkDestroyImageView(device, v, nullptr);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkDestroySemaphore(device, imageAcquired, nullptr);
  vkDestroySemaphore(device, renderComplete, nullptr);
  vkFreeCommandBuffers(device, cmdPool, (uint32_t)cmds.size(), cmds.data());
  vkDestroyCommandPool(device, cmdPool, nullptr);
  vkDestroyFence(device, fence, nullptr);
  vkDestroyRenderPass(device, renderPass, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
  printf("OK: windowed glfw+imgui demo shut down cleanly\n");
  return 0;
}
