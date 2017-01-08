JBus Documentation {#mainpage}
==============================

JBus functions as a server for accepting connections from GBA emulator clients.
The jbus::Listener class may be constructed and [started](@ref jbus::Listener::start)
to enque incoming jbus::Endpoint instances.

Once an Endpoint has been [accepted](@ref jbus::Listener::accept), it's ready to use.
Refer to the jbus::Endpoint class for the main interface.
