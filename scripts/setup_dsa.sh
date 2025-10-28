#!/bin/bash

set -u

wq_mode=dedicated

function init_dsa {
	local did=$1

	echo "Setting DSA$did..."
	sudo accel-config config-device dsa$did
	
	sudo accel-config config-engine dsa$did/engine$did.0 --group-id=$did
	sudo accel-config config-engine dsa$did/engine$did.1 --group-id=$did
	sudo accel-config config-engine dsa$did/engine$did.2 --group-id=$did
	sudo accel-config config-engine dsa$did/engine$did.3 --group-id=$did
	
	for i in {0..7}; do
		if [ "$wq_mode" = "dedicated" ]; then
			mode_flag="--mode=dedicated"
		elif [ "$wq_mode" = "shared" ]; then
			mode_flag="--mode=shared --threshold=16"
		else
			echo "Invalid WQ mode"
			return 1
		fi
		sudo accel-config config-wq dsa$did/wq$did.$i \
		--group-id=$did --type=kernel --driver-name=dmaengine \
		$mode_flag --wq-size=16 --priority=10 --name=dma$did$i
	done
	
	sudo accel-config enable-device dsa$did
	
	for i in {0..7}; do
		sudo accel-config enable-wq dsa$did/wq$did.$i
	done
}

function reset_dsa {
	local did=$1

	for i in {0..7}; do
		sudo accel-config disable-wq dsa$did/wq$did.$i 2> /dev/null && echo Diesabled dsa$did/wq$did.$i
	done

	sudo accel-config disable-device dsa$did && echo Disabled dsa$did
}

num_dsa=`ls /sys/bus/dsa/devices/ | grep dsa | wc -l`

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 [init|reset]"
elif [ "$1" = "init" ]; then
	for ((did = 0; did < $num_dsa; did++)); do
		init_dsa $did
	done
elif [ "$1" = "reset" ]; then
	for ((did = 0; did < $num_dsa; did++)); do
		reset_dsa $did
	done
else
	echo "Usage: $0 [init|reset]"
fi
