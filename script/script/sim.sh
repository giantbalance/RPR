#!/bin/bash

REPORT="./sim_report.txt"
rm $REPORT

for((i=32;i<=8192;i=i*2))
do
	echo "Size = $i"
	echo "Size = $i" >> $REPORT 
	./sim alifo $i ../pin/water_ns/water_ns.trace >> $REPORT
done

#./sim alifo 128 ../pin/water_ns/water_ns.trace > sim_report
#./sim alifo 256 ../pin/water_ns/water_ns.trace >> sim_report
#./sim alifo 512 ../pin/water_ns/water_ns.trace >> sim_report
#./sim alifo 1024 ../pin/water_ns/water_ns.trace >> sim_report
