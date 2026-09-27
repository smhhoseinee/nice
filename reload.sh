#!/bin/bash

MODULE_NAME="tcp_nice"
MODULE_FILE="${MODULE_NAME}.ko"

# Step 1: Check if the module is already loaded
if lsmod | grep -q "$MODULE_NAME"; then
    echo "Module $MODULE_NAME is already loaded. Unloading it first..."
    sudo rmmod "$MODULE_NAME"
    if [ $? -ne 0 ]; then
        echo "Failed to unload $MODULE_NAME. Exiting."
        exit 1
    fi
fi

# Step 2: Compile the kernel module
echo "Compiling the kernel module..."
make clean && make module
if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

# Step 3: Insert the kernel module
echo "Loading the kernel module..."
sudo insmod "$MODULE_FILE"
sudo dmesg -C

# Step 4: Check if the module is loaded
if lsmod | grep -q "$MODULE_NAME"; then
    echo "OK"
else
    echo "FAIL"
fi


