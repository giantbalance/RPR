#!/bin/bash

#Description :

#Runs the benchmark with native, multiprocessor 1

#Argument : [TARGET]

TARGET="$1"
PARSECDIR="/home/dcslab/parsec-3.0"
PARSECPLAT="amd64-linux.gcc"
POSEDIR="/home/dcslab/.local/bin/posetrace"
NUMPROCS="1"
#Determine Target
case "${TARGET}" in
	"cholesky" )
		PROGARGS="-p${NUMPROCS} < ${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/tk29.O"
		PROG="${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"fft" )
		 PROGARGS="-m20 -p${NUMPROCS}"
		 PROG="${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"lu_cb" )
		 PROGARGS="-p$NUMPROCS -n2048 -b16"
		 PROG="${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		 ;;
	"lu_ncb" )
		 PROGARGS="-p$NUMPROCS -n2048 -b16"
		 PROG="${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
        "radix" )
	         PROGARGS="-p${NUMPROCS} -r4096 -n67108864 -m2147483647"
                 PROG="${PARSECDIR}/ext/splash2x/kernels/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"ocean_cp" )
		 PROGARGS="-n2050 -p$NUMPROCS -e1e-07 -r20000 -t28800"
		 PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		 ;;
	 "ocean_ncp" )
		PROGARGS="-n2050 -p$NUMPROCS -e1e-07 -r20000 -t28800"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"radiosity" )
	        PROGARGS="-bf 1.5e-1 -batch -room -p ${NUMPROCS}"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"raytrace" )
		PROGARGS="-s -p$NUMPROCS -a128 ${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/car.env"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"volrend" )
		PROGARGS="${NUMPROCS} head 1000"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"water_nsquared" )
		PROGARGS="< ${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/input_1"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	"water_spatial" )
		PROGARGS="< ${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/input_1"
		PROG="${PARSECDIR}/ext/splash2x/apps/${TARGET}/inst/${PARSECPLAT}/bin/${TARGET}"
		;;
	*)
		echo "Target Error"
		exit 1;;
esac

#Execution
RUN="$POSEDIR $PROG $PROGARGS"
echo "Running $RUN:"
eval $RUN
mv ./posetrace.out $TARGET.out
