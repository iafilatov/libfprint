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

      FILE:    RESULTS.C
      AUTHOR:  Michael D. Garris
      DATE:    03/16/1999
      UPDATED: 10/04/1999 Version 2 by MDG
               09/14/2004
      UPDATED: 03/16/2005 by MDG

      Contains routines useful in visualizing intermediate and final
      results when exercising the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        write_text_results()
                        write_minutiae_XYTQ()
                        dump_map()
                        drawmap()
                        drawmap2()
                        drawblocks()
                        drawrotgrid()
                        dump_link_table()

***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <lfs.h>
#include <sunrast.h>
#include <defs.h>

/*************************************************************************
**************************************************************************
#cat: write_text_results - Takes LFS results including minutiae and image
#cat:              maps and writes them to separate formatted text files.

   Input:
      oroot     - root pathname for output files
      m1flag    - if flag set, write (X,Y,T)'s out to "*.xyt" file according
                  to M1 (ANSI INCITS 378-2004) minutiae representation

                  M1 Rep:
                           1. pixel origin top left
                           2. direction pointing up the ridge ending or
                              bifurcaiton valley
                  NIST Internal Rep:
                           1. pixel origin bottom left
                           2. direction pointing out and away from the
                              ridge ending or bifurcation valley

      iw        - image pixel width
      ih        - image pixel height
      minutiae  - structure containing the detected minutiae
      quality_map      - integrated image quality map
      direction_map    - direction map
      low_contrast_map - low contrast map
      low_flow_map     - low ridge flow map
      high_curve_map   - high curvature map
      map_w     - width (in blocks) of image maps
      map_h     - height (in blocks) of image maps
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int write_text_results(char *oroot, const int m1flag,
                       const int iw, const int ih,
                       const MINUTIAE *minutiae, int *quality_map,
                       int *direction_map, int *low_contrast_map,
                       int *low_flow_map, int *high_curve_map,
                       const int map_w, const int map_h)
{
   FILE *fp;
   int  ret;
   char ofile[MAXPATHLEN];

   /* 1. Write Minutiae results to text file "<oroot>.min". */
   /*    XYT's written in LFS native representation:        */
   /*       1. pixel coordinates with origin top-left       */
   /*       2. 11.25 degrees quantized integer orientation  */
   /*          on range [0..31]                             */
   /*       3. minutiae reliability on range [0.0 .. 1.0]   */
   /*          with 0.0 lowest and 1.0 highest reliability  */
   sprintf(ofile, "%s.%s", oroot, MIN_TXT_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-2);
   }
   /* Print out Image Dimensions Header */
   /* !!!! Image dimension header added 09-13-04 !!!! */
   fprintf(fp, "Image (w,h) %d %d\n", iw, ih);
   /* Print text report from the structure containing detected minutiae. */
   dump_minutiae(fp, minutiae);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-3);
   }

   /* 2. Write just minutiae XYT's & Qualities to text      */
   /*    file "<oroot>.xyt".                                */
   /*                                                       */
   /*    A. If M1 flag set:                                 */
   /*       XYTQ's written according to M1 (ANSI INCITS     */
   /*          378-2004) representation:                    */
   /*       1. pixel coordinates with origin top-left       */
   /*       2. orientation in degrees on range [0..360]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise                                    */
   /*       3. direction pointing up the ridge ending or    */
   /*             bifurcaiton valley                        */
   /*       4. minutiae qualities on integer range [0..100] */
   /*             (non-standard)                            */
   /*                                                       */
   /*    B. If M1 flag NOT set:                             */
   /*       XYTQ's written according to NIST internal rep.  */
   /*       1. pixel coordinates with origin bottom-left    */
   /*       2. orientation in degrees on range [0..360]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise (same as M1)                       */
   /*       3. direction pointing out and away from the     */
   /*             ridge ending or bifurcation valley        */
   /*             (opposite direction from M1)              */
   /*       4. minutiae qualities on integer range [0..100] */
   /*             (non-standard)                            */
   sprintf(ofile, "%s.%s", oroot, XYT_EXT);
   if(m1flag){
      if((ret = write_minutiae_XYTQ(ofile, M1_XYT_REP, minutiae, iw, ih))){
         return(ret);
      }
   }
   else{
      if((ret = write_minutiae_XYTQ(ofile, NIST_INTERNAL_XYT_REP,
                                   minutiae, iw, ih))){
         return(ret);
      }
   }

   /* 3. Write Integrated Quality Map results to text file. */
   sprintf(ofile, "%s.%s", oroot, QUALITY_MAP_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-4);
   }
   /* Print a text report from the map. */
   dump_map(fp, quality_map, map_w, map_h);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-5);
   }

   /* 4. Write Direction Map results to text file. */
   sprintf(ofile, "%s.%s", oroot, DIRECTION_MAP_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-6);
   }
   /* Print a text report from the map. */
   dump_map(fp, direction_map, map_w, map_h);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-7);
   }

   /* 5. Write Low Contrast Map results to text file. */
   sprintf(ofile, "%s.%s", oroot, LOW_CONTRAST_MAP_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-8);
   }
   /* Print a text report from the map. */
   dump_map(fp, low_contrast_map, map_w, map_h);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-9);
   }

   /* 6. Write Low Flow Map results to text file. */
   sprintf(ofile, "%s.%s", oroot, LOW_FLOW_MAP_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-10);
   }
   /* Print a text report from the map. */
   dump_map(fp, low_flow_map, map_w, map_h);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-11);
   }

   /* 7. Write High Curvature Map results to text file. */
   sprintf(ofile, "%s.%s", oroot, HIGH_CURVE_MAP_EXT);
   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_text_results : fopen : %s\n", ofile);
      return(-12);
   }
   /* Print a text report from the map. */
   dump_map(fp, high_curve_map, map_w, map_h);
   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_text_results : fclose : %s\n", ofile);
      return(-13);
   }

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: write_minutiae_XYTQ - Write just minutiae XYT's & Qualities to text
#cat:                   file according to the specified mintuiae represenation

   Input:
      ofile    - output file name
      reptype  - specifies XYT output representation
      minutiae - structure containing a list of LFS detected minutiae
      iw         - width (in pixels) of the input image
      ih         - height (in pixels) of the input image
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
int write_minutiae_XYTQ(char *ofile, const int reptype,
                        const MINUTIAE *minutiae, const int iw, const int ih)
{
   FILE *fp;
   int i, ox, oy, ot, oq;
   MINUTIA *minutia;

   /*    A. If M1 flag set:                                 */
   /*       XYTQ's written according to M1 (ANSI INCITS     */
   /*          378-2004) representation:                    */
   /*       1. pixel coordinates with origin top-left       */
   /*       2. orientation in degrees on range [0..360]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise                                    */
   /*       3. direction pointing up the ridge ending or    */
   /*             bifurcaiton valley                        */
   /*       4. minutiae qualities on integer range [0..100] */
   /*             (non-standard)                            */
   /*                                                       */
   /*    B. If M1 flag NOT set:                             */
   /*       XYTQ's written according to NIST internal rep.  */
   /*       1. pixel coordinates with origin bottom-left    */
   /*       2. orientation in degrees on range [0..360]     */
   /*          with 0 pointing east and increasing counter  */
   /*          clockwise (same as M1)                       */
   /*       3. direction pointing out and away from the     */
   /*             ridge ending or bifurcation valley        */
   /*             (opposite direction from M1)              */
   /*       4. minutiae qualities on integer range [0..100] */
   /*             (non-standard)                            */

   if((fp = fopen(ofile, "wb")) == (FILE *)NULL){
      fprintf(stderr, "ERROR : write_minutiae_XYTQ : fopen : %s\n", ofile);
      return(-2);
   }

   for(i = 0; i < minutiae->num; i++){
      minutia = minutiae->list[i];

      switch(reptype){
      case M1_XYT_REP:
           lfs2m1_minutia_XYT(&ox, &oy, &ot, minutia);
           break;
      case NIST_INTERNAL_XYT_REP:
           lfs2nist_minutia_XYT(&ox, &oy, &ot, minutia, iw, ih);
           break;
      default:
           fprintf(stderr, "ERROR : write_minutiae_XYTQ : ");
           fprintf(stderr, "Invalid XYT representation type = %d\n", reptype);
           fclose(fp);
           return(-4);
      }

      oq = sround(minutia->reliability * 100.0);

      fprintf(fp, "%d %d %d %d\n", ox, oy, ot, oq);
   }


   if(fclose(fp)){
      fprintf(stderr, "ERROR : write_minutiae_XYTQ : fopen : %s\n", ofile);
      return(-5);
   }

   return(0);
}

