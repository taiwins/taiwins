Taiwins input grab system
=========================

Input grab is a way for modifying input behaviors. The name "grab" came from
the X11 world. Takes the XGrabPointer_ for example, The functions allows user
to override the default behaviors to functions like ``button``, ``motion`` and
``axis``. The default behaviors in wayland is similar forwarding those events
to clients. By "grabbing" the pointer, we are able to implement some functions
like resizing (by overriding ``motion`` event) and moving the windows. We use
**grabs** in taiwins extensively, libtaiwins already offers grab like
``tw_popup_grab``. ``tw_data_drag_grab``, etc. The taiwins compositor uses it
to implement the keybinding sytem.

The old approach:
-----------------

**Previously** taiwins implements a mutex like grab system. Everyone can try to
grab the input devices, but only succeed when there is no competetion. It is
really simple but works swiftly in many scenarios, it is because all or our
grabs are the *short-living* grabs. They only exist for a short period of time
so the racing condition not likely to occur.

I am rather happy until we try to implement the input-method. Unlike the
previous grabs, this one is a *long-living* grab. If you start using IME on an
application, it could last really long, now the racing condition starts to
appear. When you want to switch applications or workspaces or resizing the
window, the ``input-method-grab`` is block us from doing it. What is worse is
when we have two *long-living* grabs. We could maneuver the grab system by
*backing up* and *restore*, but implement such mechanism for everytime is
tedious, we might as well switch the a new sytem.

The new grab system:
--------------------

After a few trials, I decided a robust grab system need to have:

1. Only one grab occupies the input device at a time.

2. Does not start the same grab twice.

3. Be able to *backup* and *restore* the grab for later use.

This sounds like a stack but there is the catch, what if we started a
**keybinding** grab, before we can finish, a **IME grab** occupies the
keyboard? We faces a awkward situation:

- We cannot finish the keybinding.
   
- We cannot start another keybinding since the keybinding grab already there.

What you would prefer in such situation? You would want to *backup* the **IME
grab** instead, when the keybinding finishes. *restore* the **IME grab**.

For implementing such system, we need to have a **priority** in grabs. In our
example, the **keybinding** grab is high priority grab, **IME** grab is low
priority one. This complex our grab system quite a bit, instead of simply set
the grab on keyboard, we need to

1. Find the right place to insert grab in ``keyboard->grabs``.

2. Set the ``current_grab`` to the right one.

There is also a API change for starting the grab, now we start the grab by
``tw_keyboard_start_grab(tw_keyboard *, tw_seat_keyboard_grab *, int
priority)``.
   
.. _XGrabPointer: https://tronche.com/gui/x/xlib/input/XGrabPointer.html
