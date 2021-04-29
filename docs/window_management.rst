
Taiwins layout engine.
======================

This is the document discussing about taiwins window managment method. We
implements several common layouts in taiwins. They behaves differently on users
interactions.

tree-based tiling layout
^^^^^^^^^^^^^^^^^^^^^^^^

1. **creating/removing new window** : at current level. The levels are
   represented by internal nodes, when you create new window, it inserts a new
   window under that level, defaultly inserting on the right.

2. **resizing** : This is probably the most complex operations, you need to
   modifier the parent container.

3. **level up/down** : This is the same as removing window current window then
   reinsert into parents level.
  
4. **switching virtical/horizental**: re-orgnaize current container horizontally
   or veritically.
  
5. **change order for windows in current level** This only affects current
   container.

6. **moving surface to different workspace**

floating layout
^^^^^^^^^^^^^^^
1. **creating/removing new window** : center the surface on the screen.

2. **moving and resizing** : only affects itself.


maximized layout
^^^^^^^^^^^^^^^^
1. **creating/removing new window** : takes over available desktop space(do not
   occupy ui elements).

fullscreen layout
^^^^^^^^^^^^^^^^^
1. **creating/removing new window** : takes over the entire screen.

Layout changing
===============
In taiwins desktop, windows are organized in layers. The windows on the top
layers will occludes the windows in the layers below. By changing the layout,
windows are moved between layers.
