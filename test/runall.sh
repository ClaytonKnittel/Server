#!/bin/bash
if [[ $PWD == */test ]]
then
    # if in test directory, cd out to root
    cd ..
fi
find test/* -type f ! -name "*.*" -exec {} \;
