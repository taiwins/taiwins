#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <vector.h>
#include "../console.h"

/**
 * @brief general scenario
 *
 * console main thread is issuing search commands to module threads. When it
 * does so, it should first clean out the search results if any (there could be
 * cases where console thread is issuing the command while module is doing the
 * search as well. Such that By the time it finished, new commands is coming in,
 * so at the module side, checking )
 */

struct module_search_cache {
	char *last_command;
	vector_t last_results;
};

static inline void
cache_takes(struct module_search_cache *cache,
	    vector_t *v, const char *command)
{
	if (cache->last_command)
		free(cache->last_command);
	cache->last_command = strdup(command);
	if (cache->last_results.elems) {
		vector_destroy(&cache->last_results);
	}
	vector_copy(&cache->last_results, v);
}

static inline void
cache_init(struct module_search_cache *cache)
{
	cache->last_command = NULL;
	cache->last_results = (vector_t){0};
}

static inline void
cache_free(struct module_search_cache *cache)
{
	if (cache->last_command)
		free(cache->last_command);
	if (cache->last_results.elems)
		vector_destroy(&cache->last_results);
	cache_init(cache);
}

static void
cache_filter(struct module_search_cache *cache,
	     char *command, vector_t *v)
{
	int cmp = strcmp(cache->last_command, command);

	if (cmp == 0)
		vector_copy(v, &cache->last_results);
	//a more interesting case, results can be filtered locally
	else if (cmp < 0) {
		vector_init_zero(v, cache->last_results.elemsize,
				 cache->last_results.free);

		for (int i = 0; i < cache->last_results.len; i++) {
			console_search_entry_t entry =
				get_search_line(&cache->last_results, i);
			//for commands, we search whole string
			if (entry.cmd && strstr(*entry.cmd, command) == *entry.cmd &&
			    strcmp(command, *entry.cmd) <= 0)
				vector_append(v, entry.cmd);
			//for apps, lossily find
			if (entry.app && strstr(entry.app->exec, command))
				vector_append(v, entry.app);
			//same as path
			if (entry.path && strstr(*entry.path, command)) {
				char *path = strdup(*entry.path);
				vector_append(v, &path);
			}
		}
	}
	cache_free(cache);
	cache_takes(cache, v, command);
}

static inline bool
cachable(const struct module_search_cache *cache,
	 char *command) {
	return command != NULL &&
		cache->last_command != NULL &&
		strstr(command, cache->last_command) == command &&
		strcmp(cache->last_command, command) <= 0;
}

/**
 * @brief running thread for the module,
 *
 * module thread is a consumer for commands and producer for results. If the
 * results is not taken, it needs to clean up before do it again.
 *
 * Console thread does the reverse. It is the consumer of results and producer
 * of commands, if the commands it produced is not taken, it needs to reset it
 * manually.
 *
 * It would be nice to have a cache method so we need not to call in search
 * every time. The main thread does not know whether results answers to the last
 * search; the threads however may lost their search.
 */
void *
thread_run_module(void *arg)
{
	struct console_module *module = arg;
	char *exec_command = NULL, *search_command = NULL;
	char *exec_res;
	vector_t search_results = {0};
	int exec_ret, search_ret;
	struct module_search_cache cache;

	cache_init(&cache);
	while (true) {
		//exec, enter critial
		pthread_mutex_lock(&module->command_mutex);
		if (module->exec_command) {
			exec_command = module->exec_command;
			module->exec_command = NULL;
		}
		pthread_mutex_unlock(&module->command_mutex);
		if (exec_command) {
			exec_ret = module->exec(module, exec_command,
						&exec_res);
			free(exec_command);
			exec_command = NULL;
		}

		//again, if results are not taken, we need to free it.
		pthread_mutex_lock(&module->results_mutex);
		if (module->exec_res)
			free(module->exec_res);
		if (exec_res)
			module->exec_res = exec_res;
		module->exec_ret = exec_ret;
		exec_res = NULL;
		pthread_mutex_unlock(&module->results_mutex);

		//search
		pthread_mutex_lock(&module->command_mutex);
		if (module->search_command) {
			search_command = module->search_command;
			module->search_command = NULL;
		}
		pthread_mutex_unlock(&module->command_mutex);
		//deal with cache first
		if (module->support_cache && cachable(&cache, search_command))
			cache_filter(&cache, search_command, &search_results);
		else if (search_command) {
			search_ret = module->search(module, search_command,
						    &search_results);
			//fprintf(stderr, "search for %s has %d results\n", search_command,
			//	search_results.len);
			cache_takes(&cache, &search_results, search_command);
			free(search_command);
			search_command = NULL;
		}

		pthread_mutex_lock(&module->results_mutex);
		if (module->search_results.elems)
			vector_destroy(&module->search_results);
		module->search_results = search_results;
		module->search_ret = search_ret;
		search_results = (vector_t){0};
		search_ret = 0;
		pthread_mutex_unlock(&module->results_mutex);

		sem_wait(&module->semaphore);
	}
	cache_free(&cache);
}

