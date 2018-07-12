# The wayland-server-protocols
## event-loop
	`event_loop` is like what I have in the `client.c`, it is almost implemented
	in the same way, it has the `timer`, `idle`, `fd` (It uses `pipe2` for
	receiving data instead of `inotify`).
## wl_client_create
	This is the only time you need to explicitly create a client for the given
	socket, otherwise the clients are created by wayland protocols(I think the
	socket is created when the first client created(if you look into the
	`wl_client_create` code)). Since you are forced to give a `fd`, the only way
	is providing a `socketpair`.

## call `wayland_socket_create_auto`
