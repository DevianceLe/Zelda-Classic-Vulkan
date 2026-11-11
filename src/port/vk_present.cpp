// vk_present.cpp - see vk_present.h.

#include "vk_present.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "volk.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "shaders/present_vert.h"
#include "shaders/present_frag.h"

namespace zcport
{

namespace
{
const int kFramesInFlight = 2;

const char *vk_result_name(VkResult r)
{
    switch(r)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult(other)";
    }
}
}

struct VulkanPresenter::Impl
{
    struct Frame
    {
        VkCommandBuffer cmd;
        VkFence fence;
        VkSemaphore image_available;

        VkBuffer staging;
        VkDeviceMemory staging_mem;
        void *staging_ptr;
        VkDeviceSize staging_size;

        VkImage tex;
        VkDeviceMemory tex_mem;
        VkImageView tex_view;
        int tex_w, tex_h;
        bool tex_initialized;

        VkDescriptorSet set;
        VkSampler set_sampler;   // sampler the descriptor set was last written with
        VkImageView set_view;    // view the descriptor set was last written with
    };

    SDL_Window *window;
    bool vsync;
    std::string device_name;

    VkInstance instance;
    VkSurfaceKHR surface;
    void *surface_handle;   // the native surface 'surface' was made from
    bool surface_lost;      // 'surface' is gone: make a new one before drawing
    VkPhysicalDevice gpu;
    VkPhysicalDeviceMemoryProperties mem_props;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;

    VkSwapchainKHR swapchain;
    VkFormat swap_format;
    VkExtent2D extent;
    std::vector<VkImage> swap_images;
    std::vector<VkImageView> swap_views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> render_done; // one per swapchain image
    bool swapchain_dirty;

    VkRenderPass render_pass;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkSampler sampler_nearest;
    VkSampler sampler_linear;
    VkDescriptorPool desc_pool;
    VkCommandPool cmd_pool;

    Frame frames[kFramesInFlight];
    uint32_t frame_index;

    bool ui_ready;   // ImGui_ImplVulkan initialised on this device

    Impl()
    {
        ui_ready = false;
        window = NULL;
        vsync = true;
        instance = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
        surface_handle = NULL;
        surface_lost = false;
        gpu = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        queue_family = 0;
        queue = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
        swap_format = VK_FORMAT_UNDEFINED;
        extent.width = extent.height = 0;
        swapchain_dirty = false;
        render_pass = VK_NULL_HANDLE;
        set_layout = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        sampler_nearest = sampler_linear = VK_NULL_HANDLE;
        desc_pool = VK_NULL_HANDLE;
        cmd_pool = VK_NULL_HANDLE;
        memset(frames, 0, sizeof(frames));
        frame_index = 0;
    }

    bool fail(std::string *error, const char *what, VkResult r)
    {
        if(error)
        {
            *error = what;
            *error += ": ";
            *error += vk_result_name(r);
        }
        return false;
    }