void
console_module_init(struct console_module *module,
		    struct desktop_console *console)
{
	module->console = console;
	pthread_mutex_init(&module->command_mutex, NULL);
	pthread_mutex_init(&module->results_mutex, NULL);
	sem_init(&module->semaphore, 0, 0);
	pthread_create(&module->thread, NULL, thread_run_module,
		       (void *)module);
	if (module->init_hook)
		module->init_hook(module);
}

void
console_module_release(struct console_module *module)
{
	int lock_state = -1;

	pthread_mutex_lock(&module->command_mutex);
	if (module->search_command) {
		free(module->search_command);
		module->search_command = NULL;
	}
	if (module->exec_command) {
		free(module->exec_command);
		module->exec_command = NULL;
	}
	pthread_mutex_unlock(&module->command_mutex);

	//wake the threads
	while (lock_state <= 0) {
		sem_getvalue(&module->semaphore, &lock_state);
		sem_post(&module->semaphore);
	}
	pthread_cancel(module->thread);
	pthread_join(module->thread, NULL);
	pthread_mutex_destroy(&module->command_mutex);
	pthread_mutex_destroy(&module->results_mutex);
	sem_destroy(&module->semaphore);

	//maybe results copied
	if (module->search_results.elems)
		vector_destroy(&module->search_results);
	if (module->destroy_hook)
		module->destroy_hook(module);
}

void
console_module_command(struct console_module *module,
		       const char *search, const char *exec)
{
	if (search && strlen(search)) {
		pthread_mutex_lock(&module->command_mutex);
		if (module->search_command)
			free(module->search_command);
		module->search_command = strdup(search);
		pthread_mutex_unlock(&module->command_mutex);
		sem_post(&module->semaphore);
	}
	if (exec && strlen(exec)) {
		pthread_mutex_lock(&module->command_mutex);
		if (module->search_command)
			free(module->search_command);
		module->exec_command = strdup(exec);
		pthread_mutex_unlock(&module->command_mutex);
		sem_post(&module->semaphore);
	}
	//int value;
	//sem_getvalue(&module->semaphore, &value);
	//fprintf(stderr, "sem value now : %d\n", value);
}

static inline bool
module_search_result_taken(const struct console_module *module)
{
	return (module->search_results.len == -1);
}

int
console_module_take_search_result(struct console_module *module,
				  vector_t *ret)
{
	int retcode = 0;
	if (pthread_mutex_trylock(&module->results_mutex))
		return 0;
	//pthread_mutex_lock(&module->results_mutex);

	//distinguish between no results and results taken
	if (module_search_result_taken(module));
	else {
		vector_destroy(ret);
		*ret = module->search_results;
		module->search_results = (vector_t){0};
		module->search_results.len = -1;
		retcode = module->search_ret;
		module->search_ret = 0;
	}
	pthread_mutex_unlock(&module->results_mutex);
	return retcode;
}

int
console_module_take_exec_result(struct console_module *module,
				char **result)
{
	int retcode = 0;
	if (pthread_mutex_trylock(&module->results_mutex)) {
		*result = NULL;
		return 0;
	}
	//pthread_mutex_lock(&module->results_mutex);
	if (module->exec_res)
		*result = module->exec_res;
	module->exec_res = NULL;
	retcode = module->exec_ret;
	module->exec_ret = 0;
	pthread_mutex_unlock(&module->results_mutex);

	return retcode;
}


/******************************************************************************/
static int
console_lua_module_search(struct console_module *module,
			  const char *search,
			  vector_t *res)
{
	return 0;
}

static int
console_lua_module_exec(struct console_module *module, const char *entry,
			char **out)
{
	return 0;
}

void
console_lua_module_init(struct console_module *module)
{
	lua_State *L = luaL_newstate();
	module->search = console_lua_module_search;
	module->exec = console_lua_module_exec;
	module->user_data = L;
}

void
console_lua_module_deinit(struct console_module *module)
{
	lua_State *L = module->user_data;
	lua_close(L);
}
