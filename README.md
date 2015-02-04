# SMC-commandline
Improved version of the command line tool to manipulate OS X System Management Control (SMC) settings,
in particular fan speeds.

Many applications designed to control fan speeds on a Mac OS X system include a command line program
called "smc". This can be used to show and alter kernel variables associated with the AppleSMC kernel
extension. Here is the help information shown by the program:

$ ./smc -h
Apple System Management Control (SMC) tool 0.01
Usage:
./smc [options]
    -f         : fan info decoded
    -h         : help
    -k <key>   : key to manipulate
    -l         : list all keys and values
    -r         : read the value of a key
    -w <value> : write the specified value to a key
    -v         : version

Unfortunately this program is a mess of errors.

For example, the '-l' option may show something like this:

$ ./smc -l
  #KEY  [ui32]  20 (bytes 00 00 01 14)
  $Adr  [ui32]  0 (bytes 00 00 03 00)
  $Num  [ui8 ]  1 (bytes 01)
  +LKS  [flag]  (bytes 07)
  ALSC  [{alc]  (bytes 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00)
  AUPO  [ui8 ]  0 (bytes 00)
  BATP  [flag]  (bytes 00)
  BEMB  [flag]  (bytes 00)
  BNum  [ui8 ]  0 (bytes 00)
  BSIn  [hex_]  (bytes 42)
  CLKT  [ui32]  58 (bytes 00 01 05 3a)
  CLSD  [ui16]  0 (bytes 00 00)
  CLWK  [ui16]  84 (bytes 00 54)
  CRCB  [ui32]  166 (bytes a3 da 51 a6)
  CRCU  [ui32]  218 (bytes 10 c9 44 da)
  DPLM  [{lim]  (bytes 00 00 00)
  EPCA  [ui32]  0 (bytes 00 00 70 00)
  EPCF  [flag]  (bytes 01)
  EPCI  [ui32]  0 (bytes 04 f0 07 00)
  EPCV  [ui16]  1 (bytes 00 01)

Note how the first key, called "#KEY", gives the number of keys. The bytes given show a hexadecimal value
of 0x0114, equal to 276. But the decimal representation is only 20. Indeed, only 20 keys are listed by 
the program. As the list is in alphabetical order, it is obviously too short.

This is only the first of many problems with this program.

Here we hope to correct all these problems and make an error-free version which is backward compatible
with the widely distributed one.

NB: Details about the AppleSMC kernel extension are very hard to come by. We can only make educated
guesses about the following:
- The meaning of each of the keys (such as #KEY, CLKT, F0ID)
- The meaning of the data types. 'ui32', 'ui16', 'ui8' and 'flag' are obvious enough, but what about 'fpe2'
  (guess: fixed point value with a 2 bit fraction)? And all the others, such as '{fds'?
- The meaning of the value for each key. E.g. key "F0Mn [fpe2]" gives the minimum speed of fan 0 in revolutions
  per minute. The "revolutions per minute" part does not follow directly from the meaning of the key or the
  type of the data.

