Frida Scripts
=============

[Frida](https://frida.re/) is like a scriptable dtrace on steroids.

Claude and I used frida to sniff out the serial protocol that the SPDSXPROAPP speaks to the SPD-SX Pro device.

Example:

```sh
$ frida \
   -f /Applications/Roland/SPDSXPROAPP.app/Contents/MacOS/SPDSXPROAPP \
   -l dt1log.js
```
