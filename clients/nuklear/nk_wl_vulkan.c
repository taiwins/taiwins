#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/input.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>

#ifndef VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include <vulkan/vulkan.h>

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT

#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

//nuklear features
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY

#include "../client.h"
#include "../ui.h"
#include "nk_wl_internal.h"

#ifdef __DEBUG
static const char *VALIDATION_LAYERS[] = {
	"VK_LAYER_LUNARG_standard_validation",
};

static const char *INSTANCE_EXTENSIONS[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};

#define VL_COUNT 1
#define INS_EXT_COUNT 3

#else

static const char *INSTANCE_EXTENSIONS[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};

#define VL_COUNT 0
#define INS_EXT_COUNT 2

#endif

static const char* DEVICE_EXTENSIONS[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

#define DEV_EXT_COUNT 1



struct nk_vulkan_backend {
	struct nk_wl_backend base;
	//opengl command buffer
	struct nk_buffer cmds;
	//vulkan states
	VkInstance instance;
#ifdef __DEBUG
	VkDebugUtilsMessengerEXT debug_callback;
#endif
	VkPhysicalDevice phy_device;
	//there is no way to create shader before having a logic device, and you
	//need to choose the device based on a VkSurfaceKHR, so there si no way
	//to avoid all the verbose callback

	VkDevice logic_device;
	//we need a queue that support graphics and presentation
	VkQueue graphics_queue;
	VkQueue present_queue;

	VkShaderModule vert_shader;
	VkShaderModule pixel_shader;

	VkAllocationCallbacks *alloc_callback;

};


static void
nk_wl_render(struct nk_wl_backend *bkend)
{
	fprintf(stderr, "this sucks\n");
}


#ifdef __DEBUG
static inline bool
check_validation_layer(const VkLayerProperties *layers, uint32_t layer_count,
		       const char *check_layers[], uint32_t clayer_count)
{

	for (int i = 0; i < clayer_count; i++) {
		const char *to_check = check_layers[i];
		bool layer_found = false;
		for (int j = 0; j < layer_count; j++) {
			if (!strcmp(to_check, layers[j].layerName)) {
				layer_found = true;
				break;
			}
		}
		if (!layer_found)
			return false;
	}
	return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_messenger(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	       VkDebugUtilsMessageTypeFlagsEXT messageType,
	       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	       void *pUserData)
{
	const char *message_types[] = {
		"ERRO", "WARN", "info", "verbos"
	};
	int message_type = 2;
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		message_type = 0;
	else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		message_type = 1;
	else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
		message_type = 2;
	else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
		message_type = 3;
	fprintf(stderr, "validation layer %s: %s\n", message_types[message_type],
		pCallbackData->pMessage);

	return message_type < 2;
}

#endif

static bool
check_device_extensions(VkPhysicalDevice dev)
{
	uint32_t count;
	vkEnumerateDeviceExtensionProperties(dev, NULL, //layername
					     &count, NULL);
	VkExtensionProperties extensions[count];
	vkEnumerateDeviceExtensionProperties(dev, NULL, &count,
					     extensions);
	for (int i = 0; i < DEV_EXT_COUNT; i++) {
		bool found = false;
		for (int j = 0; j < count; j++) {
			if (strcmp(DEVICE_EXTENSIONS[i],
				   extensions[j].extensionName) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

static void
init_instance(struct nk_vulkan_backend *b)
{
	VkApplicationInfo appinfo = {};
	appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appinfo.pApplicationName = "nk_vulkan";
	appinfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
	appinfo.pEngineName = "No Engine";
	appinfo.engineVersion = 1;
	appinfo.apiVersion = VK_API_VERSION_1_1;

	//define extensions
	//nvidia is not supporting vk_khr_wayland

	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &appinfo;
	create_info.enabledExtensionCount = INS_EXT_COUNT;
	create_info.ppEnabledExtensionNames = INSTANCE_EXTENSIONS;

#ifdef  __DEBUG
	{
		create_info.enabledLayerCount = VL_COUNT;
		create_info.ppEnabledLayerNames = VALIDATION_LAYERS;
		uint32_t layer_count;
		vkEnumerateInstanceLayerProperties(&layer_count, NULL);
		VkLayerProperties available_layers[layer_count];
		vkEnumerateInstanceLayerProperties(&layer_count,
						   available_layers);
		assert(check_validation_layer(available_layers,
					      layer_count,
					      VALIDATION_LAYERS,
					      1));

	}
#else
	create_info.enabledLayerCount = VL_COUNT;
#endif
	assert(vkCreateInstance(&create_info, b->alloc_callback, &b->instance)
	       == VK_SUCCESS);

#ifdef __DEBUG
	VkDebugUtilsMessengerCreateInfoEXT mesg_info = {};
	mesg_info.sType =
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	mesg_info.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT;
	mesg_info.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT;
	mesg_info.pfnUserCallback = debug_messenger;
	mesg_info.pUserData = NULL;

	PFN_vkCreateDebugUtilsMessengerEXT mesg_create_fun =
		(PFN_vkCreateDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(b->instance, "vkCreateDebugUtilsMessengerEXT");
	if (mesg_create_fun != NULL)
		mesg_create_fun(b->instance, &mesg_info,
				b->alloc_callback,
				&b->debug_callback);
#endif

}

static void
init_device(struct nk_vulkan_backend *b)
{
	uint32_t device_count = 0;
	int device_idx = -1;
	vkEnumeratePhysicalDevices(b->instance, &device_count, NULL);
	assert(device_count);

	int32_t qf_index = -1;
	VkPhysicalDevice devices[device_count];
	vkEnumeratePhysicalDevices(b->instance, &device_count, devices);
	//I should actually check the one that is connecting to the monitor
	//right now, but it is not really a big deal
	for (int i = 0; i < device_count; i++) {
		VkPhysicalDeviceProperties dev_probs;
		VkPhysicalDeviceFeatures dev_features;
		vkGetPhysicalDeviceProperties(devices[i], &dev_probs);
		vkGetPhysicalDeviceFeatures(devices[i], &dev_features);
#ifdef __DEBUG
		//anyway
		fprintf(stderr, "the %d device %s support wide lines.\n", i,
			(dev_features.wideLines ? "does" : "does not"));
		fprintf(stderr, "the %d device %s support depth clamp.\n", i,
			(dev_features.depthClamp ? "does" : "does not"));
		fprintf(stderr, "the %d device %s support shader storage multisample.\n", i,
			(dev_features.shaderStorageImageMultisample ? "does" : "does not"));
#endif
		//check graphics family
		int32_t dev_graphics_queue = -1;
		bool qf_has_graphics = false;
		uint32_t qf_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, NULL);
		assert(qf_count);
		fprintf(stderr, "this gpu has %d queue%s \n", qf_count,
			qf_count == 1 ? "" : "s");
		VkQueueFamilyProperties qf_probs[qf_count];
		vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qf_probs);
		for (int j = 0; j < qf_count; j++)
			if (qf_probs[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				//you have actually graphics queue, compute queue, transfer queue
				qf_has_graphics = true;
				dev_graphics_queue = j;
				break;
			}
		//check the device extensions
		bool extensions_support = check_device_extensions(devices[i]);

		if ((dev_probs.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
		     dev_probs.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) &&
		    dev_features.geometryShader &&
		    qf_has_graphics && extensions_support) {
			device_idx = i;
			qf_index = dev_graphics_queue;
		}
	}
	assert(device_idx >= 0);
	//device queue, most likely the graphics queue and presentation queue is
	//the same, but presentation queue needs a surface.
	float que_prio = 1.0;
	VkDeviceQueueCreateInfo que_info = {};
	que_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	que_info.queueFamilyIndex = qf_index;
	que_info.queueCount = 1;
	que_info.pQueuePriorities = &que_prio;

	//device features
	VkPhysicalDeviceFeatures dev_features = {};
	VkDeviceCreateInfo dev_info = {};
	dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_info.pQueueCreateInfos = &que_info;
	dev_info.queueCreateInfoCount = 1;
	//TODO: add new features
	dev_info.pEnabledFeatures = &dev_features;
	//extensions
	dev_info.enabledExtensionCount = DEV_EXT_COUNT;
	dev_info.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
	//layers
	dev_info.enabledLayerCount = VL_COUNT;
#ifdef __DEBUG //validation layer
	dev_info.ppEnabledLayerNames = VALIDATION_LAYERS;
#endif
	b->phy_device = devices[device_idx];
	assert(vkCreateDevice(devices[device_idx], &dev_info,
			      b->alloc_callback, &b->logic_device) == VK_SUCCESS);
	vkGetDeviceQueue(b->logic_device, qf_index, 0, &b->graphics_queue);
	//the queue family actually need to support the presentation to the
	//specific surface, but it is unlikely you will hit a surface without
	//that support though, we just choose the device for now.

}

static void
select_presentation_queue(struct nk_vulkan_backend *b, VkSurfaceKHR *surface)
{
	VkBool32 support_presentation;
	uint32_t qf_count;
	int32_t pres_idx = -1;
	vkGetPhysicalDeviceQueueFamilyProperties(b->phy_device, &qf_count, NULL);
	VkQueueFamilyProperties qf_probs[qf_count];
	vkGetPhysicalDeviceQueueFamilyProperties(b->phy_device, &qf_count, qf_probs);
	for (int i = 0; i < qf_count; i++) {
		vkGetPhysicalDeviceSurfaceSupportKHR(b->phy_device, i, *surface, &support_presentation);
		if (support_presentation) {
			pres_idx = i;
			break;
		}
	}
	assert(pres_idx >= 0);
	vkGetDeviceQueue(b->logic_device, pres_idx, 0,
			 &b->present_queue);
}

static void
create_shaders(struct nk_vulkan_backend *b)
{
	//use shaderc to load shaders from string

	VkShaderModuleCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = 0;
	info.pCode = NULL;
	assert(vkCreateShaderModule(b->logic_device, &info,
				    b->alloc_callback,
				    &b->vert_shader) == VK_SUCCESS);
	assert(vkCreateShaderModule(b->logic_device, &info,
				    b->alloc_callback,
				    &b->pixel_shader) == VK_SUCCESS);
	VkPipelineShaderStageCreateInfo vstage_info = {};
	vstage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vstage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vstage_info.module = b->vert_shader;
}

///exposed APIS
void
nk_vulkan_impl_app_surface(struct app_surface *surf, struct nk_wl_backend *bkend,
			   nk_wl_drawcall_t draw_cb,
			   uint32_t w, uint32_t h, uint32_t x, uint32_t y)
{
	nk_wl_impl_app_surface(surf, bkend, draw_cb, w, h, x, y, 1);
}


struct nk_wl_backend *
nk_vulkan_backend_create(void)
{
	struct nk_vulkan_backend *backend = malloc(sizeof(struct nk_vulkan_backend));
	backend->alloc_callback = NULL;
	init_instance(backend);
	init_device(backend);
	//yeah, creating device, okay, I do not need to
	return &backend->base;
}

//this function can be used to accelerate the
struct nk_wl_backend *
nk_vulkan_backend_clone(struct nk_wl_backend *b)
{
	//maybe we can simply uses the same backend...
	return NULL;
}

void
nk_vulkan_backend_destroy(struct nk_wl_backend *b)
{
	struct nk_vulkan_backend *vb = container_of(b, struct nk_vulkan_backend, base);
	vkDestroyDevice(vb->logic_device, vb->alloc_callback);

#ifdef  __DEBUG
	PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug =
		(PFN_vkDestroyDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(vb->instance, "vkDestroyDebugUtilsMessengerEXT");
	if (destroy_debug != NULL)
		destroy_debug(vb->instance, vb->debug_callback, vb->alloc_callback);
#endif
	vkDestroyInstance(vb->instance, vb->alloc_callback);
}