    bool fail(std::string *error, const char *what)
    {
        if(error)
        {
            *error = what;
        }
        return false;
    }

    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags)
    {
        for(uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
        {
            if((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        }
        return UINT32_MAX;
    }

    bool create_instance(std::string *error)
    {
        PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if(!gipa)
            return fail(error, "Vulkan loader not available (SDL_Vulkan_LoadLibrary not called or failed)");
        volkInitializeCustom(gipa);

        Uint32 ext_count = 0;
        const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
        if(!exts)
            return fail(error, SDL_GetError());

        VkApplicationInfo app;
        memset(&app, 0, sizeof(app));
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "zplayer";
        app.applicationVersion = VK_MAKE_VERSION(2, 53, 0);
        app.pEngineName = "zplayer";
        app.engineVersion = VK_MAKE_VERSION(2, 53, 0);
        app.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = ext_count;
        ci.ppEnabledExtensionNames = exts;
#if defined(__APPLE__)
        // macOS has Vulkan only through MoltenVK, a "portability" driver: through the
        // Vulkan loader it is listed only when the instance asks for those
        // (VK_KHR_portability_enumeration). Loaded directly it doesn't need this.
        std::vector<const char *> all_exts(exts, exts + ext_count);
        {
            uint32_t n = 0;
            vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
            std::vector<VkExtensionProperties> props(n);
            if(n)
                vkEnumerateInstanceExtensionProperties(NULL, &n, &props[0]);
            for(uint32_t i = 0; i < n; i++)
            {
                if(strcmp(props[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
                {
                    all_exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                }
                else if(strcmp(props[i].extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0)
                    all_exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            }
        }
        ci.enabledExtensionCount = (uint32_t)all_exts.size();
        ci.ppEnabledExtensionNames = &all_exts[0];
#endif

        // Optional validation layer for development: set ZC_VK_VALIDATION=1.
        const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
        const char *want_validation = SDL_getenv("ZC_VK_VALIDATION");
        if(want_validation && want_validation[0] == '1')
        {
            uint32_t n = 0;
            vkEnumerateInstanceLayerProperties(&n, NULL);
            std::vector<VkLayerProperties> props(n);
            vkEnumerateInstanceLayerProperties(&n, props.empty() ? NULL : &props[0]);
            for(uint32_t i = 0; i < n; i++)
            {
                if(strcmp(props[i].layerName, layers[0]) == 0)
                {
                    ci.enabledLayerCount = 1;
                    ci.ppEnabledLayerNames = layers;
                }
            }
        }

        VkResult r = vkCreateInstance(&ci, NULL, &instance);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateInstance", r);
        volkLoadInstance(instance);

        if(!SDL_Vulkan_CreateSurface(window, instance, NULL, &surface))
            return fail(error, SDL_GetError());
        return true;
    }

    bool pick_device(std::string *error)
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, NULL);
        if(n == 0)
            return fail(error, "No Vulkan-capable GPU found");
        std::vector<VkPhysicalDevice> gpus(n);
        vkEnumeratePhysicalDevices(instance, &n, &gpus[0]);

        int best_score = -1;
        for(uint32_t i = 0; i < n; i++)
        {
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, NULL);
            std::vector<VkQueueFamilyProperties> qprops(qn);
            if(qn)
                vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, &qprops[0]);

            for(uint32_t q = 0; q < qn; q++)
            {
                VkBool32 can_present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, surface, &can_present);
                if(!(qprops[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !can_present)
                    continue;

                if(!has_device_extension(gpus[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                    continue;

                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(gpus[i], &props);
                int score = 1;
                if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 3;
                else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;

                if(score > best_score)
                {
                    best_score = score;
                    gpu = gpus[i];
                    queue_family = q;
                    device_name = props.deviceName;
                }
                break;
            }
        }

        if(gpu == VK_NULL_HANDLE)
            return fail(error, "No GPU can present to this window");

        vkGetPhysicalDeviceMemoryProperties(gpu, &mem_props);
        return true;
    }

    static bool has_device_extension(VkPhysicalDevice dev, const char *name)
    {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(dev, NULL, &n, NULL);
        std::vector<VkExtensionProperties> props(n);
        if(n)
            vkEnumerateDeviceExtensionProperties(dev, NULL, &n, &props[0]);
        for(uint32_t i = 0; i < n; i++)
            if(strcmp(props[i].extensionName, name) == 0)
                return true;
        return false;
    }

    bool create_device(std::string *error)
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci;
        memset(&qci, 0, sizeof(qci));
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = queue_family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;

        const char *exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, NULL };
        uint32_t ext_n = 1;
#if defined(__APPLE__)
        // A device that has VK_KHR_portability_subset (MoltenVK) must have it enabled.
        if(has_device_extension(gpu, "VK_KHR_portability_subset"))
            exts[ext_n++] = "VK_KHR_portability_subset";
#endif
        VkDeviceCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos = &qci;
        ci.enabledExtensionCount = ext_n;
        ci.ppEnabledExtensionNames = exts;

        VkResult r = vkCreateDevice(gpu, &ci, NULL, &device);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateDevice", r);
        volkLoadDevice(device);
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        return true;
    }

    VkSurfaceFormatKHR choose_surface_format()
    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, NULL);
        std::vector<VkSurfaceFormatKHR> formats(n);
        if(n)
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, &formats[0]);

        // The game's colours are already gamma-encoded, so use a UNORM target
        // (an _SRGB target would brighten everything).
        const VkFormat preferred[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A8B8G8R8_UNORM_PACK32 };
        for(size_t p = 0; p < sizeof(preferred) / sizeof(preferred[0]); p++)
            for(uint32_t i = 0; i < n; i++)
                if(formats[i].format == preferred[p] && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                    return formats[i];

        if(n == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        {
            VkSurfaceFormatKHR f;
            f.format = VK_FORMAT_B8G8R8A8_UNORM;
            f.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            return f;
        }
        return formats[0];
    }

    VkPresentModeKHR choose_present_mode()
    {
        if(vsync)
            return VK_PRESENT_MODE_FIFO_KHR; // always available

        uint32_t n = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &n, NULL);
        std::vector<VkPresentModeKHR> modes(n);
        if(n)
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &n, &modes[0]);
        for(uint32_t i = 0; i < n; i++)
            if(modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                return modes[i];
        for(uint32_t i = 0; i < n; i++)
            if(modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                return modes[i];
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    bool create_render_pass(std::string *error)
    {
        VkAttachmentDescription color;
        memset(&color, 0, sizeof(color));
        color.format = swap_format;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref;
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub;
        memset(&sub, 0, sizeof(sub));
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        // Wait for the acquired image before writing to it.
        VkSubpassDependency dep;
        memset(&dep, 0, sizeof(dep));
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments = &color;
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies = &dep;

        VkResult r = vkCreateRenderPass(device, &ci, NULL, &render_pass);
        return r == VK_SUCCESS ? true : fail(error, "vkCreateRenderPass", r);
    }

    VkShaderModule make_shader(const uint32_t *code, size_t bytes)
    {
        VkShaderModuleCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = bytes;
        ci.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &ci, NULL, &m);
        return m;
    }

    // Objects that don't depend on the swapchain: created once.
    bool create_static_objects(std::string *error)
    {
        VkDescriptorSetLayoutBinding b;
        memset(&b, 0, sizeof(b));
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dl;
        memset(&dl, 0, sizeof(dl));
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.bindingCount = 1;
        dl.pBindings = &b;
        VkResult r = vkCreateDescriptorSetLayout(device, &dl, NULL, &set_layout);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateDescriptorSetLayout", r);

        VkPushConstantRange pcr;
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;

        VkPipelineLayoutCreateInfo pl;
        memset(&pl, 0, sizeof(pl));
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &set_layout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        r = vkCreatePipelineLayout(device, &pl, NULL, &pipeline_layout);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreatePipelineLayout", r);

        return create_samplers_and_pool(error);
    }

    // The graphics pipeline depends on the render pass (swapchain format).
    bool create_graphics_pipeline(std::string *error)
    {
        VkResult r;
        VkShaderModule vs = make_shader(present_vert_spv, sizeof(present_vert_spv));
        VkShaderModule fs = make_shader(present_frag_spv, sizeof(present_frag_spv));
        if(!vs || !fs)
            return fail(error, "vkCreateShaderModule failed");

        VkPipelineShaderStageCreateInfo stages[2];
        memset(stages, 0, sizeof(stages));
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia;
        memset(&ia, 0, sizeof(ia));
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

        VkPipelineViewportStateCreateInfo vp;
        memset(&vp, 0, sizeof(vp));
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs;
        memset(&rs, 0, sizeof(rs));
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms;
        memset(&ms, 0, sizeof(ms));
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba;
        memset(&cba, 0, sizeof(cba));
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb;
        memset(&cb, 0, sizeof(cb));
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dyn;
        memset(&dyn, 0, sizeof(dyn));
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dyn_states;

        VkGraphicsPipelineCreateInfo gp;
        memset(&gp, 0, sizeof(gp));
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = pipeline_layout;
        gp.renderPass = render_pass;
        gp.subpass = 0;

        r = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL, &pipeline);
        vkDestroyShaderModule(device, vs, NULL);
        vkDestroyShaderModule(device, fs, NULL);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateGraphicsPipelines", r);
        return true;
    }

    bool create_samplers_and_pool(std::string *error)
    {
        VkResult r;
        // Samplers: nearest for sharp pixels, linear for smooth scaling.
        VkSamplerCreateInfo sci;
        memset(&sci, 0, sizeof(sci));
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.0f;
        r = vkCreateSampler(device, &sci, NULL, &sampler_nearest);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateSampler", r);
        sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
        r = vkCreateSampler(device, &sci, NULL, &sampler_linear);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateSampler", r);

        VkDescriptorPoolSize ps;
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = kFramesInFlight;
        VkDescriptorPoolCreateInfo dp;
        memset(&dp, 0, sizeof(dp));
        dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dp.maxSets = kFramesInFlight;
        dp.poolSizeCount = 1;
        dp.pPoolSizes = &ps;
        r = vkCreateDescriptorPool(device, &dp, NULL, &desc_pool);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateDescriptorPool", r);
        return true;
    }

    bool create_frames(std::string *error)
    {
        VkCommandPoolCreateInfo cp;
        memset(&cp, 0, sizeof(cp));
        cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cp.queueFamilyIndex = queue_family;
        VkResult r = vkCreateCommandPool(device, &cp, NULL, &cmd_pool);
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateCommandPool", r);

        for(int i = 0; i < kFramesInFlight; i++)
        {
            Frame &f = frames[i];

            VkCommandBufferAllocateInfo ai;
            memset(&ai, 0, sizeof(ai));
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = cmd_pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            r = vkAllocateCommandBuffers(device, &ai, &f.cmd);
            if(r != VK_SUCCESS)
                return fail(error, "vkAllocateCommandBuffers", r);

            VkFenceCreateInfo fi;
            memset(&fi, 0, sizeof(fi));
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            r = vkCreateFence(device, &fi, NULL, &f.fence);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateFence", r);

            VkSemaphoreCreateInfo si;
            memset(&si, 0, sizeof(si));
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            r = vkCreateSemaphore(device, &si, NULL, &f.image_available);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateSemaphore", r);

            VkDescriptorSetAllocateInfo da;
            memset(&da, 0, sizeof(da));
            da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            da.descriptorPool = desc_pool;
            da.descriptorSetCount = 1;
            da.pSetLayouts = &set_layout;
            r = vkAllocateDescriptorSets(device, &da, &f.set);
            if(r != VK_SUCCESS)
                return fail(error, "vkAllocateDescriptorSets", r);
        }
        return true;
    }

    void destroy_swapchain_resources()
    {
        for(size_t i = 0; i < framebuffers.size(); i++)
            vkDestroyFramebuffer(device, framebuffers[i], NULL);
        for(size_t i = 0; i < swap_views.size(); i++)
            vkDestroyImageView(device, swap_views[i], NULL);
        for(size_t i = 0; i < render_done.size(); i++)
            vkDestroySemaphore(device, render_done[i], NULL);
        framebuffers.clear();
        swap_views.clear();
        render_done.clear();
        swap_images.clear();
    }

    static bool is_surface_gone(VkResult r)
    {
        return r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    }

    // What the window's Vulkan surface is built on: on Android the current
    // native surface, NULL while the app is in the background. Elsewhere the
    // window keeps its surface for good.
    void *native_surface() const
    {
#ifdef __ANDROID__
        return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                      SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL);
#else
        return window;
#endif
    }

    // Replaces a surface that went away. Only the surface and the swapchain
    // depend on it: the device, pipelines and the UI renderer stay.
    bool recreate_surface(void *handle, std::string *error)
    {
        vkDeviceWaitIdle(device);
        destroy_swapchain_resources();
        if(swapchain)
        {
            vkDestroySwapchainKHR(device, swapchain, NULL);
            swapchain = VK_NULL_HANDLE;
        }
        if(surface)
        {
            vkDestroySurfaceKHR(instance, surface, NULL);
            surface = VK_NULL_HANDLE;
        }
        surface_handle = NULL;
        surface_lost = true;

        if(!SDL_Vulkan_CreateSurface(window, instance, NULL, &surface))
        {
            surface = VK_NULL_HANDLE;
            return fail(error, SDL_GetError());
        }
        VkBool32 can_present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, queue_family, surface, &can_present);
        if(!can_present)
            return fail(error, "the new surface cannot be presented to");

        surface_handle = handle;
        surface_lost = false;
        swapchain_dirty = true;
        return true;
    }

    // Returns false on a hard error. A zero-sized window leaves extent at 0
    // (minimized), and present() then skips drawing.
    bool create_swapchain(std::string *error)
    {
        VkSurfaceCapabilitiesKHR caps;
        VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
        if(is_surface_gone(r))
        {
            surface_lost = true;
            return true;
        }
        if(r != VK_SUCCESS)
            return fail(error, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r);

        VkExtent2D want = caps.currentExtent;
        if(want.width == 0xFFFFFFFFu)
        {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            want.width = (uint32_t)std::max(0, pw);
            want.height = (uint32_t)std::max(0, ph);
            want.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, want.width));
            want.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, want.height));
        }

        if(want.width == 0 || want.height == 0)
        {
            extent = want;
            swapchain_dirty = true; // try again once the window has a size
            return true;
        }

        VkSurfaceFormatKHR fmt = choose_surface_format();
        bool format_changed = (swap_format != fmt.format);
        swap_format = fmt.format;

        uint32_t image_count = caps.minImageCount + 1;
        if(caps.maxImageCount > 0 && image_count > caps.maxImageCount)
            image_count = caps.maxImageCount;

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        const VkCompositeAlphaFlagBitsKHR alphas[] = { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                       VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR };
        for(size_t i = 0; i < 4; i++)
        {
            if(caps.supportedCompositeAlpha & alphas[i])
            {
                alpha = alphas[i];
                break;
            }
        }

        VkSwapchainCreateInfoKHR ci;
        memset(&ci, 0, sizeof(ci));
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface;
        ci.minImageCount = image_count;
        ci.imageFormat = fmt.format;
        ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = want;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // Identity if possible (Android composes the rotation for us).
        ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
        ci.compositeAlpha = alpha;
        ci.presentMode = choose_present_mode();
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = swapchain;

        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        r = vkCreateSwapchainKHR(device, &ci, NULL, &new_swapchain);
        if(is_surface_gone(r))
        {
            surface_lost = true;
            return true;
        }
        if(r != VK_SUCCESS)
            return fail(error, "vkCreateSwapchainKHR", r);

        destroy_swapchain_resources();
        if(swapchain)
            vkDestroySwapchainKHR(device, swapchain, NULL);
        swapchain = new_swapchain;
        extent = want;

        if(format_changed && render_pass)
        {
            // Pipeline objects depend on the render pass format.
            vkDestroyPipeline(device, pipeline, NULL);
            vkDestroyRenderPass(device, render_pass, NULL);
            pipeline = VK_NULL_HANDLE;
            render_pass = VK_NULL_HANDLE;
        }

        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &n, NULL);
        swap_images.resize(n);
        vkGetSwapchainImagesKHR(device, swapchain, &n, &swap_images[0]);

        if(!render_pass && !create_render_pass(error))
            return false;
        if(!pipeline && !create_graphics_pipeline(error))
            return false;

        swap_views.resize(n);
        framebuffers.resize(n);
        render_done.resize(n);
        for(uint32_t i = 0; i < n; i++)
        {
            VkImageViewCreateInfo vi;
            memset(&vi, 0, sizeof(vi));
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = swap_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swap_format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            r = vkCreateImageView(device, &vi, NULL, &swap_views[i]);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateImageView", r);

            VkFramebufferCreateInfo fb;
            memset(&fb, 0, sizeof(fb));
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = render_pass;
            fb.attachmentCount = 1;
            fb.pAttachments = &swap_views[i];
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;
            r = vkCreateFramebuffer(device, &fb, NULL, &framebuffers[i]);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateFramebuffer", r);

            VkSemaphoreCreateInfo si;
            memset(&si, 0, sizeof(si));
            si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            r = vkCreateSemaphore(device, &si, NULL, &render_done[i]);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateSemaphore", r);
        }

        swapchain_dirty = false;
        return true;
    }

    void destroy_texture(Frame &f)
    {
        if(f.tex_view) vkDestroyImageView(device, f.tex_view, NULL);
        if(f.tex) vkDestroyImage(device, f.tex, NULL);
        if(f.tex_mem) vkFreeMemory(device, f.tex_mem, NULL);
        f.tex_view = VK_NULL_HANDLE;
        f.tex = VK_NULL_HANDLE;
        f.tex_mem = VK_NULL_HANDLE;
        f.tex_w = f.tex_h = 0;
        f.tex_initialized = false;
        // The next view may get this one's handle value, and the descriptor
        // must still be rewritten: it refers to the image just destroyed.
        f.set_view = VK_NULL_HANDLE;
    }

    void destroy_staging(Frame &f)
    {
        if(f.staging_ptr) vkUnmapMemory(device, f.staging_mem);
        if(f.staging) vkDestroyBuffer(device, f.staging, NULL);
        if(f.staging_mem) vkFreeMemory(device, f.staging_mem, NULL);
        f.staging_ptr = NULL;
        f.staging = VK_NULL_HANDLE;
        f.staging_mem = VK_NULL_HANDLE;
        f.staging_size = 0;
    }

    bool ensure_frame_resources(Frame &f, int w, int h, std::string *error)
    {
        VkDeviceSize need = (VkDeviceSize)w * h * 4;
        if(f.staging_size < need)
        {
            destroy_staging(f);
            VkBufferCreateInfo bi;
            memset(&bi, 0, sizeof(bi));
            bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size = need;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkResult r = vkCreateBuffer(device, &bi, NULL, &f.staging);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateBuffer", r);
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(device, f.staging, &req);
            VkMemoryAllocateInfo mi;
            memset(&mi, 0, sizeof(mi));
            mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mi.allocationSize = req.size;
            mi.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if(mi.memoryTypeIndex == UINT32_MAX)
                return fail(error, "No host-visible memory for uploads");
            r = vkAllocateMemory(device, &mi, NULL, &f.staging_mem);
            if(r != VK_SUCCESS)
                return fail(error, "vkAllocateMemory", r);
            vkBindBufferMemory(device, f.staging, f.staging_mem, 0);
            r = vkMapMemory(device, f.staging_mem, 0, need, 0, &f.staging_ptr);
            if(r != VK_SUCCESS)
                return fail(error, "vkMapMemory", r);
            f.staging_size = need;
        }

        if(f.tex_w != w || f.tex_h != h)
        {
            destroy_texture(f);
            VkImageCreateInfo ii;
            memset(&ii, 0, sizeof(ii));
            ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ii.imageType = VK_IMAGE_TYPE_2D;
            ii.format = VK_FORMAT_B8G8R8A8_UNORM; // 0xAARRGGBB little-endian
            ii.extent.width = (uint32_t)w;
            ii.extent.height = (uint32_t)h;
            ii.extent.depth = 1;
            ii.mipLevels = 1;
            ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkResult r = vkCreateImage(device, &ii, NULL, &f.tex);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateImage", r);
            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(device, f.tex, &req);
            VkMemoryAllocateInfo mi;
            memset(&mi, 0, sizeof(mi));
            mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mi.allocationSize = req.size;
            mi.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if(mi.memoryTypeIndex == UINT32_MAX)
                mi.memoryTypeIndex = find_memory_type(req.memoryTypeBits, 0);
            r = vkAllocateMemory(device, &mi, NULL, &f.tex_mem);
            if(r != VK_SUCCESS)
                return fail(error, "vkAllocateMemory", r);
            vkBindImageMemory(device, f.tex, f.tex_mem, 0);

            VkImageViewCreateInfo vi;
            memset(&vi, 0, sizeof(vi));
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = f.tex;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = ii.format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            r = vkCreateImageView(device, &vi, NULL, &f.tex_view);
            if(r != VK_SUCCESS)
                return fail(error, "vkCreateImageView", r);
            f.tex_w = w;
            f.tex_h = h;
            f.tex_initialized = false;
        }
        return true;
    }

    void update_descriptor(Frame &f, VkSampler sampler)
    {
        if(f.set_sampler == sampler && f.set_view == f.tex_view)
            return;
        VkDescriptorImageInfo ii;
        ii.sampler = sampler;
        ii.imageView = f.tex_view;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w;
        memset(&w, 0, sizeof(w));
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = f.set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(device, 1, &w, 0, NULL);
        f.set_sampler = sampler;
        f.set_view = f.tex_view;
    }

    // Destination rectangle of a w x h frame in the swapchain, in pixels.
    void dest_rect(int w, int h, ScaleMode mode, float *x0, float *y0, float *x1, float *y1)
    {
        float ew = (float)extent.width, eh = (float)extent.height;
        float dw = ew, dh = eh;
        if(mode == SCALE_INTEGER)
        {
            int s = std::min((int)extent.width / w, (int)extent.height / h);
            if(s < 1)
                mode = SCALE_ASPECT; // window smaller than the frame
            else
            {
                dw = (float)(w * s);
                dh = (float)(h * s);
            }
        }
        if(mode == SCALE_ASPECT || mode == SCALE_ASPECT_SMOOTH)
        {
            float s = std::min(ew / w, eh / h);
            dw = w * s;
            dh = h * s;
        }
        *x0 = (float)(int)((ew - dw) / 2);
        *y0 = (float)(int)((eh - dh) / 2);
        *x1 = *x0 + dw;
        *y1 = *y0 + dh;
    }
};

