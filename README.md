All In One Script for building Lichee Pi Nano Linux Image
=======================
Based on: https://github.com/xiaoxiaohuixxh/lichee-nano-one-key-package

It builds a system with support for the following
- SPI0
- SPI1 (untested)
- I2C0
- USB Mass Storage (with automount)
- USB Audio

The default password for root is `clever`

## To install:
1. git clone https://github.com/jockm/lichee-nano-one-key-package 
2. cd lichee-nano-one-key-package
3. sudo chmod +x ./build.sh 
4. ./build.sh pull_all
5. ./build.sh nano_tf

If everything is successful you will find a `.img` file in the `output/image` directory

## To add additional files to the image:
Add a script nameed `post-buildroot.sh` in the parent directory.  It is passed the Buildroot output directory as the first argument.  So to add a file to the `/usr/bin` directory:

    cp $1/MyAdditionalFiles/MyExecutable $OUTPUTDIR/target/usr/bin

## NOTES: 
- Tested on Ubuntu 16.04 and 18.04 only
- The script assumes you are on a x86 or x64 Linux system
