#!/bin/bash
# Install packages needed by LTIB（首先要先安装aptitude ： sudo apt-get install aptitude）
apt-get install aptitude
aptitude -y install gettext libgtk2.0-dev rpm bison m4 libfreetype6-dev
aptitude -y install libdbus-glib-1 -dev liborbit2-dev intltool
aptitude -y install ccache ncurses-dev zlib1g zlib1g-dev gcc g++ libtool
aptitude -y install uuid-dev liblzo2-dev
aptitude -y install tcl dpkg
aptitude -y install asciidoc texlive-latex-base dblatex xutils-dev
apt-get install texinfo
# Packages required for 64-bit Ubuntu
# Do "uname -a" and see if the word "x86_64" shows up.
if uname -a|grep -sq 'x86_64'; then
aptitude -y install ia32-libs libc6-dev-i386 lib32z1
fi
# The following recommended for Linux development.
# They are not required by LTIB.
aptitude -y install gparted emacs22-nox openssh-server
aptitude -y install nfs-common nfs-kernel-server lintian
aptitude -y install git-core git-doc git-email git-gui gitk
aptitude -y install diffstat indent tofrodos fakeroot doxygen uboot-mkimage
aptitude -y install sendmail mailutils meld atftpd sharutils
aptitude -y install manpages-dev manpages-posix manpages-posix-dev linux-doc
aptitude -y install vnc4server xvnc4viewer
