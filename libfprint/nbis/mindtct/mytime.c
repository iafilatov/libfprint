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

/***********************************************************************
      LIBRARY: LFS - NIST Latent Fingerprint System

      FILE:    MYTIME.C
      AUTHOR:  Michael D. Garris
      DATE:    03/16/1999

      Contains global variable definitions used to record timings by
      the NIST Latent Fingerprint System (LFS).
***********************************************************************/

/* Total time: including all initializations                            */
/*           : excluding all I/O except for required HO39 input image   */
/*             (This is done to contrast the fact that the NIST GENHO39 */
/*              eliminates the need for this extra read.)               */
unsigned long total_timer;
float total_time = 0.0;   /* time accumulator */

/* IMAP generation time: excluding initialization */
unsigned long imap_timer;
float imap_time = 0.0;   /* time accumulator */

/* Binarization time: excluding initialization */
unsigned long bin_timer;
float bin_time = 0.0;   /* time accumulator */

/* Minutia Detection time */
unsigned long minutia_timer;
float minutia_time = 0.0;   /* time accumulator */

/* Minutia Removal time */
unsigned long rm_minutia_timer;
float rm_minutia_time = 0.0; /* time accumulator */

/* Neighbor Ridge Counting time */
unsigned long ridge_count_timer;
float ridge_count_time = 0.0; /* time accumulator */
