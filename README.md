# Thread-Local-Storage

An implementation of Thread Local Storage in C using the standard pthread library. Also compatible with my own thread library implementation. 

Every thread can have one thread local storage, which is either their own created storage, or the cloned storage of another thread. 

Copy-on-write cloning is implemented such that cloned storages point to the same piece of memory until they are written to.

Running "make" will create a tls.o file. This project was created for EC440.
