#!/bin/bash
rsync -cvrP --delete   ./ pbl@47.100.242.24:/home/pbl/MXT2105-MP/
#--exclude-from="./.gitignore" 