/*************************************************************************
**************************************************************************
#cat: dump_map - Prints a text report to the specified open file pointer
#cat:             of the integer values in a 2D integer vector.

   Input:
      fpout - open file pointer
      map  - vector of integer directions (-1 ==> invalid direction)
      mw    - width (number of blocks) of map vector
      mh    - height (number of blocks) of map vector
**************************************************************************/
void dump_map(FILE *fpout, int *map, const int mw, const int mh)
{
   int mx, my;
   int *iptr;

   /* Simply print the map matrix out to the specified file pointer. */
   iptr = map;
   for(my = 0; my < mh; my++){
      for(mx = 0; mx < mw; mx++){
         fprintf(fpout, "%2d ", *iptr++);
      }
      fprintf(fpout, "\n");
   }
}

/*************************************************************************
**************************************************************************
#cat: drawmap - Draws integer direction vectors over their respective blocks
#cat:            of an input image.  Note that the input image is modified
#cat:            upon return form this routine.

   Input:
      imap       - computed vector of integer directions. (-1 ==> invalid)
      mw         - width (in blocks) of the map
      mh         - height (in blocks) of the map
      idata      - input image data to be annotated
      iw         - width (in pixels) of the input image
      ih         - height (in pixels) of the input image
      rotgrids   - structure containing the rotated pixel grid offsets
      draw_pixel - pixel intensity to be used when drawing on the image
   Output:
      idata      - input image contains the results of the annoatation
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
int drawmap(int *imap, const int mw, const int mh,
              unsigned char *idata, const int iw, const int ih,
              const ROTGRIDS *dftgrids, const int draw_pixel)
{
   int bi, *iptr;
   double dy, dx, xincr, yincr;
   int cbxy;
   int i, xyoffset;
   unsigned char *cptr, *lptr, *rptr, *eptr;
   double theta, pi_incr;
   int *blkoffs, bw, bh;
   int ret; /* return code */

   /* Compute block offsets into the input image. */
   /* Block_offsets() assumes square block (grid), so ERROR otherwise. */
   if(dftgrids->grid_w != dftgrids->grid_h){
      fprintf(stderr, "ERROR : drawmap : DFT grids must be square\n");
      return(-130);
   }
   if((ret = block_offsets(&blkoffs, &bw, &bh, iw, ih,
                           dftgrids->pad, dftgrids->grid_w))){
      return(ret);
   }

   if((bw != mw) || (bh != mh)){
      /* Free memory allocated to this point. */
      free(blkoffs);
      fprintf(stderr,
  "ERROR : drawmap : block dimensions between map and image do not match\n");
      return(-131);
   }

   cbxy = dftgrids->grid_w>>1;
   pi_incr = M_PI/(double)dftgrids->ngrids;

   eptr = idata + (ih*iw);
   iptr = imap;
   /* Foreach block in image ... */
   for(bi = 0; bi < mw*mh; bi++){

      /* If valid direction for block ... */
      if(*iptr != INVALID_DIR){

         /* Get slope components of direction angle */
         theta = dftgrids->start_angle + (*iptr * pi_incr);
         dx = cos(theta);
         dy = sin(theta);

         /* Draw line rotated by the direction angle and centered */
         /* on the block.                                         */
         /* Check if line is perfectly vertical ... */
         if(dx == 0){
            /* Draw vertical line starting at top of block shifted */
            /* over to horizontal center of the block.             */
            lptr = idata + blkoffs[bi] + cbxy;
            for(i = 0; i < dftgrids->grid_w; i++){
               if((lptr > idata) && (lptr < eptr)){
                  *lptr = draw_pixel;
               }
               lptr += iw;
            }
         }
         else{
            cptr = idata + blkoffs[bi] + (cbxy*iw) + cbxy;

            /* Draw center pixel */
            *cptr = draw_pixel;

            /* Draw left and right half of line */
            xincr = dx;
            yincr = dy;
            for(i = 0; i < cbxy; i++){
               xyoffset = (sround(yincr)*iw) + sround(xincr);
               rptr = cptr + xyoffset;
               if((rptr > idata) && (rptr < eptr)){
                  *rptr = draw_pixel;
               }
               lptr = cptr - xyoffset;
               if((lptr > idata) && (lptr < eptr)){
                  *lptr = draw_pixel;
               }
               xincr += dx;
               yincr += dy;
            }
         }
      }
      iptr++;
   }

   /* Deallocate working memory */
   free(blkoffs);

   return(0);
}

