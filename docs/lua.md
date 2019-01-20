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
