/*******************************************************************************

License: 
This software was developed at the National Institute of Standards and 
Technology (NIST) by employees of the Federal Government in the course 
of their official duties. Pursuant to title 17 Section 105 of the 
United States Code, this software is not subject to copyright protection 
and is in the public domain. NIST assumes no responsibility  whatsoever for 
its use by other parties, and makes no guarantees, expressed or implied, 
about its quality, reliability, or any other characteristic. 

Disclaimer: 
This software was developed to promote biometric standards and biometric
technology testing for the Federal Government in accordance with the USA
PATRIOT Act and the Enhanced Border Security and Visa Entry Reform Act.
Specific hardware and software products identified in this software were used
in order to perform the software development.  In no case does such
identification imply recommendation or endorsement by the National Institute
of Standards and Technology, nor does it imply that the products and equipment
identified are necessarily the best available for the purpose.  

*******************************************************************************/

#ifndef _MYTIME_H
#define _MYTIME_H

/* this file needed to support timer and ticks */
/* UPDATED: 03/16/2005 by MDG */

#ifdef TIMER
#include <sys/types.h>
#include <sys/times.h>
#endif

#ifdef TIMER
#define set_timer(_timer_); \
   {  \
      _timer_ = (unsigned long)ticks();
#else
#define set_timer(_timer_);
#endif

#ifdef TIMER
#define time_accum(_timer_, _var_); \
      _var_ += ((unsigned long)ticks() - _timer_)/(float)ticksPerSec(); \
   }
#else
#define time_accum(_timer_, _var_);
#endif

#ifdef TIMER
#define print_time(_fp_, _fmt_, _var_); \
    fprintf(_fp_, _fmt_, _var_);
#else
#define print_time(_fp_, _fmt_, _var_);
#endif

extern unsigned long ticks(void);
extern int ticksPerSec(void);

extern unsigned long total_timer;
extern float total_time;

extern unsigned long imap_timer;
extern float imap_time;

extern unsigned long bin_timer;
extern float bin_time;

extern unsigned long minutia_timer;
extern float minutia_time;

extern unsigned long rm_minutia_timer;
extern float rm_minutia_time;

extern unsigned long ridge_count_timer;
extern float ridge_count_time;

#endif