/*************************************************************************
**************************************************************************
#cat: drawmap2 - Draws integer direction vectors over their respective blocks
#cat:            of an input image.  Note that the input image is modified
#cat:            upon return form this routine.  In this version of the
#cat:            routine, offsets to the origin of each block in the image
#cat:            must be precomputed and passed in.

   Input:
      imap       - computed vector of integer directions. (-1 ==> invalid)
      blkoffs    - list of pixel offsets to the origin of each block in the
                   image from which the map was computed
      mw         - width (in blocks) of the map
      mh         - height (in blocks) of the map
      pdata      - input image data to be annotated
      pw         - width (in pixels) of the input image
      ph         - height (in pixels) of the input image
      start_angle - the angle (in radians) that the direction 0 points in
      ndirs      - number of directions within a half circle
      blocksize  - the dimensions (in pixels) of each block
   Output:
      pdata      - input image contains the results of the annoatation
**************************************************************************/
void drawmap2(int *imap, const int *blkoffs, const int mw, const int mh,
              unsigned char *pdata, const int pw, const int ph,
              const double start_angle, const int ndirs, const int blocksize)
{
   int bi, *iptr;
   double dy, dx, xincr, yincr;
   int cbxy;
   int i, xyoffset;
   unsigned char *cptr, *lptr, *rptr, *eptr;
   double theta, pi_incr;

   cbxy = blocksize>>1;
   pi_incr = M_PI/(double)ndirs;

   eptr = pdata + (pw*ph);
   iptr = imap;
   /* Foreach block in image ... */
   for(bi = 0; bi < mw*mh; bi++){

      /* If valid direction for block ... */
      if(*iptr != INVALID_DIR){

         /* Get slope components of direction angle */
         theta = start_angle + (*iptr * pi_incr);
         dx = cos((double)theta);
         dy = sin((double)theta);

         /* Draw line rotated by the direction angle and centered */
         /* on the block.                                         */
         /* Check if line is perfectly vertical ... */
         if(dx == 0){
            /* Draw vertical line starting at top of block shifted */
            /* over to horizontal center of the block.             */
            lptr = pdata + blkoffs[bi] + cbxy;
            for(i = 0; i < blocksize; i++){
               if((lptr > pdata) && (lptr < eptr))
                  *lptr = 255;
               lptr += pw;
            }
         }
         else{
            cptr = pdata + blkoffs[bi] + (cbxy*pw) + cbxy;

            /* Draw center pixel */
            *cptr = 255;

            /* Draw left and right half of line */
            xincr = dx;
            yincr = dy;
            for(i = 0; i < cbxy; i++){
               xyoffset = (sround(yincr)*pw) + sround(xincr);
               rptr = cptr + xyoffset;
               if((rptr > pdata) && (rptr < eptr))
                  *rptr = 255;
               lptr = cptr - xyoffset;
               if((lptr > pdata) && (lptr < eptr))
                  *lptr = 255;
               xincr += dx;
               yincr += dy;
            }
         }
      }
      iptr++;
   }
}

