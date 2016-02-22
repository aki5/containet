#!/bin/bash

apt-get -y update || exit 1
apt-get -y install curl module-init-tools || exit 1

version=$(head -n 1 /proc/driver/nvidia/version | awk '{print $8}')
filename="NVIDIA-Linux-x86_64-${version}.run"
[ ! -f "$filename" ] && curl -o "${filename}" "http://us.download.nvidia.com/XFree86/Linux-x86_64/${version}/${filename}"
bash ${filename} -a -N --ui=none --no-kernel-module
