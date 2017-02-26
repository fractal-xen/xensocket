XenSocket (XVMSocket) - Prototype version for Xen 3.0.3

XenSocket (XVMSocket) is joint work with Xiaolan Zhang, John Griffin, and Pankaj Rohatgi.

Our Xen Summit 2007 presentation can be found here: http://www-archive.xenproject.org/files/xensummit_4/SuzanneMcIntosh_XenSummit_2007.pdf

Our Middleware 2007 paper is available here: http://link.springer.com/chapter/10.1007%2F978-3-540-76778-7_10

XenSocket provides a sockets-based interface for interdomain 
communication in Xen. XenSocket uses shared memory for message passing
rather than the well-utilized method of using Xen communication rings
and page flipping.

XenSocket files in top-level directory, xensocket:
xensocket.h - XenSocket header file
xensocket.c - XenSocket source code that compiles into kernel module
license.txt
Makefile
ReadMe

XenSocket test files in directory xensocket/test:
sender.c   - Sender source code
receiver.c - Receiver source code
Makefile

------------------------------------------------------------------------
Compiling XenSocket:

XenSocket must be compiled on a system that has Xen 3.0.3 installed 
because it needs access to several Xen header files. XenSocket has been 
developed and tested with Xen 3.0.3 and Linux 2.6.16.18-1.8. We use the
gcc compiler.

To compile: Change to the top-level xensocket directory and type "make".

XenSocket compiles into a kernel module named xensocket.ko.  To install 
the module in a Linux-based domain, type "insmod ./xensocket.ko".

------------------------------------------------------------------------
Compiling and running XenSocket test code:
There are sample programs in the /test directory that implement a 
sender (called "client" in the XenSocket source code) and receiver 
("server").  These can be compiled by typing "make" in that directory. 

The receiver test code must be started before the sender test code. Start
the sender test code once the receiver has started up successfully and is
in the traffic state. Note that the xensocket.ko kernel module must be 
loaded in each domain prior to starting any test code. Loading of the 
xensocket.ko module can happen in any order at any time before the test 
code is started.  The same kernel module (xensocket.ko) is used whether 
a domain is to be a sender, receiver, or both.

To start the receiver, type:  receiver <domid of sender>
The receiver then prints the gref value, which you'll need when you start
the sender.

Start the sender in its domain by typing: sender <domid of receiver> <gref>

The following lists the routines invoked during a typical test scenario.
The receiver calls:
1. socket(), which causes xensocket:xen_create() to be invoked.
2. bind(), which causes xensocket:xen_bind() to be invoked.
3. [multiple times] recv() which causes xensocket:xen_recvmsg()
   to be invoked.
4. [eventually] shutdown(), which causes xensocket:xen_release() to be 
   invoked.

The sender calls:
1. socket(), which causes xensocket:xen_create() to be invoked.
2. connect(), which causes xensocket:xen_connect() to be invoked.
3. [multiple times] send() which causes xensocket:xen_sendmsg() to be 
   invoked.
4. [eventually] shutdown(), which causes xensocket:xen_release() to be 
   invoked.

For manual shutdown/abort, the sender should be shutdown first.