VulkanPresenter::VulkanPresenter() : impl(new Impl) {}

VulkanPresenter::~VulkanPresenter()
{
    shutdown();
    delete impl;
}

const char *VulkanPresenter::device_name() const
{
    return impl->device_name.c_str();
}

bool VulkanPresenter::init(SDL_Window *window, bool vsync, std::string *error)
{
    Impl &d = *impl;
    d.window = window;
    d.vsync = vsync;
    if(!d.create_instance(error)) return false;
    d.surface_handle = d.native_surface();
    if(!d.pick_device(error)) return false;
    if(!d.create_device(error)) return false;
    // Render pass + pipeline need the swapchain format, which create_swapchain picks.
    VkSurfaceFormatKHR fmt = d.choose_surface_format();
    d.swap_format = fmt.format;
    if(!d.create_render_pass(error)) return false;
    if(!d.create_static_objects(error)) return false;
    if(!d.create_graphics_pipeline(error)) return false;
    if(!d.create_frames(error)) return false;
    if(!d.create_swapchain(error)) return false;
    return true;
}

void VulkanPresenter::shutdown()
{
    Impl &d = *impl;
    if(!d.instance)
        return;
    shutdown_ui();
    if(d.device)
    {
        vkDeviceWaitIdle(d.device);
        for(int i = 0; i < kFramesInFlight; i++)
        {
            Impl::Frame &f = d.frames[i];
            d.destroy_texture(f);
            d.destroy_staging(f);
            if(f.fence) vkDestroyFence(d.device, f.fence, NULL);
            if(f.image_available) vkDestroySemaphore(d.device, f.image_available, NULL);
        }
        memset(d.frames, 0, sizeof(d.frames));
        d.destroy_swapchain_resources();
        if(d.swapchain) vkDestroySwapchainKHR(d.device, d.swapchain, NULL);
        if(d.cmd_pool) vkDestroyCommandPool(d.device, d.cmd_pool, NULL);
        if(d.desc_pool) vkDestroyDescriptorPool(d.device, d.desc_pool, NULL);
        if(d.sampler_nearest) vkDestroySampler(d.device, d.sampler_nearest, NULL);
        if(d.sampler_linear) vkDestroySampler(d.device, d.sampler_linear, NULL);
        if(d.pipeline) vkDestroyPipeline(d.device, d.pipeline, NULL);
        if(d.pipeline_layout) vkDestroyPipelineLayout(d.device, d.pipeline_layout, NULL);
        if(d.set_layout) vkDestroyDescriptorSetLayout(d.device, d.set_layout, NULL);
        if(d.render_pass) vkDestroyRenderPass(d.device, d.render_pass, NULL);
        vkDestroyDevice(d.device, NULL);
    }
    if(d.surface) vkDestroySurfaceKHR(d.instance, d.surface, NULL);
    vkDestroyInstance(d.instance, NULL);

    SDL_Window *w = d.window;
    bool vs = d.vsync;
    delete impl;
    impl = new Impl;
    impl->window = w;
    impl->vsync = vs;
}

