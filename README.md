# Kernel Modules
## Overview
This is an OS project to make a kernel module that creates a hierarchical process tree structure with memory allocation demonstrations. It showcases kernel thread creation, memory management, and visualization of process memory segments in the Linux kernel environment.
## Features
* Creates a multi-level process tree with parent-child relationships (up to 4 levels deep)
* Dynamically allocates different-sized memory blocks for each process
* Provides detailed memory segment information for each process (heap, stack, code, data)
* Visualizes the entire process structure in kernel logs with proper indentation
* Cleans up resources properly on module unload
