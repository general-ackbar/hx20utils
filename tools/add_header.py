import wave
import sys

if len(sys.argv) == 1:
    print("Usage: add_header.py <infile> <outfile>")
    exit(1)

infile  = sys.argv[1]
outfile = sys.argv[2] 
print("Input: ", infile);
print("Output: ", outfile);
samplerate = 11025  # fast værdi for HX-20 signal
channels = 1        # mono
samplewidth = 1     # 1 byte = 8-bit unsigned PCM

# load raw bytes
with open(infile, "rb") as f:
    raw = f.read()

# wrap in WAV header
with wave.open(outfile, "wb") as w:
    w.setnchannels(channels)
    w.setsampwidth(samplewidth)
    w.setframerate(samplerate)
    w.writeframes(raw)

print("Done:", outfile)

