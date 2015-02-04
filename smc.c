/*
 *  smc.c
 *  Smc
 *
 *  Created by Erik Groenhuis on 3-2-2013.
 *  Copyright 2013 Vertus Publications. All rights reserved.
 *
 */

/*
 * Apple System Management Control (SMC) Tool 
 * Copyright (C) 2006 devnull 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

#include "smc.h"

io_connect_t conn;

/*
 * AAARG!!
 *
 * Private function definitions starting with an underscore :-((
 *
 * Rename to 'mystrtoul()' or something
 */

/*
 * Appears to:
 * - convert the series of bytes in str to a 32 bit (4 byte) integer
 * - str[0] becomes the most significant byte (biggest shift)
 * - str[3] becomes the least significant byte (least shift)
 * Apparent distinction between base=16 and others is meaningless
 * Seems to be geared to size==4, but less also works
 * For size>4 the UInt32 simply overflows, so that part fails gracefully.
 * 'size' must not be greater than the number of bytes available in the string.
 * Ergo: works well for any size (<4, ==4, >4). Result is always a 32 bit int.
 * - Does *not* convert from hex or dec to unsigned long, so the name is deceptive
 * - By adding values to an int this method is independent on the byte order of the internal representation.
 * ==
 * Renamed from _strtoul() to bytes2uint32() to better reflect the function
 * Dropped the 'base' parameter. No base conversion takes place, all it does
 * is move bytes around.
 * Now works correctly, especially in the cases where it was called with base=10.
 *
 * Convert an array of bytes to a UInt32.
 * - bytes[0] is seen as the most significant byte in the array.
 * - The 'size' (values 1-4) gives the number of bytes read from 'bytes'.
 * The conversion is independent of the byte order ("endian-ness") of the system; bytes[0] will
 * always be the most significant part of the result.
 */
UInt32 bytes2uint32(char *bytes, int size)
{
    UInt32 total = 0;
    int i;
    
    for (i = 0; i < size; i++)
    {
            total += bytes[i] << (size - 1 - i) * 8;
    }
    return total;
}

/*
 * Appears to:
 * - convert the bytes in 'val' to a string of bytes in 'str'
 * - MSB becomes the first character
 * - LSB becomes the last character
 * Input 'val' is limited to 32 bits (4 bytes), so output will never be more than 4 characters
 *
 *  == Rename to 'uint32tobytes
 */
void _ultostr(char *str, UInt32 val)
{
    str[0] = '\0'; // Redundant. sprintf() does not require a terminated string as first parameter
    sprintf(str, "%c%c%c%c", 
            (unsigned int) val >> 24,
            (unsigned int) val >> 16,
            (unsigned int) val >> 8,
            (unsigned int) val);
}

/*
 * Appears to:
 * - convert bytes in 'str' to a float
 * - 'size' is the number of bytes taken from 'str'
 * - '8-e' is the number of bits to take from each byte in 'str'
 * - fail completely: for e>0 bits of str[] will overlap
 * Was the intention to shift the whole series of bytes to the right by 'e' bits
 * That would make the input a fixed point representation. Everything after
 * the fixed point is still dropped.
 * - lucks out for 2-byte input. For that case there is no overlap in the bits
 *
 *  == Rename to 'bytes2float'
 */
float _strtof(char *str, int size, int e)
{
    float total = 0;
    int i;
    
    for (i = 0; i < size; i++)
    {
        if (i == (size - 1))
            // MSB in 'str' is special.
            // Already a byte, it is reduced by 'e' bits and added to the low part of 'total'
            total += (str[i] & 0xff) >> e;
            // Strange cutoff to 0xff. str[i] is already a char
        else
            // Other bytes in 'str':
            total += str[i] << (size - 1 - i) * (8 - e);
    }
    
    return total;
}

void printFPE2(SMCVal_t val)
{
    /* FIXME: This decode is incomplete, last 2 bits are dropped */
    /* No shit, Einstein!. Your _strtof() is broken */
    
    printf("%.0f ", _strtof(val.bytes, val.dataSize, 2));
}

