#include "../clients/client.h"
#include "../clients/ui.h"


struct nk_wl_backend;

struct nk_wl_backend *nk_vulkan_backend_create(void);


int main(int argc, char *argv[])
{
	struct nk_wl_backend *backend = nk_vulkan_backend_create();
	free(backend);
	return 0;
}
