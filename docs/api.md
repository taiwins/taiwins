# name conversion
## `create` and  `destroy`
when using create destroy, par exemple, `wl_shm_pool_create` and
`wl_shm_pool_destroy`, we are getting a pointer then free the pointer in the
end.

The `struct` is usually not visible, declared in header, defined in source
code. Good example is `nk_egl_backend`, since there could be only one
implementation of it.

## `init` and `end`
In this case, user has the control over the memory, but the interface controls
the heap if any, after calling `end`, the struct should returns to the init
state.

## `init` and `release`
Same as above.
