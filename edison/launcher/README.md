Edison Launcher
===============

The Edison launcher is a service that will constantly run on the Intel Edison
on the Signpost.

It services RPC requests that modules sends to the storage master and keeps
the Edison asleep in the absence of requests.

Upon receiving a request it will launch the request (which is simply a starting
a new process), monitor it, and go to sleep when it is done. Eventually
it will also work with the controller main MCU to account for processes
running and energy used while they are running.

We hope to eventually service remote requests as well (for instance a pointer
to a git repo which can be fetched and executed.

Once processes are launched, they should use the [Edison Client API]
(https://github.com/lab11/signpsost-software/tree/master/signbus/python) to communicate
with signpost modules and access signpost storage.
