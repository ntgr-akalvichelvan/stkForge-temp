#!/bin/bash

PLATFORM=$1     #M4350 M4300 M4250_LK M4250_IM
AGENT=$2  #M4250_IM_agent_appmgr_1.0.5.27_devicemgr_2.2.13.25.tar.gz
IMAGE=$3  #M4250H-v13.0.5.21.stk
NEW_IMAGE_VER="$4" #"13.0.5.23"

WORK_DIR=$5 				#/home/vspl007/Downloads/Management_switch_Package/ImagePacking
SUP_DIR=/home/swnuc04/arun/stkForge-temp/ImagePacking
UTIL_DIR=$SUP_DIR/utilities		#/home/vspl007/Downloads/Management_switch_Package/ImagePacking/utilities
BIN_DIR=$WORK_DIR	#$PLATFORM
SIGN_DIR=$SUP_DIR/SigningKeys

NEW_IMAGE=$(echo "$IMAGE" | sed -E "s/(v)[0-9.]+(\.stk)/\1$NEW_IMAGE_VER\2/")     #M4250H-v$NEW_IMAGE_VER.stk
STRIP_IMG="$(echo $IMAGE | cut -d'.' -f1-4)-strip.stk"

export PATH=$PATH:$UTIL_DIR

echo
echo "Creating tmp_$PLATFORM & copy the tools & binaries"
echo "=================================================="

mkdir -p $WORK_DIR/tmp_$PLATFORM
cp -rf $SUP_DIR/tools $WORK_DIR/tmp_$PLATFORM/.
cp -rf $BIN_DIR/$IMAGE $WORK_DIR/tmp_$PLATFORM/.
cp -rf $BIN_DIR/$AGENT $WORK_DIR/tmp_$PLATFORM/.

echo
echo "[PROGRESS] 10"
echo

echo $PATH

cd $WORK_DIR/tmp_$PLATFORM/

echo
echo "Untar the Agent File: $AGENT"
echo "===================="
if file "$AGENT" | grep -q gzip; then
    tar -xvzf "$AGENT"
else
    tar -xvf "$AGENT"
fi

echo
echo "[PROGRESS] 25"
echo

echo
echo "Remove Certificate from Existing Image"
echo "======================================"
$UTIL_DIR/remove_certificate.sh $IMAGE

echo
echo "[PROGRESS] 80"
echo

echo $PATH
echo
echo "Pack the agent into the image"
echo "================================"
$UTIL_DIR/agent_integrate.sh $STRIP_IMG $NEW_IMAGE_VER 

echo
echo "Sign the image"
echo "=============="
$UTIL_DIR/image_sign.sh $NEW_IMAGE $SIGN_DIR

echo
echo "[PROGRESS] 90"
echo

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

echo
echo "[PROGRESS] 100"
echo
