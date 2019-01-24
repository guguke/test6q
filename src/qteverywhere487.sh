#!/bin/sh

./configure \
 -prefix /usr/local/freescale/qt4.8.7 \
 -opensource \
 -confirm-license \
 -release -shared \
 -embedded arm \
 -xplatform qws/linux-arm-g++ \
 -depths 16,18,24,32 \
 -fast \
 -optimized-qmake \
 -little-endian -host-little-endian \
 -pch \
 -qt-sql-sqlite \
 -qt-libjpeg \
 -qt-zlib \
 -qt-libpng \
 -qt-libmng \
 -qt-libtiff \
 -qt-freetype \
 -declarative \
 -xmlpatterns \
 -webkit \
 -multimedia \
 -make tools \
 -nomake examples -nomake demos \
 -exceptions \
 -no-openssl \
 -no-glib \
 -no-qt3support \
 -no-libtiff -no-libmng \
 -no-mmx -no-sse -no-sse2 \
 -no-3dnow \
 -no-qvfb \
 -no-phonon \
 -no-nis \
 -no-opengl \
 -no-cups \
 -no-separate-debug-info \
 -nomake docs \
 -no-mouse-tslib \
 -no-mouse-linuxinput \
 -qt-kbd-linuxinput \
 -qt-gfx-transformed \
 -qt-gfx-linuxfb

 #-nomake examples -nomake tools -nomake docs \
 #-no-xcursor -no-xfixes -no-xrandr -no-xrender \

#make -j8

#make install -j8

#exit