/*
 * Print the value of an SMCVal_t (which has an integer type of 8, 16 or 32 bits)
 * as a decimal integer value
 */
void printUInt(SMCVal_t val)
{
    /*
     * == Problem fixed: bytes 01 14 were returned as '20', should be 276
     * Solved by replacing "home brew" _strtoul() (with 'base' parameter) by
     * bytes2uint32() (without a 'base' parameter).
     */
    printf("%u ", (unsigned int) bytes2uint32(val.bytes, val.dataSize));
}

/*
 * Print the value of an SMCVal_t as a sequence of bytes, in hexadecimal
 * - number of bytes is in val.dataSize
 * - bytes are in val.bytes[]
 */
void printBytesHex(SMCVal_t val)
{
    int i;
    
    printf("(bytes");
    for (i = 0; i < val.dataSize; i++)
        printf(" %02x", (unsigned char) val.bytes[i]);
    printf(")\n");
}

/*
 * Print the information contained in an SMCVal_t
 */
void printVal(SMCVal_t val)
{
    // - two spaces
    // - 4 characters for the name of the key
    // - 4 characters for the datatype of the key
    printf("  %-4s  [%-4s]  ", val.key, val.dataType);

    // print the value only if the dataSize is bigger than zero
    if (val.dataSize > 0)
    {
        // For integer dataTypes use printUInt()
        // For fpe2 dataType use printFPE2()
        // For others print nothing
        if ((strcmp(val.dataType, DATATYPE_UINT8) == 0)  ||
            (strcmp(val.dataType, DATATYPE_UINT16) == 0) ||
            (strcmp(val.dataType, DATATYPE_UINT32) == 0)
           )
            printUInt(val);
        else if (strcmp(val.dataType, DATATYPE_FPE2) == 0)
            printFPE2(val);
        
        // Print the byte values in hexadecimal
        printBytesHex(val);
    }
    else
    {
        printf("no data\n");
    }
}

kern_return_t SMCOpen(io_connect_t *conn)
{
    kern_return_t result;
    mach_port_t   masterPort;
    io_iterator_t iterator;
    io_object_t   device;
    
    result = IOMasterPort(MACH_PORT_NULL, &masterPort);
    
    CFMutableDictionaryRef matchingDictionary = IOServiceMatching("AppleSMC");
    result = IOServiceGetMatchingServices(masterPort, matchingDictionary, &iterator);
    if (result != kIOReturnSuccess)
    {
        printf("Error: IOServiceGetMatchingServices() = %08x\n", result);
        return 1;
    }
    
    device = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    if (device == 0)
    {
        printf("Error: no SMC found\n");
        return 1;
    }
    
    result = IOServiceOpen(device, mach_task_self(), 0, conn);
    IOObjectRelease(device);
    if (result != kIOReturnSuccess)
    {
        printf("Error: IOServiceOpen() = %08x\n", result);
        return 1;
    }
    
    return kIOReturnSuccess;
}

kern_return_t SMCClose(io_connect_t conn)
{
    return IOServiceClose(conn);
}


/*
 * Exchange data with the kernel extension (SMC device in the AppleSMC extension)
 * All reading and writing of data in this program goes through this call
 * - 'index' is passed to the kernel extension as the command to execute
 * - 'inputStructure' contains data passed to the kernel extension
 * - 'outputStructure' will contain data returned from the kernel extension
 * - global variable 'conn' is used as the connection to use
 */
