#!/bin/bash

#Description : Runs Simulation with different Size and Policy

#Argument : [TARGET]

TARGET="$1"
POLICY="$2"
SIMDIR="/home/dcslab/git_rpr/rpr/sim/sim"
INPUTDIR="/home/dcslab/APR/pose-tools-master/script"
SIZE_cholesky=("4000" "6250" "8500" "10750" "13000" "15250" "17500" "19750" "22000" "24250" "26500" "28750" "31000" "33250" "35500" "37750" "40000" )

SIZE_fft=("4200" "6250" "8500" "10750" "13000" "15250" "17500" "19750" "22000" "24250" "26500" "28750" "31000" "33250" "35500" "37750" "40000" )
SIZE_lu_cb=("300" "400" "500" "600" "700" "800" "900" "1000" "1100" "1200" "1300" "1400" "1500" "1600" "1700" "1800" "1900" )
SIZE_lu_ncb=("17000" "18125" "19250" "20375" "21500" "22625" "23750" "24875" "26000" "27125" "28250" "29375" "30500" "31625" "32750" "33875" "35000" )
SIZE_ocean_cp=("4000" "4700" "5400" "6100" "6800" "7500" "8200" "8900" "9600" "10300" "11000" "11700" "12400" "13100" "13800" "14500" "15200" )
SIZE_ocean_ncp=("12000" "14000" "16000" "18000" "20000" "22000" "24000" "26000" "28000" "30000" "32000" "34000" "36000" "38000" "40000" "42000" "44000" )
SIZE_radiosity=("7000" "10937" "14874" "18811" "22748" "26685" "30622" "34559" "38496" "42433" "46370" "50307" "54244" "58181" "62118" "66055" "69992" )
SIZE_radix=("2500" "2600" "2700" "2800" "2900" "3000" "3100" "3200" "3300" "3400" "3500" "3600" "3700" "3800" "3900" "4000" "4100" )
SIZE_raytrace=("2000" "2080" "2160" "2240" "2320" "2400" "2480" "2560" "2640" "2720" "2800" "2880" "2960" "3040" "3120" "3200" "3280" )
SIZE_volrend=("200" "240" "280" "320" "360" "400" "440" "480" "520" "560" "600" "640" "680" "720" "760" "800" "840" )
SIZE_water_nsquared=("200" "281" "362" "443" "524" "605" "686" "767" "848" "929" "1010" "1091" "1172" "1253" "1334" "1415" "1496" )
SIZE_water_spatial=("200" "281" "362" "443" "524" "605" "686" "767" "848" "929" "1010" "1091" "1172" "1253" "1334" "1415" "1496" )

case "${TARGET}" in
	"cholesky" )
		SIZE=${SIZE_cholesky[*]}
		;;
	"fft" )
		SIZE=${SIZE_fft[*]}
		;;
	"lu_cb" )
		SIZE=${SIZE_lu_cb[*]};;
	"lu_ncb" )
		SIZE=${SIZE_lu_ncb[*]};;
	"radix" )
		SIZE=${SIZE_radix[*]};;
	"ocean_cp" )
		SIZE=${SIZE_ocean_cp[*]};;
	"ocean_ncp" )
		SIZE=${SIZE_ocean_ncp[*]};;
	"radiosity" )
		SIZE=${SIZE_radiosity[*]};;
	"raytrace" )
		SIZE=${SIZE_raytrace[*]};;
	"volrend" )
		SIZE=${SIZE_volrend[*]};;
	"water_nsquared" )
		SIZE=${SIZE_water_nsquared[*]};;
	"water_spatial" )
		SIZE=${SIZE_water_spatial[*]};;
	*)
		echo "Target Error!"
		exit 1;;
esac


echo "$SIZE"

INPUT=$INPUTDIR/$TARGET.out
DATE=$(date '+%d%m%Y_%H%M%S')
echo "$DATE"
echo "$POLICY[*]"
echo "$NAME"

LOG=$INPUTDIR/$TARGET.$NAME.$DATE.log
touch $LOG
for pol_item in ${POLICY[*]}
do
	for size_item in ${SIZE[*]}
	do
		RUN="$SIMDIR $pol_item $size_item $INPUT"
		echo "$RUN" | tee -a $LOG
		eval $RUN | tee -a $LOG
	done
done


