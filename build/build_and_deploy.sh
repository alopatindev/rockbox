#!/bin/bash

set -e

make -j
make zip
echo A | unzip rockbox.zip -d /media/CLIP_PLUS/
sudo umount /media/CLIP_PLUS
echo 'DONE! Eject your player.'
