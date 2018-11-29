#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <linux/input.h>
#include <time.h>
#include <stdbool.h>

#ifndef VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>

#ifndef NK_IMPLEMENTATION
#define NK_IMPLEMENTATION
#endif

#define NK_EGL_CMD_SIZE 4096
#define MAX_VERTEX_BUFFER 512 * 128
#define MAX_ELEMENT_BUFFER 128 * 128

//nuklear features
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY

#include "../client.h"
#include "../ui.h"
#include "nk_wl_internal.h"


struct nk_vulkan_backend {
	struct nk_wl_backend base;
	//opengl command buffer
	struct nk_buffer cmds;

};


static void
createInstance(bool enable_validation)
{
	VkApplicationInfo appinfo = {};
	appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appinfo.pApplicationName = "nk_vulkan";
	appinfo.applicationVersion = VK_MAKE_VERSION(0, 0, 0);
	appinfo.pEngineName = "No Engine";
	appinfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
	appinfo.apiVersion = VK_API_VERSION_1_1;

	//define extensions
	const char *instance_extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
	};
	const char *validation_layers[] = {
		"VK_LAYER_LUNARG_standard_validation",
	};

	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &appinfo;
	create_info.enabledExtensionCount = 2;
	create_info.ppEnabledExtensionNames = instance_extensions;
	if (!enable_validation)
	//add validation layer
		create_info.enabledLayerCount = 0;
	else {
		create_info.enabledLayerCount = 1;
		create_info.ppEnabledLayerNames = validation_layers;
	}

	VkInstance instance;
	//create the instance!
	assert(vkCreateInstance(&create_info, NULL, &instance) == VK_SUCCESS);

}

struct nk_wl_backend *
nk_vulkan_backend_create()
{
	struct nk_vulkan_backend *backend = malloc(sizeof(struct nk_vulkan_backend));




	//yeah, creating device, okay, I do not need to
	return backend;
}
