/*******************************************************************************

License:
This software and/or related materials was developed at the National Institute
of Standards and Technology (NIST) by employees of the Federal Government
in the course of their official duties. Pursuant to title 17 Section 105
of the United States Code, this software is not subject to copyright
protection and is in the public domain.

This software and/or related materials have been determined to be not subject
to the EAR (see Part 734.3 of the EAR for exact details) because it is
a publicly available technology and software, and is freely distributed
to any interested party with no licensing requirements.  Therefore, it is
permissible to distribute this software as a free download from the internet.

Disclaimer:
This software and/or related materials was developed to promote biometric
standards and biometric technology testing for the Federal Government
in accordance with the USA PATRIOT Act and the Enhanced Border Security
and Visa Entry Reform Act. Specific hardware and software products identified
in this software were used in order to perform the software development.
In no case does such identification imply recommendation or endorsement
by the National Institute of Standards and Technology, nor does it imply that
the products and equipment identified are necessarily the best available
for the purpose.

This software and/or related materials are provided "AS-IS" without warranty
of any kind including NO WARRANTY OF PERFORMANCE, MERCHANTABILITY,
NO WARRANTY OF NON-INFRINGEMENT OF ANY 3RD PARTY INTELLECTUAL PROPERTY
or FITNESS FOR A PARTICULAR PURPOSE or for any purpose whatsoever, for the
licensed product, however used. In no event shall NIST be liable for any
damages and/or costs, including but not limited to incidental or consequential
damages of any kind, including economic damage or injury to property and lost
profits, regardless of whether NIST shall be advised, have reason to know,
or in fact shall know of the possibility.

By using this software, you agree to bear all risk relating to quality,
use and performance of the software and/or related materials.  You agree
to hold the Government harmless from any claim arising from your use
of the software.

*******************************************************************************/

/***********************************************************************
      LIBRARY: FING - NIST Fingerprint Systems Utilities

      FILE:           BZ_IO.C
      ALGORITHM:      Allan S. Bozorth (FBI)
      MODIFICATIONS:  Michael D. Garris (NIST)
                      Stan Janet (NIST)
      DATE:           09/21/2004
      UPDATED:        01/11/2012 by Kenneth Ko
      UPDATED:        03/08/2012 by Kenneth Ko
      UPDATED:        07/10/2014 by Kenneth Ko

      Contains routines responsible for supporting command line
      processing, file and data input to, and output from the
      Bozorth3 fingerprint matching algorithm.

***********************************************************************

      ROUTINES:
#cat: parse_line_range - parses strings of the form #-# into the upper
#cat:            and lower bounds of a range corresponding to lines in
#cat:            an input file list
#cat: set_progname - stores the program name for the current invocation
#cat: set_probe_filename - stores the name of the current probe file
#cat:            being processed
#cat: set_gallery_filename - stores the name of the current gallery file
#cat:            being processed
#cat: get_progname - retrieves the program name for the current invocation
#cat: get_probe_filename - retrieves the name of the current probe file
#cat:            being processed
#cat: get_gallery_filename - retrieves the name of the current gallery
#cat:            file being processed
#cat: get_next_file - gets the next probe (or gallery) filename to be
#cat:            processed, either from the command line or from a
#cat:            file list
#cat: get_score_filename - returns the filename to which the output line
#cat:            should be written
#cat: get_score_line - formats output lines based on command line options
#cat:            specified
#cat: bz_load -  loads the contents of the specified XYT file into
#cat:            structured memory
#cat: fd_readable - when multiple bozorth processes are being run
#cat:            concurrently and one of the processes determines a
#cat:            has been found, the other processes poll a file
#cat:            descriptor using this function to see if they
#cat:            should exit as well

***********************************************************************/

#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <bozorth.h>

/***********************************************************************/

/***********************************************************************/

/* Used by the following set* and get* routines */
static char program_buffer[ 1024 ];
static char * pfile;
static char * gfile;

/***********************************************************************/

/***********************************************************************/

/***********************************************************************/

/***********************************************************************/
char * get_progname( void )
{
return program_buffer;
}

/***********************************************************************/
char * get_probe_filename( void )
{
return pfile;
}

/***********************************************************************/
char * get_gallery_filename( void )
{
return gfile;
}

/***********************************************************************/

/***********************************************************************/
/* returns CNULL on error */

/***********************************************************************/

/************************************************************************
Load a 3-4 column (X,Y,T[,Q]) set of minutiae from the specified file
and return a XYT sturcture.
Row 3's value is an angle which is normalized to the interval (-180,180].
A maximum of MAX_BOZORTH_MINUTIAE minutiae can be returned -- fewer if
"DEFAULT_BOZORTH_MINUTIAE" is smaller.  If the file contains more minutiae than are
to be returned, the highest-quality minutiae are returned.
*************************************************************************/

/***********************************************************************/

/************************************************************************
Load a XYTQ structure and return a XYT struct.
Row 3's value is an angle which is normalized to the interval (-180,180].
A maximum of MAX_BOZORTH_MINUTIAE minutiae can be returned -- fewer if
"DEFAULT_BOZORTH_MINUTIAE" is smaller.  If the file contains more minutiae than are
to be returned, the highest-quality minutiae are returned.
*************************************************************************/

/***********************************************************************/
#ifdef PARALLEL_SEARCH
int fd_readable( int fd )
{
int retval;
fd_set rfds;
struct timeval tv;


FD_ZERO( &rfds );
FD_SET( fd, &rfds );
tv.tv_sec = 0;
tv.tv_usec = 0;

retval = select( fd+1, &rfds, NULL, NULL, &tv );

if ( retval < 0 ) {
   perror( "select() failed" );
   return 0;
}

if ( FD_ISSET( fd, &rfds ) ) {
   /*fprintf( stderr, "data is available now.\n" );*/
   return 1;
}

/* fprintf( stderr, "no data is available\n" ); */
return 0;
}
#endif
