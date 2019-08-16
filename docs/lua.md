# Lua bindings

Now you know you have chosen the lua binding, you know there are basically two
things in mind. The configure setup and nuklear bindings.


### configurations
you can simply set it up like a table, then query the table entries in the c
code. Another way, you can do a `request(configurator)`, and use a provided lua
api to work.

How should I say, if you want to do things like providing lua functions as
callback to use in the system, then you have to provide api to use in api.
Then all you have to do in the c side is ~lua_loadfile~ :-p.

For this part we want to have a global function to retrieve all the handles for
lua.

	local configurator = get_taiwins_configureator()

some code likes this. Afterwards you can simply have

	configurator.bind_close_taiwins("C-xC-c")
	configurator.bind(lua_function, "C-cC-f")
	configurator.set_theme("dark")

We can also have `user_data` which binds with it. So we can get the `user_data`
instead of save it somewhere as global variable.

### metatables, `__index` and `__newindex`
	- `metatable` serves the purpose of evoking function (e.g. `__add` for the
	  function that is not there.)
	- `__index` method is there when you access an element of table that is not
	  there.
	- `metatable` of the userdata is for verifring whether certain userdata is
	  the certain type.
	- set `__index` of a `metatable` to itself.

### nuklear bindings
This part you have no choice but to provide functions.

lua script has two functions:
`run_label` and `run_binding`. The lua functions are called by corresponding c
functions. They does two things.

- create `user_data` as a opaque data that contains the nk_context. Then use
  `setmetatable` so later we can use `user_data` in the c functions. You also
  need to create gc for metatables.


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

### so we can create the table as global.
