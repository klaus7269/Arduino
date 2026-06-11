# Display registers on startup

For each board we simply copied the output of `read_registers.ino` from the examples directory. All this does is give us the contents of some common registers as the display just wakes up.

&nbsp;

## Cheap Yellow Display variants

### ESP32-2432S028

The original CYD 2.8" display on ILI9341
resistive touch
micro-USB

```
RDDID (0x04): seq: 00 00 00 - idx: 00 00 00 
RDDST (0x09): seq: 00 61 00 00 - idx: 00 00 00 00 
0x0A (0x0a): 08
0x0B (0x0b): 00
0x0C (0x0c): 06
0x0D (0x0d): 00
0x0E (0x0e): 00
0x0F (0x0f): 00
ID4 (0xd3): seq: 00 00 00 00 - idx: 41 00 93 41 
RDID1 (0xda): 00
RDID2 (0xdb): 00
RDID3 (0xdc): 00
```

### Guition JC2432W328

2.8" display on ST7789
capacitive touch
USB-C

```
RDDID (0x04): seq: 81 81 b3 - idx: 40 40 40 
RDDST (0x09): seq: 00 61 00 00 - idx: 00 00 00 00 
0x0A (0x0a): 08
0x0B (0x0b): 00
0x0C (0x0c): 06
0x0D (0x0d): 00
0x0E (0x0e): 00
0x0F (0x0f): 00
ID4 (0xd3): seq: 00 00 00 00 - idx: 00 00 00 00 
RDID1 (0xda): 81
RDID2 (0xdb): 81
RDID3 (0xdc): b3
```

### Guition ESP32-2432S024

2.4" display on ILI9341
capacitive touch
USB-C

```
RDDID (0x04): seq: 00 00 00 - idx: 00 00 00 
RDDST (0x09): seq: 00 61 00 00 - idx: 00 00 00 00 
0x0A (0x0a): 08
0x0B (0x0b): 00
0x0C (0x0c): 06
0x0D (0x0d): 00
0x0E (0x0e): 00
0x0F (0x0f): 00
ID4 (0xd3): seq: 00 00 00 00 - idx: 41 00 93 41 
RDID1 (0xda): 00
RDID2 (0xdb): 00
RDID3 (0xdc): 00
```

### CYD2USB

2.8" display on ST7789
capacitive touch
USB-C and micro-USB

```
RDDID (0x04): seq: 85 85 52 - idx: 42 42 42 
RDDST (0x09): seq: 00 61 00 00 - idx: 00 00 00 00 
0x0A (0x0a): 08
0x0B (0x0b): 00
0x0C (0x0c): 06
0x0D (0x0d): 00
0x0E (0x0e): 00
0x0F (0x0f): 00
ID4 (0xd3): seq: 00 00 00 00 - idx: 00 00 00 00 
RDID1 (0xda): 85
RDID2 (0xdb): 85
RDID3 (0xdc): 52
```