kern_return_t SMCCall(int index, SMCKeyData_t *inputStructure, SMCKeyData_t *outputStructure)
{
    size_t  structureInputSize;
    size_t  structureOutputSize;
    
    structureInputSize = sizeof(SMCKeyData_t);
    structureOutputSize = sizeof(SMCKeyData_t);

    // IOConnectMethodStructureIStructureO is depricated and no longer available
    // See https://developer.apple.com/library/mac/samplecode/SimpleUserClient/Listings/SimpleUserClientInterface_c.html
    // for an example of a replacement.
    
    // For completeness there should be a pre-10.5 version of the code here, with #ifdef
    // switches for compilation to different targets.
    /*
     From IOKitLib.h:
     
     kern_return_t
     IOConnectCallStructMethod(
        mach_port_t	 connection,		// In
        uint32_t	 selector,          // In
        const void	*inputStruct,		// In
        size_t		 inputStructCnt,	// In
        void		*outputStruct,		// Out
        size_t		*outputStructCnt)	// In/Out
     AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER;

     */
    return IOConnectCallStructMethod(
                                        conn,
                                        index,
                                        inputStructure,
                                        structureInputSize,
                                        outputStructure,
                                        &structureOutputSize
                                    );
}

/*
 * Read the specific SMC value for a given key
 */
kern_return_t SMCReadKey(UInt32Char_t key, SMCVal_t *val)
{
    kern_return_t result;
    SMCKeyData_t  inputStructure;
    SMCKeyData_t  outputStructure;

    // Initialise all values to zero
    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    memset(val, 0, sizeof(SMCVal_t));

    // Convert 4 bytes in 'key' to an Int32, with key[0] as MSB
    inputStructure.key = bytes2uint32(key, 4);

    // Also: print the Int32 as a string of characters into val->key ??
    // Is this intended as a simple string copy?
    strcpy(val->key, key);

    // Put the command to read info about the key in the inputStructure
    inputStructure.data8 = SMC_CMD_READ_KEYINFO;
 
    // SMCCall with only the key filled in will fill outputStructure with:
    // - dataSize
    // - dataType
    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;  // Quit if call fails
    
    // Remember the dataSize
    val->dataSize = outputStructure.keyInfo.dataSize;
    
    // Convert the UInt32 dataType to string of bytes in 'val'
    _ultostr(val->dataType, outputStructure.keyInfo.dataType);

    // Set up inputStructure to read the actual value
    inputStructure.keyInfo.dataSize = val->dataSize;      /** WARNING: accepts any data size. Danger of array overflow **/

    // Put the command to read value of the key in the inputStructure
    inputStructure.data8 = SMC_CMD_READ_BYTES;
    
    // Read the value of the key
    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;  // Quit if call fails
    
    // Bluntly copy bytes from outputStructure to 'val'
    memcpy(val->bytes, outputStructure.bytes, sizeof(outputStructure.bytes));
    
    return kIOReturnSuccess;
}

kern_return_t SMCWriteKey(SMCVal_t writeVal)
{
    kern_return_t result;
    SMCKeyData_t  inputStructure;
    SMCKeyData_t  outputStructure;
    
    SMCVal_t      readVal;
    
    result = SMCReadKey(writeVal.key, &readVal);
    if (result != kIOReturnSuccess) 
        return result;
    
    if (readVal.dataSize != writeVal.dataSize)
        return kIOReturnError;
    
    memset(&inputStructure, 0, sizeof(SMCKeyData_t));
    memset(&outputStructure, 0, sizeof(SMCKeyData_t));
    
    inputStructure.key = bytes2uint32(writeVal.key, 4);
    inputStructure.data8 = SMC_CMD_WRITE_BYTES;    
    inputStructure.keyInfo.dataSize = writeVal.dataSize;
    memcpy(inputStructure.bytes, writeVal.bytes, sizeof(writeVal.bytes));
    
    result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
    if (result != kIOReturnSuccess)
        return result;
    
    return kIOReturnSuccess;
}


/*
 * Find the total number of keys in SMC
 * - Use the special key "#KEY" to get the number
 * - Convert to an int
 */
UInt32 SMCReadIndexCount(void)
{
    SMCVal_t val;
    
    SMCReadKey("#KEY", &val);
    return bytes2uint32(val.bytes, val.dataSize);
}

/*
 * Print all SMC values
 */
