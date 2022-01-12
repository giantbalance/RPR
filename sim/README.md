# sim

sim is a tool that simulates page replacement policies using memory traces
extracted by `posetrace`.
Given memory size and the trace file, sim outputs the miss rate / ratio of
a page replacement policy (the number of cold misses are not counted).
This is the core tool for the project POSE.


## Architecture
`sim.c` and `sim.h` is the frontend module that interacts with
memory trace files (e.g., reading and parsing), delivers information to
backend page replacement modules (contained in `policy/`), and reports
the simulations result.
`policy/` contains the modules that implement various page replacement
algorithms.
`lib/` contains useful libraries that are used in the implementation of page
replacement modules.


## Implemented page replacement algorithms
- LRU: `lru.c, lru.h`
- FIFO: `fifo.c, fifo.h`
- [OPT](https://ieeexplore.ieee.org/abstract/document/5388441): `opt.c, opt.h`
- [CLOCK](https://multicians.org/paging-experiment.pdf): `clock.c, clock.h`
- [SEQ](https://dl.acm.org/doi/abs/10.1145/258623.258681): `seq.c, seq.h`
- [CLOCK-Pro](https://dl.acm.org/doi/10.5555/1247360.1247395): `clock-pro.c, clock-pro.h`
- APR (Our work!): `alifo.c, alifo.h`


## To build
```
$ make
```


## How to use
```
$ ./sim <policy> <memory size (kB)> <trace file> [-v] [-s] [-d] [-r]
```
For example,
```
$ ./sim lru 4096 fft.trace
```
