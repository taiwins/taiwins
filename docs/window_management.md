# Taiwins layout engine.
This is the document discussing about taiwins window managment method. Right now
we have only floating and tiling method. Here we mostly dicuss the tiling method.

### tree-based tiling window manager provides following operations:

- **creating/removing new window**: at current level. The levels are represented by internal
  nodes, when you create new window, it inserts a new window under that level,
  defaultly inserting on the right.

- **resizing**: This is probably the most complex operations, you need to
  modifier the parent container.

- **level up/down**: This is the same as removing window current window then
  reinsert into parents level.

- **change order for windows in current level** This only affects current
  container.

- **moving surface to different workspace**

### And following modes:

horizental/vertical/monocle.

### With that in-mind, desktop needs to offer the following operations:

- creating/removing new window

- resizing by keys or cursor.

- changing window order.

- a tiling window specific, move one window up or down.

- focus windows by click or keys:
  focusing by cursor is simple, the user experience it is not very obvious to do
  it by keys. You may have to do it differently for *floating views* and *tiling
  views*. If I want to switch among last focused views. I will have to create a
  another view-list to keep track of current views. focus by click will be
  difficult(since you need to search the list)


- fullscreen(we need a specific WESTON_LAYER_POSITION_FULLSCREEN for that).

- minimizing, by moving it to the hidden layer(and do I lose frames in mpv)?

- moving surface to different workspace, this is general


## What is better than a tree-based tiling window manager?
What can we do even more?

- When you create new view, whether insert it in the floating layer or tiling
  layer?

- At what size and scale?

- At what position?

- If in the tiling window layer, how big it is? Do I need to change size of
  other views?
