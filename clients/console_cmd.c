#include <stdio.h>
#include <semaphore.h>

#include "console.h"

//exec an command
static void
console_cmd_run(struct console_module *module, const char *entry)
{
	vector_t buffer;
	vector_init_zero(&buffer, 1, NULL);
	vector_resize(&buffer, 1000);

	FILE *pipe = popen(entry, "r");
	//reads the
	while (true) {
		size_t read, to_read = buffer.alloc_len - buffer.len;
		read = fread(vector_at(&buffer, buffer.len),
			     buffer.elemsize, to_read, pipe);
		if (read == to_read)
			vector_resize(&buffer, buffer.len * 2);
		if (feof(pipe) || ferror(pipe))
			break;
	}
	//it should return
	pclose(pipe);
	vector_destroy(&buffer);
}


static void
console_search_command(struct console_module *module, const char *to_search)
{
	if (!module->radix)
		return;
	struct raxIterator iter;
	raxStart(&iter, module->radix);


	raxStop(&iter);
}

/**
 * @brief running thread for the module,
 *
 * it first detects whether if there is an command to exec, then try to generate
 * the search results.
 *
 * This thread is a consumer, if there is an exec command or search command. It
 * should comsume it.
 */
void *thread_run_module(void *arg)
{
	struct console_module *module = arg;


	while (true) {
		if (strlen(module->exec_command)) {
			char *command = strdup(module->exec_command);
			module->exec(module, command);
			free(command);
		}
		//strdup is MT-safe
		char *command = strdup(module->command);
		module->search(module, command);
		free(command);
		//strlen is MT-safe
		if (!strlen(module->command))
			pthread_cond_wait(&module->condition, &module->mutex);
		//what happens next is that we can run the exec command.

	}
	//in the main thread, we do this
	/* if (strlen(module->command)) */
	/*	pthread_cond_signal(&module->condition); */
}
