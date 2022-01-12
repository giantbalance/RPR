#!/bin/bash

APP="./apprun.sh"
SIM="./simrun.sh"
TARGET="$1"

eval $APP $TARGET
eval $SIM $TARGET