bool VulkanPresenter::init_ui()
{
    Impl &d = *impl;
    if(d.ui_ready)
        return true;
    if(!d.device || !d.render_pass || !ImGui::GetCurrentContext())
        return false;

    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_0;
    ii.Instance = d.instance;
    ii.PhysicalDevice = d.gpu;
    ii.Device = d.device;
    ii.QueueFamily = d.queue_family;
    ii.Queue = d.queue;
    ii.DescriptorPoolSize = 64;   // the backend makes its own pool
    ii.MinImageCount = 2;
    ii.ImageCount = (uint32_t)std::max<size_t>(2, d.swap_images.size());
    ii.PipelineInfoMain.RenderPass = d.render_pass;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if(!ImGui_ImplVulkan_Init(&ii))
        return false;
    d.ui_ready = true;
    return true;
}

void VulkanPresenter::shutdown_ui()
{
    Impl &d = *impl;
    if(!d.ui_ready)
        return;
    if(d.device)
        vkDeviceWaitIdle(d.device);
    ImGui_ImplVulkan_Shutdown();
    d.ui_ready = false;
}

bool VulkanPresenter::ui_ready() const
{
    return impl->ui_ready;
}

void VulkanPresenter::ui_new_frame()
{
    if(impl->ui_ready)
        ImGui_ImplVulkan_NewFrame();
}

