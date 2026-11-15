#!/bin/bash

#Pico2 Build
cmake -B pico2_debug   -S . -DCMAKE_BUILD_TYPE=Debug   -DPICO_BOARD=pico2_w
cmake -B pico2_release -S . -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico2_w