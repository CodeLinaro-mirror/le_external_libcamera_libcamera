#!/usr/bin/env bash

# Currently only for ubuntu and debian based distros
apt_get_ext=$(command -v apt-get)
if [ -z ${apt_get_ext} ]; then
	exit
fi

# Help message
function help {
	echo "Usage:"
	echo "./bootstrap-deps.sh [arg1] [arg2] . . ."
	echo ""
	echo "Where argi can be:"
	echo "-all       for all dependencies"
	echo "-cam       for cam"
	echo "-qcam      for qcam"
	echo "-tracing   for tracing with lttng"
	echo "-gstreamer for gstreamer"
	echo "-hotplug   for device hotplug enumeration"
	echo "-docs      for documentation"
	echo "-ipams     for IPA module signing"
	echo "-debug     for improved debugging"
	echo "-android   for android"
	echo "-lcc       for lc-compliance"
}

if [ $# == 1 ] && [ $1 == "-h" ] || [ $1 == "-help" ]; then
	help
	exit
fi

# Dependencies
CORE="libyaml-dev python3-yaml python3-ply python3-jinja2"
IPAMS="libgnutls28-dev"
DEBUG="libdw-dev libunwind-dev"
HOTPLUG="libudev-dev"
DOCS="python3-sphinx doxygen graphviz texlive-latex-extra"
GSTREAMER="libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev"
CAM="libevent-dev libdrm-dev libjpeg-dev libsdl2-dev"
QCAM="qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5 qttools5-dev-tools libtiff-dev"
TRACING="liblttng-ust-dev python3-jinja2 lttng-tools"
ANDROID="libexif-dev libjpeg-dev"
LCCOMP="libevent-dev"

# Meson build system
pip3 install --user meson
pip3 install --user --upgrade meson

# libcamera core
sudo apt-get install -y ${CORE}

# Optional dependencies
for arg in "$@"
do
	case ${arg} in 
		# For all dependencies
		"-all")
			sudo apt-get install -y ${IPAMS} ${DEBUG} ${HOTPLUG} ${DOCS} ${GSTREAMER} ${CAM} ${QCAM} ${TRACING} ${ANDROID} ${LCCOMP}	
		;;
		# For cam
		"-cam")
			sudo apt-get install -y ${CAM}
		;;
		# For qcam
		"-qcam")
			sudo apt-get install -y ${QCAM}
		;;	
		# For tracing 
		"-tracing")
			sudo apt-get install -y ${TRACING}
		;;
		# For gstreamer
		"-gstreamer")
			sudo apt-get install -y ${GSTREAMER}
		;;
		# For hotplug 
		"-hotplug")
			sudo apt-get install -y ${HOTPLUG}
		;;
		# For documentation
		"-docs")
			sudo apt-get install -y ${DOCS}
		;;
		# For IPA module signing
		"-ipams")
			sudo apt-get install -y ${IPAMS}
		;;
		# For improved debugging
		"-debug")
			sudo apt-get install -y ${DEBUG}
		;;
		# For android
		"-android")
			sudo apt-get install -y ${ANDROID}
		;;
		# For lc-compliance
		"-lcc")
			sudo apt-get install -y ${LCCOMP}
		;;
	esac	
done