kern_return_t SMCPrintAll(void)
{
    kern_return_t result;
    SMCKeyData_t  inputStructure;
    SMCKeyData_t  outputStructure;
    
    int           totalKeys, i;
    UInt32Char_t  key;
    SMCVal_t      val;
    
    // Find the total number of keys in SMC
    totalKeys = SMCReadIndexCount();
    
    // Iterate through all of the keys
    for (i = 0; i < totalKeys; i++)
    {
        // Clear all the data
        memset(&inputStructure, 0, sizeof(SMCKeyData_t));
        memset(&outputStructure, 0, sizeof(SMCKeyData_t));
        memset(&val, 0, sizeof(SMCVal_t));
        
        inputStructure.data8 = SMC_CMD_READ_INDEX;  // Set command to read
        inputStructure.data32 = i;                  // Set key index number

        // Get the key name
        result = SMCCall(KERNEL_INDEX_SMC, &inputStructure, &outputStructure);
        if (result != kIOReturnSuccess)
            /*
             * == Improvement: print an error "Failed to read key name with index %d; Error code%d\n"
             */
            continue; // on error skip the rest of the loop and go back to 'for'

        // Convert the 4 bytes of the key name into a string of 4 bytes
        // (independent of the byte order of the processor)
        _ultostr(key, outputStructure.key); 

        // Read the value associated with the key
        result = SMCReadKey(key, &val); // (ignore the result code)
        /*
         * == Improvement: print an error "Failed to read value of key %s; Error code %d\n"
         */
        
        // Print the value
        printVal(val);
    }
    
    /* == Improvement: count the nr of errors, both from SMCCall and SMCReadKey
     *                 and print them out.
     */
    return kIOReturnSuccess; // Always return succes :-(
}

kern_return_t SMCPrintFans(void)
{
    kern_return_t result;
    SMCVal_t      val;
    UInt32Char_t  key;
    int           totalFans, i;
    
    result = SMCReadKey("FNum", &val);
    if (result != kIOReturnSuccess)
        return kIOReturnError;
    
    totalFans = bytes2uint32(val.bytes, val.dataSize);
    printf("Total fans in system: %d\n", totalFans);
    
    for (i = 0; i < totalFans; i++)
    {
        printf("\nFan #%d:\n", i);
        sprintf(key, "F%dAc", i); 
        SMCReadKey(key, &val); 
        printf("    Actual speed : %.0f\n", _strtof(val.bytes, val.dataSize, 2));
        sprintf(key, "F%dMn", i);   
        SMCReadKey(key, &val);
        printf("    Minimum speed: %.0f\n", _strtof(val.bytes, val.dataSize, 2));
        sprintf(key, "F%dMx", i);   
        SMCReadKey(key, &val);
        printf("    Maximum speed: %.0f\n", _strtof(val.bytes, val.dataSize, 2));
        sprintf(key, "F%dSf", i);   
        SMCReadKey(key, &val);
        printf("    Safe speed   : %.0f\n", _strtof(val.bytes, val.dataSize, 2));
        sprintf(key, "F%dTg", i);   
        SMCReadKey(key, &val);
        printf("    Target speed : %.0f\n", _strtof(val.bytes, val.dataSize, 2));
        SMCReadKey("FS! ", &val);
        if ((bytes2uint32(val.bytes, 2) & (1 << i)) == 0)
            printf("    Mode         : auto\n"); 
        else
            printf("    Mode         : forced\n");
    }
    
    return kIOReturnSuccess;
}


/*
 * Print better help info
 * -r and -w require -k
 * 'version' -> 'print version info of this program'
 * 'fan info decoded' -> ... something
 * 'keys are ...' (strings as defined in...)
 * 'values are ...' (n hex digits)
 */
void usage(char* prog)
{
    printf("Apple System Management Control (SMC) tool %s\n", VERSION);
    printf("Usage:\n");
    printf("%s [options]\n", prog);
    printf("    -f         : fan info decoded\n");
    printf("    -h         : help\n");
    printf("    -k <key>   : key to manipulate\n");
    printf("    -l         : list all keys and values\n");
    printf("    -r         : read the value of a key\n");
    printf("    -w <value> : write the specified value to a key\n");
    printf("    -v         : version\n");
    printf("\n");
}

