/*
 * File:        mathCore.h
 * Author:      Juho Rantsi
 * Created:     16.02.2026
 * Description: 
        mathCore is a library / module for all custom math functions that are 
        needed in esp32-oscilloscope project.  
 *
 * Licensed under the GNU General Public License version 2.
 * See the accompanying LICENSE file for full details.
 *
 *******************************************************************************
 *******************************************************************************
 
 Version history:
 
    16.02.2026 JR   
        - Library created

*/

#ifndef MATH_CORE_H
#define MATH_CORE_H

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

typedef struct
{
  // Pointer to buffers base / root
  void *buffer;
  
  // Length of buffer
  uint32_t length;
  
} Buffer_t;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

typedef struct
{
  
} BufferStatistics_t;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#define mathCore_calculateArrayStatistics( buffer, \
                                           startIndex, \
                                           length, stats ) \
                                        _Generic(buffer, \ 
                                          uint32_t: mathCore_calcArrStatsU32, \ 
                                          uint64_t: mathCore_calcArrStatsU64, \
                                          int32_t: mathCore_calcArrStatsI32, \ 
                                          int64_t: mathCore_calcArrStatsI64, \ 
                                          float: mathCore_calcArrStatsFloat, \
                                          double: mathCore_calcArrStatsDouble, \ 
                                        ( buffer, startIndex, length, stats )




extern void mathCore_calculateArrayStatistics( Buffer_t array,  
                                               uint32_t startIndex, 
                                               uint32_t length, 
                                               BufferStatistics_t *stats ); 


#endif
