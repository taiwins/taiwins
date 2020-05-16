# Lua bindings

Taiwins is currently configed through lua language. refer to
[config](config.lua) as an example.

## here are some knowledge about lua c API.

You work with a stack when dealing with lua C code, it is annoying sometimes, as
it kinda like writing assembly, but it is what it is.

### registry
	For c programmer, you have registry `LUA_REGISTRYINDEX` to store your data, 
	call `lua_setfile(L, LUA_REGISTRYINDEX, pos)` to set the data.

### metatables, *__index* and *__newindex*
	- `metatable` serves the purpose of operator overrides.
	- `__index` is for element access.
	- `__newindex` is for elements assignment. 
	
	using metatables provides much more syntax sugar for configurations. If you
    decide to override the `__index` and `__newindex` for metatable, you can't
    assign elements into tables after setting metatables anymore. So register
    methods *BEFORE* assigning metatables.
	
	If you have a global table, placing methods inside the table directly
    instead of inside the metatable would be a good idea. 

### To call a lua fuction
- `lua_getglobal` : at 0
- `lua_pushstring` : at -1. first argument
- `lua_newtable` : at -2. second argument
- `lua_pushtablestring` operate on table
- `lua_call` : clear the stack, call the function then push results onto stack
  for you to retrieve.

### to create a lua global variable
- `lua_push*` : you have some thing on the stack.
- `lua_setglobal` : pop that value and sets it as global variable in the lua
  context

### to get something in the table
For example, you can create a new table then set it as global, then to get the
field.

- `lua_pushstring` : push the name of that field.
- `lua_gettable(L, index)` : use what we pushed onto stack to get the
  field. `index` is usually just -2.
- access the value by the index of -1
- `lua_pop`, pop that value.

### to set something in the table
- `lua_newtable` : create the table
- `lua_pushstring` : the name of the field.
- `lua_push*` : push a actual value for that field.
- `lua_settable` : the index should be -3, points to the table.
- `lua_setglobal` : set our table name and pop it from the stack.