int main(int argc, char *argv[])
{
    int c;
    extern char   *optarg;
    extern int    optind, optopt, opterr;
    
    kern_return_t result;
    int           op = OP_NONE;
    UInt32Char_t  key = "\0";  // Can hold 4 bytes and a terminating \0
    SMCVal_t      val;         // Struct to hold key, size, value and 32 bytes

    // Process the options. Reminder: the ':' denotes a required argument
    while ((c = getopt(argc, argv, "fhk:lrw:v")) != -1)
    {
        switch(c)
        {
            case 'f':
                op = OP_READ_FAN;
                break;
            case 'k':
                strncpy(key, optarg, sizeof(key));   //fix for buffer overflow; limit to 4 characters (plus terminator)
                break;
            case 'l':
                op = OP_LIST;
                break;
            case 'r':
                op = OP_READ;
                break;
            case 'v':
                printf("%s\n", VERSION);    // Simply print the version number and quit
                return 0;
                break;
            case 'w':
                op = OP_WRITE;
            {
                int i;
                char c[3];  // temp string to hold 2 hex digits and terminator
                
        /*
         *  WRONG !!!
         */
                // go through the characters of <value> two by two (sort of...)
                for (i = 0; i < strlen(optarg); i++)
                {
                    // Create a string of the next two characters in <value>
                    // NOTE: 2*i will be much bigger than strlen(optarg), so this will parse beyond the end of <value>
                    sprintf(c, "%c%c", optarg[i * 2], optarg[(i * 2) + 1]);
                    // Convert the two hex digits to a byte and add it to val.bytes
                    val.bytes[i] = (int) strtol(c, NULL, 16);
                }
                val.dataSize = i / 2;   // Size in bytes: two hex characters form one byte
                
                // Half-assed check for valid input
                // Better:
                //  - scan string for hex digts
                //  - nothing may follow the hex digits
                //  - nr of digits must be even (?)
                if ((val.dataSize * 2) != strlen(optarg))
                {
                    printf("Error: value is not valid\n");
                    return 1;
                }
            }
                break;
            case 'h':
            case '?':
                op = OP_NONE; // Add: OP_HELP
                break;
        }
    }
    
    if (op == OP_NONE) //  or == OP_HELP
    {
        usage(argv[0]);
        return 1;
    }
    
/*
 *  Add all checks for valid parameters
 *   - OP_READ: must have 'key' value
 *   - OP_WRITE: must have 'key' value
 *   - Either -f, -r or -w. No combinations
 */
    
    // Open a connection to the SMC system; store the connection info in the 'conn' global variable
    SMCOpen(&conn);

    switch(op)
    {
        case OP_LIST:
            result = SMCPrintAll();
            if (result != kIOReturnSuccess)
                printf("Error: SMCPrintAll() = %08x\n", result);
            break;
        case OP_READ:
            if (strlen(key) > 0) /* This test should go before opening the connection */
            {
                result = SMCReadKey(key, &val);
                if (result != kIOReturnSuccess)
                    printf("Error: SMCReadKey() = %08x\n", result);
                else
                    printVal(val);
            }
            else
            {
                printf("Error: specify a key to read\n");
            }
            break;
        case OP_READ_FAN:
            result = SMCPrintFans();
            if (result != kIOReturnSuccess)
                printf("Error: SMCPrintFans() = %08x\n", result);
            break;
        case OP_WRITE:
            if (strlen(key) > 0) /* This test should go before opening the connection */
            {
                /* What is attempted here?? */
                // val.key is a 4-character string (plus terminator)
                // key is also a 4-character string (plus terminator)
                // looks like a simple strcpy()
                strcpy(val.key, key);
                result = SMCWriteKey(val);
                if (result != kIOReturnSuccess)
                    printf("Error: SMCWriteKey() = %08x\n", result);
            }
            else
            {
                printf("Error: specify a key to write\n");
            }
            break;
    }
    
    SMCClose(conn);
    return 0;;
}
