# XVMSocket
This is a fork of https://github.com/skranjac/XVMSocket to run on current Linux kernel and Xen (tested Linux 4.4 with Xen 4.6). Being our submission for a lab project, it is not production ready.

## Requirements
* Linux >= 4.4
* Xen >= 4.6
* xenstored running (preferably on dom0)

## Addressing
Our fork uses a new addressing scheme with a service string, which the server can bind and clients connect to, e.g. "database" or "proxy". See files in `test3` for examples.
