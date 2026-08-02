#!/bin/bash

# Create output directory
mkdir -p processed

# Loop over all mp3 files in current directory
for f in *.mp3; do
    if [ "$f" = "*.mp3" ]; then
        echo "No mp3 files found"
        exit 1
    fi
    echo "Processing $f..."
    # Apply lowpass to muffle, highpass to remove sub-bass, and mix pink noise
    # We use anoisesrc to generate pink noise at low volume (0.03)
    # amix will mix the audio and noise. duration=first means it stops when the first input (the music) stops.
    ffmpeg -y -v warning -i "$f" -filter_complex "anoisesrc=c=pink:a=0.03[noise]; [0:a]lowpass=f=2500,highpass=f=150,volume=0.9[audio]; [audio][noise]amix=inputs=2:duration=first" "processed/$f"
done

echo "Processing complete."