void VulkanPresenter::set_vsync(bool vsync)
{
    if(impl->vsync != vsync)
    {
        impl->vsync = vsync;
        impl->swapchain_dirty = true;
    }
}

void VulkanPresenter::notify_resized()
{
    impl->swapchain_dirty = true;
}

bool VulkanPresenter::present(const uint32_t *pixels, int w, int h, int pitch, ScaleMode mode,
                              ImDrawData *ui)
{
    Impl &d = *impl;
    if(!d.device || w <= 0 || h <= 0)
        return false;

    // Android takes the window's surface away while the app is in the
    // background and gives it a new one when it returns. Nothing is drawn
    // without one, and the old VkSurfaceKHR can't be used on the new one.
    void *native = d.native_surface();
    if(!native)
        return true;
    if(d.surface_lost || native != d.surface_handle)
    {
        std::string surface_err;
        if(!d.recreate_surface(native, &surface_err))
        {
            SDL_Log("zplayer: surface: %s", surface_err.c_str());
#ifdef __ANDROID__
            return true;   // it went away again: try with the next frame
#else
            return false;
#endif
        }
    }

    Impl::Frame &f = d.frames[d.frame_index];
    vkWaitForFences(d.device, 1, &f.fence, VK_TRUE, UINT64_MAX);

    std::string err;
    if(d.swapchain_dirty || d.extent.width == 0)
    {
        vkDeviceWaitIdle(d.device);
        if(!d.create_swapchain(&err))
        {
            SDL_Log("zplayer: swapchain: %s", err.c_str());
            return false;
        }
        if(d.surface_lost || d.extent.width == 0 || d.extent.height == 0)
            return true; // minimized: nothing to draw
    }

    uint32_t image_index = 0;
    VkResult r = vkAcquireNextImageKHR(d.device, d.swapchain, UINT64_MAX, f.image_available, VK_NULL_HANDLE, &image_index);
    if(Impl::is_surface_gone(r))
    {
        d.surface_lost = true;
        return true;
    }
    if(r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        d.swapchain_dirty = true;
        return true;
    }
    if(r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        SDL_Log("zplayer: vkAcquireNextImageKHR: %s", vk_result_name(r));
        return r != VK_ERROR_DEVICE_LOST;
    }

    if(!d.ensure_frame_resources(f, w, h, &err))
    {
        SDL_Log("zplayer: upload resources: %s", err.c_str());
        return false;
    }

    // Copy the frame into the staging buffer, tightly packed.
    uint32_t *dst = (uint32_t *)f.staging_ptr;
    for(int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w, pixels + (size_t)y * pitch, (size_t)w * 4);

    VkSampler sampler = (mode == SCALE_ASPECT_SMOOTH) ? d.sampler_linear : d.sampler_nearest;
    d.update_descriptor(f, sampler);

    vkResetFences(d.device, 1, &f.fence);
    vkResetCommandBuffer(f.cmd, 0);

    VkCommandBufferBeginInfo bi;
    memset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);

    // Texture: (undefined or shader-read) -> transfer dst -> copy -> shader read.
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = f.tex_initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // contents are fully replaced
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = f.tex;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(f.cmd, f.tex_initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy copy;
    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)w;
    copy.imageExtent.height = (uint32_t)h;
    copy.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(f.cmd, f.staging, f.tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    f.tex_initialized = true;

    VkClearValue clear;
    memset(&clear, 0, sizeof(clear));
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rp;
    memset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = d.render_pass;
    rp.framebuffer = d.framebuffers[image_index];
    rp.renderArea.extent = d.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(f.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp;
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)d.extent.width;
    vp.height = (float)d.extent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    VkRect2D sc;
    sc.offset.x = sc.offset.y = 0;
    sc.extent = d.extent;
    vkCmdSetScissor(f.cmd, 0, 1, &sc);

    float x0, y0, x1, y1;
    d.dest_rect(w, h, mode, &x0, &y0, &x1, &y1);
    float rect[4];
    rect[0] = x0 / d.extent.width * 2.0f - 1.0f;
    rect[1] = y0 / d.extent.height * 2.0f - 1.0f;
    rect[2] = x1 / d.extent.width * 2.0f - 1.0f;
    rect[3] = y1 / d.extent.height * 2.0f - 1.0f;

    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline);
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipeline_layout, 0, 1, &f.set, 0, NULL);
    vkCmdPushConstants(f.cmd, d.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rect), rect);
    vkCmdDraw(f.cmd, 4, 1, 0, 0);
    if(ui && d.ui_ready)
        ImGui_ImplVulkan_RenderDrawData(ui, f.cmd);
    vkCmdEndRenderPass(f.cmd);
    vkEndCommandBuffer(f.cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si;
    memset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &d.render_done[image_index];
    r = vkQueueSubmit(d.queue, 1, &si, f.fence);
    if(r != VK_SUCCESS)
    {
        SDL_Log("zplayer: vkQueueSubmit: %s", vk_result_name(r));
        return r != VK_ERROR_DEVICE_LOST;
    }

    VkPresentInfoKHR pi;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &d.render_done[image_index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &d.swapchain;
    pi.pImageIndices = &image_index;
    r = vkQueuePresentKHR(d.queue, &pi);
    if(r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        d.swapchain_dirty = true;
    else if(Impl::is_surface_gone(r))
        d.surface_lost = true;
    else if(r != VK_SUCCESS)
    {
        SDL_Log("zplayer: vkQueuePresentKHR: %s", vk_result_name(r));
        return r != VK_ERROR_DEVICE_LOST;
    }

    d.frame_index = (d.frame_index + 1) % kFramesInFlight;
    return true;
}

} // namespace zcport
