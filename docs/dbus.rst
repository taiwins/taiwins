.. _dbus.rst:

D-Bus API Quick Reference
=========================

D-Bus Quick Intro
-----------------

D-Bus is a protocol, part of the freedesktop project, it is designed easier IPC
on linux. It is a beautiful idea thus that for applications want to call
functions in another program, they do not need to link against that service,
they can simply issue a call on the dbus instead. D-Bus acts like Mediator
pattern. DBus has implementations in many programming languages.

A DBus service firstly has a **destination name** which looks like this:
*org.freedesktop.DBus*\ , then under this name, there are the **object paths**
like */org/freedesktop/DBus*\ , then through the object path, we get to
**interface** for its methods, properties and signals. We demostrate the usage
through the ``dbus-send`` utility.

.. code-block::

     dbus-send --session --type=method_call --print-reply
         --dest=org.gnome.Rhythmbox
         /org/gnome/Rhythmbox/Player
         org.freedesktop.DBus.Introspectable.Introspect

This command sends a message to Rhythmbox for getting its available
methods. ``org.freedesktop.DBus.Introspectable.Introspect`` is the convention
method to query the available methods in an interface.

APIs
----

There are two main dbus APIs, the "low level dbus c api" and the "systemd bus"
API, the first one is more portable (available on freebsd, OSX through homebrew
as well). Nevertheless, the api is not easy to use, that is why I created the
tdbus (tiny dbus) project for easier usage.

In general, we touch the most the three parts of the API, ``dbus_connection_*``\ ,
``dbus_message_*`` and ``dbus_signature_*``.

DBus Connection APIs
^^^^^^^^^^^^^^^^^^^^

``DBusConnection``\ , as its name pronounced, maintains a connection to the DBus
daemon. To work with it, DBus API provides 3 major routines to work with it;
read, write and dispatch. The most simple case would be simply calling
``dbus_connection_send_with_reply_and_block``\ , it does all three things. But
usually a software with a mainloop which polls on fds, DBus provides callbacks
for ``DBusWatch``\ , ``DBusTimeout`` and ``DBusDispatchFunction``. For instance, we get
the fd for a ``DBusWatch`` by ``dbus_watch_get_unix_fd``\ ; for Timeout, one can use
timerfd for creating pollable fds. Note that ``DBusConnection`` also has a
``dbus_connection_get_unix_fd``\ , but it is not pollable. For monitoring
``DBusConnection``\ , we need to create a fake ``eventfd`` then call
``wl_event_source_check`` for dispatching ``DBusConnection``. 

DBus Message APIs
^^^^^^^^^^^^^^^^^

The messaging API is a subsystem itself. There is two concepts, ``DBusMessage``
and ``DBusSignature``. Remember when we said dbus allows you to call functions?
The signature here is the function argument
`types <https://dbus.freedesktop.org/doc/dbus-specification.html#idm702>`_. 

Those two provides iterators and sub-iterators, we call ``dbus_message_iter_next``
and ``dbus_signature_next`` to aquire next element, ``dbus_signature_iter_recurse``
and ``dbus_message_iter_recurse`` for get into sub-types. We can also validate the
signature by ``dbus_signature_validate`` or ``dbus_signature_validate_single``. Work
with iterators is not particular easy, thus dbus has a shorthand for basic
types. 

.. code-block::

   dbus_message_append_args (message,
                             DBUS_TYPE_INT32, &v_INT32,
                             DBUS_TYPE_STRING, &v_STRING,
                             DBUS_TYPE_INVALID);

For writing basic arguments,

.. code-block::

   dbus_message_get_args (message,
                          DBUS_TYPE_INT32, &v_INT32,
                          DBUS_TYPE_STRING, &v_STRING,
                          DBUS_TYPE_INVALID);

Server
^^^^^^

Creating a DBusServer needs a little additional work. Firstly we need to call
``dbus_bus_request_name`` to DBus Daemon to see if the service name is taken,
then all we need to do is simply fill up the ``DBusObjectPathVTable`` for
answering the method calls and unregister handler which can be simply blank.

How to answer the method calls is totally the freedom of the server, except
there are two mandatary methods, namely ``Introspect`` (quering methods) and ``Get``
(quering properties) for any usable dbus service.
