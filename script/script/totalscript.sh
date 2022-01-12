#!/bin/bash

APP="./apprun.sh"
SIM="./simrun.sh"
LOG="app.log"
LIST=("cholesky" "fft" "lu_cb" "lu_ncb" "ocean_cp" "ocean_ncp" "radiosity" "raidx" "raytrace" "volrend" "water_nsquared" "water_spatial" )

echo "LOG START" > $LOG

for arr_item in ${LIST[*]}
do
	echo $arr_item
	eval $APP $arr_item >> $LOG 
	eval $SIM $arr_item >> $LOG
	rm $arr_item.out
done

