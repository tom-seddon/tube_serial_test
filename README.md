Test for doubled-up bytes when doing Acorn->PC transfer with a Tube
Serial widget. Initially noted on the Electron/AP5 but then also noted
on the BBC B: https://stardot.org.uk/forums/viewtopic.php?f=3&t=27093

How to use:

1. Get `TUTEST` (spooled BBC BASIC output) into the BBC Micro somehow

2. Run `tube_serial_test recv COMn`, where `COMn` is the COM port of
   interest. It should tell you it's waiting for input
   
3. Run `TUTEST` on the BBC Micro, and select test of interest (see
   below)

4. `tube_serial_test` should ideally go quiet, but any transmission
   errors will be displayed as it runs

# ACORN->PC

All the ACORN->PC tests send sequential byte values to the PC, and the
PC reports any duplicated values. For example:

    Errors: 0x63: 1/320 0x67: 13/320 0x70: 178/320 0x71: 252/320 0x72: 253/320 0x73: 285/320 0x74: 299/320 0x75: 319/320 0x76: 319/320 0x77: 320/320 0xb0: 316/320 0xb1: 320/320 0xb2: 319/320 0xb3: 320/320 0xb4: 320/320 0xb5: 320/320 0xb6: 320/320 0xb7: 320/320 0xc7: 1/320 0xd0: 302/320 0xd1: 318/320 0xd2: 317/320 0xd3: 320/320 0xd4: 320/320 0xd5: 320/320 0xd6: 320/320 0xd7: 320/320 0xe3: 20/320 0xe5: 13/320 0xe6: 5/320 0xe7: 100/320 0xf0: 320/320 0xf1: 320/320 0xf2: 320/320 0xf3: 320/320 0xf4: 320/320 0xf5: 320/320 0xf6: 320/320 0xf7: 320/320
	
This means value 0x63 was received 320 times, and was duplicated once;
0x67 was received 320 times, and duplicated 13 times - and so on.

While it's running, you can press Space to get error stats or `q` to
quit.

Interesting tests are probably:

## `3. ACORN->PC DATA 1*256 (6502)`

Sends bytes, reading them from a table using (zp),y, roughly matching
the equivalent send loop in the BeebLink ROM.

There are 256 FIFO writes, one per value written, so duplicated bytes
printed on the PC can be matched to a particular address. Find the
address of the instruction that writes N by doing `PRINT
!(UNPS%+N*4)`.

## `5. ACORN->PC Y 1*256 (6502)`

As test 3, but no table. Just sends the Y register value instead.

Again, there are 256 FIFO writes, one per value written, and a table
at `UNPS%`.

## `6. ACORN->PC SINGLE ADDR (6502)`

Sends bytes, always writing the FIFO from the same address. The address in the code produces 100% failures on my BBC B!

Adjust the address in `PROCA2P_2_6502`.