/*************************************************************************
**************************************************************************
#cat: drawblocks - Annotates an input image with the location of each block's
#cat:              origin.  This routine is useful to see how blocks are
#cat:              assigned to arbitrarily-sized images that are not an even
#cat:              width or height of the block size. In these cases the last
#cat:              column pair and row pair of blocks overlap each other.
#cat:              Note that the input image is modified upon return form
#cat:              this routine.

   Input:
      blkoffs    - offsets to the pixel origin of each block in the image
      mw         - number of blocks horizontally in the input image
      mh         - number of blocks vertically in the input image
      pdata      - input image data to be annotated that has pixel dimensions
                   compatible with the offsets in blkoffs
      pw         - width (in pixels) of the input image
      ph         - height (in pixels) of the input image
      draw_pixel - pixel intensity to be used when drawing on the image
   Output:
      pdata      - input image contains the results of the annoatation
**************************************************************************/
void drawblocks(const int *blkoffs, const int mw, const int mh,
                unsigned char *pdata, const int pw, const int ph,
                const int draw_pixel)
{
   int bi;
   unsigned char *bptr;

   for(bi = 0; bi < mw*mh; bi++){
      bptr = pdata + blkoffs[bi];
      *bptr = draw_pixel;
   }
}


/*************************************************************************
**************************************************************************
#cat: drawrotgrid - Annotates an input image with a specified rotated grid.
#cat:               This routine is useful to see the location and orientation
#cat:               of a specific rotated grid within a specific block in the
#cat:               image.  Note that the input image is modified upon return
#cat:               form this routine.

   Input:
      rotgrids   - structure containing the rotated pixel grid offsets
      dir        - integer direction of the rotated grid to be annontated
      idata      - input image data to be annotated.
      blkoffset  - the pixel offset from the origin of the input image to
                   the origin of the specific block to be annoted
      iw         - width (in pixels) of the input image
      ih         - height (in pixels) of the input image
      draw_pixel - pixel intensity to be used when drawing on the image
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
int drawrotgrid(const ROTGRIDS *rotgrids, const int dir,
                unsigned char *idata, const int blkoffset,
                const int iw, const int ih, const int draw_pixel)
{
   int i, j, gi;

   /* Check if specified rotation direction is within range of */
   /* rotated grids. */
   if(dir >= rotgrids->ngrids){
      fprintf(stderr,
    "ERROR : drawrotgrid : input direction exceeds range of rotated grids\n");
      return(-140);
   }

   /* Intialize grid offset index */
   gi = 0;
   /* Foreach row in rotated grid ... */
   for(i = 0; i < rotgrids->grid_h; i++){
      /* Foreach column in rotated grid ... */
      for(j = 0; j < rotgrids->grid_w; j++){
         /* Draw pixels from every other rotated row to represent  direction */
         /* of line sums used in DFT processing. */
         if(i%2)
            *(idata+blkoffset+rotgrids->grids[dir][gi]) = draw_pixel;
         /* Bump grid offset index */
         gi++;
      }
   }

   return(0);
}

