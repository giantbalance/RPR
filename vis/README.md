# vis

vis is a tool that visualizes the trace extraced by `posetrace`.
It visualizes the global access pattern with all memory addresses.
In addition, it visualzies local access pattern per object;
an object is the memory address range allocated by memory allocation functions
included in the trace.

## How to use
```
$ ./vis.py <trace file>
```
For example,
```
$ ./vis.py fft.trace
```
In the example above, you can find the directory `fft/` containing
the png files each exhibits the access pattern of an object or the global access pattern.
