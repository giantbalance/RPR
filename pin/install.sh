#!/bin/bash

BINDIR=/home/$USER/.local/bin
LIBDIR=/home/$USER/.local/lib/posetrace
BIN=$BINDIR/posetrace
PIN=$(realpath pin-3.11-97998-g7ecce2dac-gcc-linux/pin)
POSETRACE=$(realpath pin-3.11-97998-g7ecce2dac-gcc-linux/source/tools/posetrace/obj-intel64/posetrace.so)

mkdir -p $BINDIR
mkdir -p $LIBDIR

cp pin-3.11-97998-g7ecce2dac-gcc-linux/source/tools/posetrace/obj-intel64/posetrace.so $LIBDIR

echo "#!/bin/bash" > $BIN
echo "CMD=\"\$@\"" >> $BIN
echo "$PIN -t $LIBDIR/posetrace.so -- \$CMD" >> $BIN

chmod +x $BIN