/*************************************************************************
**************************************************************************
#cat: dump_link_table - takes a link table and vectors of minutia IDs
#cat:              assigned to its axes and prints the table's contents out
#cat:              as formatted text to the specified open file pointer.

   Input:
      fpout      - open file pointer
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      tbldim     - dimension of each axes of the link table
      minutiae   - list of minutia points
**************************************************************************/
void dump_link_table(FILE *fpout, const int *link_table,
                   const int *x_axis, const int *y_axis,
                   const int nx_axis, const int ny_axis, const int tbldim,
                   const MINUTIAE *minutiae)
{
   int i, tx, ty, sentry, entry;

   fprintf(fpout, "DUMP LINK TABLE:\n");

   fprintf(fpout, "X-AXIS:\n");
   for(i = 0; i < nx_axis; i++){
      fprintf(fpout, "%d: %d,%d\n", i, minutiae->list[x_axis[i]]->x,
                              minutiae->list[x_axis[i]]->y);
   }

   fprintf(fpout, "Y-AXIS:\n");
   for(i = 0; i < ny_axis; i++){
      fprintf(fpout, "%d: %d,%d\n", i, minutiae->list[y_axis[i]]->x,
                              minutiae->list[y_axis[i]]->y);
   }

   fprintf(fpout, "TABLE:\n");
   sentry = 0;
   for(ty = 0; ty < ny_axis; ty++){
      entry = sentry;
      for(tx = 0; tx < nx_axis; tx++){
         fprintf(fpout, "%7d ", link_table[entry++]);
      }
      fprintf(fpout, "\n");
      sentry += tbldim;
   }
}

