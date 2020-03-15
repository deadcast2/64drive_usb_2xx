
64drive USB loader
_____________________________________________________________________

(c) 2014-2018 Retroactive
Code is BSD-licensed.


Usage
-----

   -l <file> [bank] [addr]        Load binary to bank
   -d <file> <bank> <addr> <len>  Dump binary from bank
               1 - Cartridge ROM              2 - SRAM 256kbit
               3 - SRAM 768kbit               4 - FlashRAM 1Mbit
               5 - FlashRAM 1Mbit (PokeStdm2) 6 - EEPROM 16kbit

   -s <int>                       Set save emulation type
               0 - None (default)       1 - EEPROM 4k
               2 - EEPROM 16k           3 - SRAM 256kbit
               4 - FlashRAM 1Mbit       5 - SRAM 768kbit (Dezaemon 3D)
               6 - FlashRAM 1Mbit (PokeStdm2)
   -c <int>                       Set CIC emulation type (RevB/HW2 only)
               0 - 6101 (NTSC)          1 - 6102 (NTSC)
               2 - 7101 (PAL)           3 - 7102 (PAL)
               4 - x103 (All)           5 - x105 (All)
               6 - x106 (All)           7 - 5101 (NTSC)
   -x                             Standalone mode
   -z                             Debug server mode
   -b <file.rpk>                  Update bootloader
   -f <file.rpk>                  Update firmware
   -v                             Verbose output for debug

1. Arguments: <required>, [optional]
2. Address and size values are hexadecimal and byte-addressed.
3. Any bank can be loaded at any time. However, some banks overlap
   each other and so only one save bank should be used at once.
4. Save banks can be loaded/read regardless of what the 64drive has 
   been instructed to emulate.
5. Refer to the 64drive Hardware Specification for more information.
6. Use the verbose flag when updating firmware or bootloader to see
   detailed revision information about that update.
7. Debug server allows communication directly from PC (host) to N64
   (target). Firmware 2.05+ required
8. Standalone mode is exclusively for use with UltraSave, it is used
   for interacting with physically connected carts and accessories
9. About CICs:
   Nintendo confusingly named the first few CICs before settling on 
   a standardization for the later ones.
     6101 - NTSC, only used in Starfox
     6102 - NTSC, used in virtually all games
     7101 - PAL,  same as 6102 but different region
     7102 - PAL,  only used in Lylat Wars
     x103 - All,  some first-party games
     x105 - All,  Zelda, many Rareware games
     x106 - All,  handful of first-party games
   The bootcode used by the game image (0x40-0xFFF) is "signed" by
   a secret number cooked into its associated CIC.
   So, you will need to specify the correct CIC to match the 
   bootcode - which itself is not region-affiliated.
   You can also use the "friendly" name, i.e. "6102" instead of the
   number index as the parameter.
10. If the image is bigger than 64mbyte, the loader will check to see
   if you have a HW2 unit and enable a special extended address mode
   which gains you 240mbyte of cartridge space. In this mode, the CI
   registers accessible by the N64 are relocated to make room.




Building
--------
I use Visual Studio 2013. You will need:
 - ftd2xx.h
 - ftd2xx.lib
Both are available within FTDI's D2xx driver package, which is a
self-extracting EXE. Open with 7zip or Winrar and pull these files.

If you just want to run the program, the EXE is in /Release/.






Release History
----------------------------------------------------------------------

Dec 10 2018
-----------
 - Added CI extended address mode support for firmware 2.06+


Dec 27 2017
-----------
 - Added debug server, block types 0x01 (printf) and 0xD0 (remote
   control example) are implemented as examples.


Oct 08 2016
-----------
 - Added manual CIC support for 64drive HW2
 - Some standalone bugs may still exist


Jul 17 2016
-----------
 - Added support for 64drive HW2
 - Fixed some bugs in standalone mode 


Mar 12 2016
-----------
 - Add support for UltraSave standalone mode
 - Fixed bug where USB device wouldn't be found on many systems


Dec 16 2014
-----------
 - Initial release
 - Ground-up rewrite of the PC-side code
 - Supports device firmwares 2.xx only
 - Devices running the older 1.xx firmwares must use the older USB 
   loader to upgrade. This newer code can only perform upgrades 
   within the 2.xx series firmwares.
 - Support for chaining multiple operations on the command line. 


EOF