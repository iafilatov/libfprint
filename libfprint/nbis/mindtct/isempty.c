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

/************************************************************************/
/***********************************************************************
      LIBRARY: LFS - NIST Latent Fingerprint System

      FILE:           ISEMPTY.C
      AUTHOR:         Michael D. Garris
      DATE:           09/13/2004

      Contains routines responsible for determining if a fingerprint
      image is empty.

***********************************************************************
               ROUTINES:
                        is_image_empty()
                        is_qmap_empty()

***********************************************************************/

#include <lfs.h>

/***********************************************************************
************************************************************************
#cat: is_image_empty - Routine determines if statistics passed indicate
#cat:                  an empty image.

   Input:
      quality_map - quality map computed by NIST's Mindtct
      map_w       - width of map
      map_h       - height of map
   Return Code:
      True        - image determined empty
      False       - image determined NOT empty
************************************************************************/
int is_image_empty(int *quality_map, const int map_w, const int map_h)
{
   /* This routine is designed to be expanded as more statistical */
   /* tests are developed. */

   if(is_qmap_empty(quality_map, map_w, map_h))
      return(TRUE);
   else
      return(FALSE);
}

/***********************************************************************
************************************************************************
#cat: is_qmap_empty - Routine determines if quality map is all set to zero

   Input:
      quality_map - quality map computed by NIST's Mindtct
      map_w       - width of map
      map_h       - height of map
   Return Code:
      True        - quality map is empty
      False       - quality map is NOT empty
************************************************************************/
int is_qmap_empty(int *quality_map, const int map_w, const int map_h)
{
   int i, maplen;
   int *qptr;

   qptr = quality_map;
   maplen = map_w * map_h;
   for(i = 0; i < maplen; i++){
      if(*qptr++ != 0){
         return(FALSE);
      }
   }
   return(TRUE);
}

