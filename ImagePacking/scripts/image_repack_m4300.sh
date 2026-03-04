#!/bin/bash

PLATFORM=M4300
AGENT=M4300_agent_appmgr_1.0.5.27_devicemgr_2.2.13.25.tar.gz
IMAGE=m4300v12.0.19.16.stk
NEW_IMAGE_VER="12.0.19.18"

UTIL_DIR=/home/nivethak/ImagePacking/utilities
WORK_DIR=/home/nivethak/ImagePacking
BIN_DIR=$WORK_DIR/$PLATFORM
SIGN_DIR=$WORK_DIR/SigningKeys

NEW_IMAGE=m4300v$NEW_IMAGE_VER.stk

export PATH=$PATH:$UTIL_DIR

echo
echo "Creating tmp_$PLATFORM & copy the tools & binaries"
echo "=================================================="

mkdir -p $WORK_DIR/tmp_$PLATFORM
cp -rf $WORK_DIR/tools $WORK_DIR/tmp_$PLATFORM/.
cp -rf $BIN_DIR/$IMAGE $WORK_DIR/tmp_$PLATFORM/.
cp -rf $BIN_DIR/$AGENT $WORK_DIR/tmp_$PLATFORM/.


cd $WORK_DIR/tmp_$PLATFORM/

echo
echo "Untar the Agent File: $AGENT"
echo "===================="
tar -xvzf $AGENT

echo
echo "Pack the agent into the image"
echo "================================"
$UTIL_DIR/agent_integrate.sh $IMAGE $NEW_IMAGE_VER 

echo
echo "Sign the image"
echo "=============="
$UTIL_DIR/image_sign.sh $NEW_IMAGE $SIGN_DIR

echo
echo "Copy the image to binaries location $BIN_DIR under output"
echo "========================================================="
mkdir -p $BIN_DIR/output
cp $NEW_IMAGE $BIN_DIR/output/.

echo
echo "CleanUp tmp_$PLATFORM"
echo "====================="
echo
rm -rf $WORK_DIR/tmp_$PLATFORM
