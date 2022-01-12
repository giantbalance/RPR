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

apprun.sh : run specific workload and save trace.
ex : apprun.sh radix

simrun.sh : run specific trace and calculate miss rate.
if not mentioned, run all policies
ex : simrun.sh radix alifo
ex : simrun.sh radix 

allrun.sh : run both app and sim
ex : allrun.sh radix

totalscript.sh : run both apprun.sh and simrun.sh

simrun_rl.sh : run specific trace and calculate miss rate 100 times
ex: simrun_rl.sh radix alifo


