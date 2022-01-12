# POSE tools

There are three tools included.
This document describes each tool briefly;
for more information, refer to README in each directory.

### pin (posetrace)
A memory tracing tool that records the sequence of memory addresses
referenced by a program.

### vis
A tool that visualizes the memory trace extraced by `pin`.

### sim
A tool that simulates multiple page replacement algorithms using
a memory trace extraced by `pin`.

### script
script 
apprun.sh : run specific workload and save trace
simrun.sh : run specific trace and calculate miss rate
allrun.sh : run all workload and save trace
totalscript.sh : run both apprun.sh and simrun.sh
simrun_rl.sh : run specific trace and calculate miss rate 100 times


