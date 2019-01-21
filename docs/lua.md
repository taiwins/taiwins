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


### nuklear bindings
This part you have no choice but to provide


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
