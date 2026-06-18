#!/bin/bash

for port in /dev/ttyACM*; do
  pio run -t upload --upload-port "$port"
done
for port in /dev/ttyACM*; do pio run -t upload --upload-port $port; done