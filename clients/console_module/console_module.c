#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <vector.h>
#include "console_module.h"

/******************************************************************************/
void
search_entry_assign(void *dst, const void *src)
{
	console_search_entry_t *d = dst;
	const console_search_entry_t *s = src;

	if (d->pstr)
		free(d->pstr);
	*d = *s;
	if (s->pstr)
		d->pstr = strdup(s->pstr);
}

void
free_console_search_entry(void *m)
{
	console_search_entry_t *entry = m;
	if (entry->pstr)
		free(entry->pstr);
}

/******************************************************************************/

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
	vector_copy_complex(&cache->last_results, v,
			    search_entry_assign);
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
	     char *command, vector_t *v,
	     bool (*filter_test)(const char *cmd, const char *candidate))
{
	console_search_entry_t *entry = NULL;
	int cmp = strcmp(cache->last_command, command);

	if (cmp == 0)
		return;
	else if (cmp < 0) {
		vector_init_zero(v, sizeof(console_search_entry_t),
				 free_console_search_entry);
		vector_for_each(entry, &cache->last_results) {
			const char *str = search_entry_get_string(entry);
			if (filter_test(command, str)) {
				search_entry_move(vector_newelem(v), entry);
			}
		}
	} else
		assert(0);

	cache_free(cache);
	cache_takes(cache, v, command);
}

static inline bool
cachable(const struct module_search_cache *cache,
	 char *command)
{
	return command != NULL && cache->last_command != NULL &&
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
 * We have a general cache method to reduce the uncessary module searching.
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
		if (module->exec_res) {
			free(module->exec_res);
			module->exec_res = NULL;
		}
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
			cache_filter(&cache, search_command, &search_results,
				module->filter_test);
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

		/* int value; */
		/* sem_getvalue(&module->semaphore, &value); */
		/* fprintf(stderr, "sem value now : %d\n", value); */
		sem_wait(&module->semaphore);
	}
	cache_free(&cache);
}

static bool
console_module_filter_test(const char *cmd, const char *entry)
{
	return (strstr(entry, cmd) == entry);
}

/******************************************************************************/

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
	if (!module->filter_test)
		module->filter_test = console_module_filter_test;
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
		if (module->exec_command)
			free(module->exec_command);
		module->exec_command = strdup(exec);
		pthread_mutex_unlock(&module->command_mutex);
		sem_post(&module->semaphore);
	}
	/* int value; */
	/* sem_getvalue(&module->semaphore, &value); */
	/* fprintf(stderr, "sem value now : %d\n", value); */
}

int
console_module_take_search_result(struct console_module *module,
				  vector_t *ret)
{
	int retcode = 0;

	if (pthread_mutex_trylock(&module->results_mutex))
		return 0;
	if (module->search_results.len == -1) //taken
		;
	else {
		vector_destroy(ret);
		*ret = module->search_results;
		module->search_results = (vector_t){0};
		module->search_results.len = -1; //taken
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
