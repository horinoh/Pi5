#!/bin/bash

rm -rf build
meson setup build
meson configure build
