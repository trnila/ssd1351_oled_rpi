# SSD1351 display driver for linux userspace

- enable larger spidev buffers by appending `spidev.bufsiz=32768` to `/boot/cmdline.txt`
- video generation
```
$ ffmpeg -i input.webm -vf scale=128x128 -f rawvideo -pix_fmt bgr565be output.raw
```
