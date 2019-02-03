#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <sequential.h>
#include <compositor.h>

enum tw_binding_type {
	TW_BINDING_key,
	TW_BINDING_btn,
	TW_BINDING_axis,
	TW_BINDING_tch,
};

struct tw_press {
	union {
		xkb_keycode_t keycode;
		uint32_t btn;
		enum wl_pointer_axis axis;
		bool tch;
	};
	uint32_t modifier;
};

//only server need to know about this
//server register all that bindings
struct taiwins_binding {
	struct tw_press press[5];
	enum tw_binding_type type;
	//the name is probably used for lua
	char name[128];
	void *func;
};

struct event_map {
	const char *name;
	uint32_t event_code;
};

static const struct event_map modifiers_map[] =
{
	{"C", MODIFIER_CTRL}, {"Ctrl", MODIFIER_CTRL},
	{"M", MODIFIER_ALT}, {"Alt", MODIFIER_ALT},
	{"s", MODIFIER_SUPER}, {"Super", MODIFIER_SUPER},
	{"S", MODIFIER_SHIFT}, {"Shift", MODIFIER_SHIFT},
};

static const struct event_map special_keys_table[] = {
	//we are here to deal with any key which is not in asci table
	{"F1", KEY_F1}, {"F2", KEY_F2}, {"F3", KEY_F3},
	{"F4", KEY_F4}, {"F5", KEY_F5}, {"F6", KEY_F6},
	{"F7", KEY_F7}, {"F8", KEY_F8}, {"F9", KEY_F9},
	{"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
	//function keys
	{"enter", KEY_ENTER}, {"minus", KEY_MINUS},
	{"del", KEY_DELETE}, {"home", KEY_HOME},
	{"end", KEY_END}, {"pgup", KEY_PAGEUP},
	{"pgdn", KEY_PAGEDOWN},
	{"pause", KEY_PAUSE}, {"break", KEY_BREAK},
	{"scrlk", KEY_SCROLLLOCK}, {"insert", KEY_INSERT},
	{"prtsc", KEY_RESERVED}, //TODO
	//arrows
	{"left", KEY_LEFT}, {"right", KEY_RIGHT},
	{"up", KEY_UP}, {"down", KEY_DOWN},
	{"mute", KEY_MUTE}, {"volume_dn", KEY_VOLUMEDOWN},
	{"volume_up", KEY_VOLUMEUP},
	{"bn_up", KEY_BRIGHTNESSUP}, {"bn_dn", KEY_BRIGHTNESSDOWN},
	{"bs", KEY_BACKSPACE}, {"tab", KEY_TAB},
};

static const struct event_map btns_table[] = {
	//btns
	{"btn_l", BTN_LEFT}, {"btn_r", BTN_RIGHT},
	{"btn_m", BTN_MIDDLE},
};

static const struct event_map axis_table[] = {
	{"axis_x", WL_POINTER_AXIS_HORIZONTAL_SCROLL},
	{"axis_y", WL_POINTER_AXIS_VERTICAL_SCROLL},
};


//now we need to have a asci-table, but since you cannot type chars like esc,
//they are dealt in the special_keys_table
#define PUNCT_START 33

//these are the printable keys which you can type
static struct char_map {
	unsigned char ascii;
	int32_t code;
} chars_table[] = {
	//key_resered is not valid, they are for those printable
	//chars which you cannot type thme directly
	//directly start with exclam
	//[33 ~ 47]
	{'!', KEY_RESERVED},
	{'\"', KEY_RESERVED}, {'#', KEY_NUMERIC_POUND},
	{'$', KEY_RESERVED}, {'%', KEY_RESERVED},
	{'&', KEY_RESERVED},
	{'\'', KEY_APOSTROPHE}, //TODO not sure about this one
	{'(', KEY_RESERVED}, {')', KEY_RESERVED},
	{'*', KEY_NUMERIC_STAR}, {'+', KEY_RESERVED},
	{',', KEY_COMMA},
	{'-', KEY_RESERVED}, //special case, you shouldn't see it
	{'.', KEY_DOT}, {'/', KEY_SLASH},
	//numeric keys [48 ~ 37]
	{'0', KEY_0}, {'1', KEY_1}, {'2', KEY_2},
	{'3', KEY_3}, {'4', KEY_4}, {'5', KEY_5},
	{'6', KEY_6}, {'7', KEY_7}, {'8', KEY_8},
	{'9', KEY_9},
	//[58 ~ 64]
	{':', KEY_RESERVED}, {';', KEY_SEMICOLON},
	{'<', KEY_RESERVED}, {'=', KEY_EQUAL},
	{'>', KEY_RESERVED}, {'?', KEY_RESERVED},
	{'@', KEY_RESERVED},
	//upper keys, [65 ~ 90]
	{'A', KEY_RESERVED}, {'B', KEY_RESERVED},
	{'C', KEY_RESERVED}, {'D', KEY_RESERVED},
	{'E', KEY_RESERVED}, {'F', KEY_RESERVED},
	{'G', KEY_RESERVED}, {'H', KEY_RESERVED},
	{'I', KEY_RESERVED}, {'J', KEY_RESERVED},
	{'K', KEY_RESERVED}, {'L', KEY_RESERVED},
	{'M', KEY_RESERVED}, {'N', KEY_RESERVED},
	{'O', KEY_RESERVED}, {'P', KEY_RESERVED},
	{'Q', KEY_RESERVED}, {'R', KEY_RESERVED},
	{'S', KEY_RESERVED}, {'T', KEY_RESERVED},
	{'U', KEY_RESERVED}, {'V', KEY_RESERVED},
	{'W', KEY_RESERVED}, {'X', KEY_RESERVED},
	{'Y', KEY_RESERVED}, {'Z', KEY_RESERVED},
	//signs [91 ~ 96]
	{'[', KEY_LEFTBRACE}, {'\\', KEY_BACKSLASH},
	{']', KEY_RIGHTBRACE}, {'^', KEY_RESERVED},
	{'_', KEY_RESERVED}, {'`', KEY_RESERVED}, //TODO,incorrect
	//lowercase char [92 ~ 122]
	{'a', KEY_A}, {'b', KEY_B}, {'c', KEY_C}, {'d', KEY_D},
	{'e', KEY_E}, {'f', KEY_F}, {'g', KEY_G}, {'h', KEY_H},
	{'i', KEY_I}, {'j', KEY_J}, {'k', KEY_K}, {'l', KEY_L},
	{'m', KEY_M}, {'n', KEY_N}, {'o', KEY_O}, {'p', KEY_P},
	{'q', KEY_Q}, {'r', KEY_R}, {'s', KEY_S}, {'t', KEY_T},
	{'u', KEY_U}, {'v', KEY_V}, {'w', KEY_W}, {'x', KEY_X},
	{'y', KEY_Y}, {'z', KEY_Z},
	//signs [123 ~ 126]
	{'{', KEY_RESERVED}, {'|', KEY_RESERVED},
	{'}', KEY_RESERVED}, {'~', KEY_RESERVED},
};


static inline bool
parse_table(const char *ptr, const struct event_map *table, size_t table_len,
	    uint32_t *code)
{
	for (int i = 0; i < table_len; i++) {
		if (strcasecmp(ptr, table[i].name) == 0) {
			*code = table[i].event_code;
			return true;
			break;
		}
	}
	return false;
}

static inline unsigned int
parse_modifier(const char *str)
{
	for (int i = 0; i < NUMOF(modifiers_map); i++) {
		if (strcmp(modifiers_map[i].name, str) == 0)
			return modifiers_map[i].event_code;
	}
	return 0;
}

static bool
parse_code(const char *code_str, enum tw_binding_type type,
	   struct tw_press *press)
{
	const struct event_map *table = NULL;
	size_t table_len = 0;
	uint32_t code;
	bool parsed = false;
	switch (type) {
	case TW_BINDING_key:
		table = special_keys_table;
		table_len = NUMOF(special_keys_table);
		break;
	case TW_BINDING_btn:
		table = btns_table;
		table_len = NUMOF(btns_table);
		break;
	case TW_BINDING_axis:
		table = axis_table;
		table_len = NUMOF(axis_table);
		break;
	case TW_BINDING_tch:
		break;
	}
	if (type == TW_BINDING_tch) {
		press->tch = (strcasecmp(code_str, "tch") == 0);
		parsed = press->tch;
	} else if (strlen(code_str) > 1) {
		parsed = parse_table(code_str, table, table_len, &code);
		press->keycode = (type == TW_BINDING_key) ? code+8 : code;
	} else if (strlen(code_str) == 1 &&
		   type == TW_BINDING_key &&
		   *code_str >= PUNCT_START &&
		   *code_str < 128) {
		int c = chars_table[*code_str-PUNCT_START].code;
		parsed = (c != KEY_RESERVED);
		press->keycode = c+8;
	} else
		return false;
	return parsed;
}

static bool
parse_one_press(const char *code_str, const enum tw_binding_type type,
		struct tw_press *press)
{
	char str_cpy[128];
	char *toks[5] = {0}, *saved_ptr, *c;
	int count = 0;

	press->modifier = 0;
	press->keycode = 0;
	//deal with case `-asf-`
	strncpy(str_cpy, code_str, sizeof(str_cpy));
	if (*code_str == '-' || *(code_str+strlen(code_str)-1) == '-')
		return false;

	c = strtok_r(str_cpy, "-", &saved_ptr);
	while (c && count < 5) {
		toks[count++] = c;
		c = strtok_r(NULL, "-", &saved_ptr);
	}
	if (count >= 5)
		return false;
	//parse the modifiers, this loop avoids any dups S-S wont pass
	for (int i = 0; i < count-1; i++) {
		uint32_t mod = parse_modifier(toks[i]);
		if ((press->modifier | mod) == press->modifier)
			return false;
		press->modifier |= mod;
	}
	//parse the code
	return parse_code(toks[count-1], type, press);
}

struct validate_press {
	uint32_t mod;
	uint32_t code;
};

//we can support multiple delims, use space, comma, semicolon
//but this shit works now even if it should not "C-x M-a,C-t", no worries, I am worry about it later
static bool
parse_binding(struct taiwins_binding *b, const char *seq_string)
{
	char seq_copy[128];
	strncpy(seq_copy, seq_string, 128);
	char *save_ptr;
	char *c = strtok_r(seq_copy, " ,;", &save_ptr);
	int count = 0;
	bool parsed = true;
	while (c != NULL && count < 5 && parsed) {
		parsed = parsed &&
			parse_one_press(c, b->type, &b->press[count]);
		c = strtok_r(NULL, " ,;", &save_ptr);
		count += (parsed) ? 1 : 0;
	}
	if (count > 5)
		return false;
	if (count < 5)
		b->press[count].keycode = 0;
	return true && parsed;
}


static inline bool
validate_binding(struct taiwins_binding *b, struct validate_press validate[5])
{
	bool passed = true;
	for (int i = 0; i < 5; i++)
		passed = passed && (validate[i].code == b->press[i].keycode &&
				    validate[i].mod == b->press[i].modifier);
	return passed;
}


void
test_binding(void)
{
	const char *sample_bindings[] = {
		"C-x C-a", //valid
		"C-t 1 2 3 4 5", //invalid
		"C-a  absece", //invalid
		"C-x \t", //invalid
		"C-", //invalid
		"C-M-c C-j", //valid
		"S-M-t C-g", //valid
		"C-x 8", //valid
		"C-t 1 2 3 4", //valid
	};
	struct validate_press sbk[9][5] = {
		{{MODIFIER_CTRL, KEY_X+8}, {MODIFIER_CTRL, KEY_A+8}, {0}, {0}, {0}},
		{{MODIFIER_CTRL, KEY_T+8}, {0, KEY_1+8}, {0, KEY_2+8}, {0, KEY_3+8}, {0, KEY_4+8}},
		{{MODIFIER_CTRL, KEY_A+8}, {0}, {0}, {0}, {0}},
		{{MODIFIER_CTRL, KEY_X+8}, {0}, {0}, {0}, {0}},
		{{0}, {0}, {0}, {0}, {0}},
		{{MODIFIER_CTRL | MODIFIER_ALT, KEY_C+8}, {MODIFIER_CTRL, KEY_J+8}, {0}, {0}, {0}},
		{{MODIFIER_SHIFT | MODIFIER_ALT, KEY_T+8}, {MODIFIER_CTRL, KEY_G+8}, {0}, {0}, {0}},
		{{MODIFIER_CTRL, KEY_X+8}, {0, KEY_8+8}, {0}, {0}, {0}},
		{{MODIFIER_CTRL, KEY_T+8}, {0, KEY_1+8}, {0, KEY_2+8}, {0, KEY_3+8}, {0, KEY_4+8}},
	};

	const char *sample_bindings_a[] = {
		"C-axis_x C-axis_y", //valid
		"M-axis_y", //valid
	};
	struct validate_press sba[2][5] = {
		{{MODIFIER_CTRL, WL_POINTER_AXIS_HORIZONTAL_SCROLL},
		 {MODIFIER_CTRL, WL_POINTER_AXIS_VERTICAL_SCROLL}, {0}, {0}, {0}},
		{{MODIFIER_ALT, WL_POINTER_AXIS_VERTICAL_SCROLL}, {0}, {0}, {0}, {0}},
	};

	const char *sample_bindings_b[] = {
		"C-btn_l", //valid
		"C-btn_r M-btn_l", //valid
	};
	struct validate_press sbb[2][5] = {
		{{MODIFIER_CTRL, BTN_LEFT}, {0}, {0}, {0}, {0}},
		{{MODIFIER_CTRL, BTN_RIGHT}, {MODIFIER_ALT, BTN_LEFT}, {0}, {0}, {0}},
	};
	const char *sample_bindings_t[] = {
		"M-tch", //valid
		"M-tch M-tch" //invalid
	};
	struct validate_press sbt[2][5] = {
		{{MODIFIER_ALT, true}, {0}, {0},{0}, {0}},
		{{MODIFIER_ALT, true}, {MODIFIER_ALT, true}, {0},{0}, {0}},
	};

	bool pass;

	for (int i = 0; i < NUMOF(sample_bindings); i++) {
		struct taiwins_binding b = {.type = TW_BINDING_key};
		pass = parse_binding(&b, sample_bindings[i]);
		if (!pass)
			fprintf(stderr, "failed string:%s\n", sample_bindings[i]);
		else
			assert(validate_binding(&b, sbk[i]));
	}

	for (int i = 0; i < NUMOF(sample_bindings_a); i++) {
		struct taiwins_binding b = {.type = TW_BINDING_axis};
		pass =parse_binding(&b, sample_bindings_a[i]);
		if (!pass)
			fprintf(stderr, "failed string:%s\n", sample_bindings_a[i]);
		else
			assert(validate_binding(&b, sba[i]));

	}
	for (int i = 0; i < NUMOF(sample_bindings_b); i++) {
		struct taiwins_binding b = {.type = TW_BINDING_btn};
		pass = parse_binding(&b, sample_bindings_b[i]);
		if (!pass)
			printf("failed string:%s\n", sample_bindings_b[i]);
		else
			assert(validate_binding(&b, sbb[i]));

	}
	for (int i = 0; i < NUMOF(sample_bindings_t); i++) {
		struct taiwins_binding b = {.type = TW_BINDING_tch};
		pass = parse_binding(&b, sample_bindings_t[i]);
		if (!pass)
			printf("failed string:%s\n", sample_bindings_t[i]);
		else
			assert(validate_binding(&b, sbt[i]));
	}
}

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

struct taiwins_config {
	//I find this could be a rather smart design, since you dont really want
	//to expose all the bindings at here, you can declare it, then you can
	//have library to populate it, with its name and, well, why not use a
	//hash table for this
	struct weston_compositor *compositor;
	vector_t bindings;
	//this is the place to store all the lua functions
	vector_t lua_bindings;
	//you will need some names about the keybinding
};


//now this bind_key work. We have to think about the procedure.
//the configurator run once for the given lua state, it should quit the lua state if any error
static int
_lua_bind_key(lua_State *L)
{
	//first argument
	struct taiwins_config *cd = lua_touserdata(L, 1);
	//if the pushed value is a string, we try to find it in our binding
	//table.  otherwise, it should be a lua function. Then we create this
	//binding by its name. Later. you will need functors.

	struct taiwins_binding *binding_to_find = NULL;
	const char *key = NULL;
	//matching string, or lua function
	if (lua_isstring(L, 2)) {
		key = lua_tostring(L, 2);
		//find that binding in the list
		//search in the bindings
	} else if (lua_isfunction(L, 2) && !lua_iscfunction(L, 2)) {
		//we need to find a way to store this function
		//push the value and store it in the register with a different name
		lua_pushvalue(L, 2);
		binding_to_find = vector_newelem(&cd->lua_bindings);
		binding_to_find->type = TW_BINDING_key;
		sprintf(binding_to_find->name, "luabinding:%d", cd->lua_bindings.len);
		lua_setfield(L, LUA_REGISTRYINDEX, binding_to_find->name);
		//now we need to get the binding
	} else {
		//panic??
	}
	//now get the last parameter
	//the last parameter it should has
	const char *binding_seq = lua_tostring(L, 3);
	parse_binding(binding_to_find, binding_seq);
	return 0;
}

int create_test(lua_State *L)
{
	struct taiwins_config *config = lua_newuserdata(L, sizeof(struct taiwins_config));
	printf("%d arguments\n", lua_gettop(L));

	vector_init(&config->bindings, sizeof(struct taiwins_binding), NULL);
	vector_init(&config->lua_bindings, sizeof(struct taiwins_binding), NULL);
	config->compositor = NULL;

	luaL_getmetatable(L, "compositor");
	lua_setmetatable(L, -2);
	printf("%d arguments\n", lua_gettop(L));
	return 1;
}


int main(int argc, char *argv[])
{
	/* test_binding(); */
	//here we do a simple thing, we create a lua global and use
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	//create meta table for this
	lua_pushcfunction(L, create_test);
	lua_setglobal(L, "create_test");

	luaL_newmetatable(L, "compositor");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, _lua_bind_key);
	lua_setfield(L, -2, "bind_key");
	//set the gc

	int error = luaL_loadfile(L, argv[1]);
	if (error)
		printf("shit, errors");
	else
		lua_pcall(L, 0, 0, 0);

	lua_close(L);
	return 0;
}
