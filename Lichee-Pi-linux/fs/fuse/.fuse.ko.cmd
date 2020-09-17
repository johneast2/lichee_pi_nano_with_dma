cmd_fs/fuse/fuse.ko := arm-linux-gnueabi-ld -r  -EL -T ./scripts/module-common.lds  --build-id  -o fs/fuse/fuse.ko fs/fuse/fuse.o fs/fuse/fuse.mod.o ;  true
