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

      FILE:    LINK.C
      AUTHOR:  Michael D. Garris
      DATE:    08/02/1999
      UPDATED: 10/04/1999 Version 2 by MDG
      UPDATED: 03/16/2005 by MDG

      Contains routines responsible for linking compatible minutiae
      together as part of the NIST Latent Fingerprint System (LFS).

***********************************************************************
               ROUTINES:
                        link_minutiae()
                        create_link_table()
                        update_link_table()
                        order_link_table()
                        process_link_table()
                        link_score()
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lfs.h>
#include <log.h>

/*************************************************************************
**************************************************************************
#cat: link_minutiae - Clusters minutiae that are sufficiently close to each
#cat:                other and have compatible directions to be considered part
#cat:                of the same ridge or valley and then links them together.
#cat:                In linking two minutia, the respective minutia features
#cat:                in the image are joined by drawing pixels and the points
#cat:                are removed from the list.

   Input:
      minutiae  - list of true and false minutiae
      bdata     - binary image data (0==while & 1==black)
      iw        - width (in pixels) of image
      ih        - height (in pixels) of image
      nmap      - IMAP ridge flow matrix with invalid, high-curvature,
                  and no-valid-neighbor regions identified
      mw        - width in blocks of the NMAP
      mh        - height in blocks of the NMAP
      lfsparms  - parameters and thresholds for controlling LFS
   Output:
      minutiae  - list of pruned minutiae
      bdata     - edited binary image with breaks in ridges and valleys filled
   Return Code:
      Zero     - successful completion
      Negative - system error
**************************************************************************/
int link_minutiae(MINUTIAE *minutiae,
                 unsigned char *bdata, const int iw, const int ih,
                 int *nmap, const int mw, const int mh,
                 const LFSPARMS *lfsparms)
{
   int i, ret, *onloop;
   MINUTIA *main_min; /* Renamed by MDG on 03-16-05 */
   int main_x, main_y;
   int *link_table, *x_axis, *y_axis, nx_axis, ny_axis, n_entries;

   print2log("\nLINKING MINUTIA:\n");

   /* Go through the list of minutiae and detect any small loops (ex. */
   /* < 15 pixels in circumference), and remove any minutiae          */
   /* (bifurcations) with bad contours.                               */
   if((ret = get_loop_list(&onloop, minutiae, lfsparms->small_loop_len,
                           bdata, iw, ih)))
      return(ret);

   i = 0;
   /* Foreach minutia treated as a "main" minutia */
   /* (except for the very last one) ...          */
   while(i < minutiae->num-1){

      /* Set current minutia to "main" minutia. */
      main_min = minutiae->list[i];
      main_x = main_min->x;
      main_y = main_min->y;

      /* If the main minutia is NOT on a small loop ... */
      if(!onloop[i]){

         /* Then minutia is a ridge-ending OR a bifurcation */
         /* which is NOT to be skipped ...                  */

         /* We now want to build a connected graph (link table) of    */
         /* all those minutiae sufficiently close and complementing   */
         /* each other that they potentially could be merged (linked) */
         /* together. (Ex. table has max dimensions of 20).           */
         if((ret = create_link_table(&link_table, &x_axis, &y_axis,
                           &nx_axis, &ny_axis, &n_entries,
                           lfsparms->link_table_dim,
                           i, minutiae, onloop, nmap, mw, mh,
                           bdata, iw, ih, lfsparms))){
            /* Deallocate working memory. */
            free(onloop);
            /* Return error code. */
            return(ret);
         }

         /* Put the link table in sorted order based on x and then y-axis  */
         /* entries.  These minutia are sorted based on their point of     */
         /* perpendicular intersection with a line running from the origin */
         /* at an angle equal to the average direction of all entries in   */
         /* the link table.                                                */
         if((ret = order_link_table(link_table, x_axis, y_axis,
                                   nx_axis, ny_axis, n_entries,
                                   lfsparms->link_table_dim, minutiae,
                                   lfsparms->num_directions))){
            /* Deallocate working memories. */
            free(link_table);
            free(x_axis);
            free(y_axis);
            free(onloop);
            /* Return error code. */
            return(ret);
         }

         /* Process the link table deciding which minutia pairs in  */
         /* the table should be linked (ie. joined in the image and */
         /* removed from the minutiae list (and from onloop).        */
         if((ret = process_link_table(link_table, x_axis, y_axis,
                            nx_axis, ny_axis, n_entries,
                            lfsparms->link_table_dim,
                            minutiae, onloop, bdata, iw, ih, lfsparms))){
            /* Deallocate working memories. */
            free(link_table);
            free(x_axis);
            free(y_axis);
            free(onloop);
            /* Return error code. */
            return(ret);
         }

         /* Deallocate link table buffers. */
         free(link_table);
         free(x_axis);
         free(y_axis);
      }
      /* Otherwise, skip minutia on a small loop. */

      /* Check to see if the current "main" minutia has been removed */
      /* from the minutiae list.  If it has not, then we need to      */
      /* advance to the next minutia in the list.  If it has been    */
      /* removed, then we are pointing to the next minutia already.  */
      if((minutiae->list[i]->x == main_x) &&
         (minutiae->list[i]->y == main_y))
         /* Advance to the next main feature in the list. */
         i++;

      /* At this point 'i' is pointing to the next main minutia to be */
      /* processed, so continue.                                      */
   }

   free(onloop);
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: create_link_table - Builds a 2D minutia link table where each cell in the
#cat:                    table represents a potential linking of 2 different
#cat:                    minutia points.  Minutia IDs are stored on each axes
#cat:                    and scores representing the degree of compatibility
#cat:                    between 2 minutia are stored in each cell.  Note that
#cat:                    the table is sparsely filled with scores.

   Input:
      tbldim    - dimension of each axes of the link table
      start     - index position of starting minutia point in input list
      minutiae  - list of minutia
      onloop    - list of loop flags (on flag for each minutia point in list)
      nmap      - IMAP ridge flow matrix with invalid, high-curvature,
                  and no-valid-neighbor regions identified
      mw        - width in blocks of the NMAP
      mh        - height in blocks of the NMAP
      bdata     - binary image data (0==while & 1==black)
      iw        - width (in pixels) of image
      ih        - height (in pixels) of image
      lfsparms  - parameters and thresholds for controlling LFS
   Output:
      olink_table - sparse 2D table containing scores of potentially
                    linked minutia pairs
      ox_axis     - minutia IDs registered along x-axis
      oy_axis     - minutia IDs registered along y-axis
      onx_axis    - number of minutia registered along x-axis
      ony_axis    - number of minutia registered along y-axis
      on_entries  - number of scores currently entered in the table
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int create_link_table(int **olink_table, int **ox_axis, int **oy_axis,
           int *onx_axis, int *ony_axis, int *on_entries, const int tbldim, 
           const int start, const MINUTIAE *minutiae, const int *onloop,
           int *nmap, const int mw, const int mh,
           unsigned char *bdata, const int iw, const int ih,
           const LFSPARMS *lfsparms)
{
   int ret, first, second;
   int full_ndirs, qtr_ndirs, half_ndirs, low_curve_min_deltadir, deltadir;
   int *link_table, *x_axis, *y_axis, nx_axis, ny_axis, n_entries, tblalloc;
   int *queue, *inqueue, head, tail;
   MINUTIA *minutia1, *minutia2;
   int xblk, yblk;
   int nmapval, opp1dir, joindir, iscore;
   double jointheta, joindist, dscore;

   /* Compute number of directions on full circle. */
   full_ndirs = lfsparms->num_directions<<1;
   /* Compute number of directions in 45=(180/4) degrees. */
   qtr_ndirs = lfsparms->num_directions>>2;
   /* Compute number of directions in 90=(180/2) degrees. */
   half_ndirs = lfsparms->num_directions>>1;

   /* Minimum allowable deltadir to link minutia in low-curvature region.   */
   /* (The closer the deltadir is to 180 degrees, the more likely the link. */
   /* When ndirs==16, then this value is 11=(3*4)-1 == 123.75 degrees.      */
   /* I chose to parameterize this threshold based on a fixed fraction of   */
   /* 'ndirs' rather than on passing in a parameter in degrees and doing    */
   /* the conversion.  I doubt the difference matters.                      */
   low_curve_min_deltadir = (3 * qtr_ndirs) - 1;

   /* Allocate and initialize link table buffers.                  */
   /* Note: The use of "calloc" initializes all table values to 0. */
   tblalloc = tbldim * tbldim;
   link_table = (int *)calloc(tblalloc, sizeof(int));
   if(link_table == (int *)NULL){
      fprintf(stderr, "ERROR : create_link_table : calloc : link_table\n");
      return(-330);
   }
   /* Allocate horizontal axis entries in table. */
   x_axis = (int *)malloc(tbldim * sizeof(int));
   if(x_axis == (int *)NULL){
      free(link_table);
      fprintf(stderr, "ERROR : create_link_table : malloc : x_axis\n");
      return(-331);
   }
   /* Allocate vertical axis entries in table. */
   y_axis = (int *)malloc(tbldim * sizeof(int));
   if(y_axis == (int *)NULL){
      free(link_table);
      free(x_axis);
      fprintf(stderr, "ERROR : create_link_table : malloc : y_axis\n");
      return(-332);
   }
   nx_axis = 0;
   ny_axis = 0;
   n_entries = 0;

   /* Allocate and initalize queue buffers.  As minutia are entered into   */
   /* the link table they are placed in the queue for subsequent matching. */
   queue = (int *)malloc(minutiae->num * sizeof(int));
   if(queue == (int *)NULL){
      free(link_table);
      free(x_axis);
      free(y_axis);
      fprintf(stderr, "ERROR : create_link_table : malloc : queue\n");
      return(-333);
   }
   /* List of flags to indicate if a manutia has been entered in the queue. */
   /* Once a minutia "in queue" status is set to TRUE it will not be reset. */
   /* This way a minutia will only ever be processed as a primary minutia   */
   /* once when builing the link table.  Note that the calloc() initializes */
   /* the flags to FALSE (as the queue is initially empty).                 */
   inqueue = (int *)calloc(minutiae->num, sizeof(int));
   if(inqueue == (int *)NULL){
      free(link_table);
      free(x_axis);
      free(y_axis);
      free(queue);
      fprintf(stderr, "ERROR : create_link_table : calloc : inqueue\n");
      return(-334);
   }
   /* Initialize head and tail to start of queue. */
   head = 0;
   tail = 0;

   /* Push the index of the "main" manutia point onto the queue. */
   queue[tail++] = start;
   /* Set "main" minutia inqueue flag to TRUE. */
   inqueue[start] = TRUE;

   print2log("BUILD TABLE:\n");

   /* While the queue is NOT empty ... */
   while(head != tail){
      /* Pop the next manutia point from the queue and refer to it as */
      /* the primary (first) minutia.                                 */
      first = queue[head++];
      minutia1 = minutiae->list[first];

      /* Look for those minutia points that potentially match the   */
      /* "first" minutia and add them to the link table.  These     */
      /* potentially matching minutia are secondary and refered to  */
      /* as the "second" minutia.  Always restart the search at the */
      /* original "main" manutia.                                   */
      second = start+1;

      /* While secondary manutae remain to be matched to the current */
      /* first minutia...                                            */
      while(second < minutiae->num){
         /* Assign second minutia to temporary pointer. */
         minutia2 = minutiae->list[second];

         print2log("1:%d(%d,%d)%d 2:%d(%d,%d)%d ",
                   first, minutia1->x, minutia1->y, minutia1->type,
                   second, minutia2->x, minutia2->y, minutia2->type);

         /* 1. If y-delta from second to first minutia is small (ex. */
         /*    <= 20 pixels) ...                                     */
         if((minutia2->y - minutia1->y) <= lfsparms->max_link_dist){

            print2log("1DY ");


            /* 2. If first and second minutia are not the same point ... */
            /*    (Remeber that the search for matching seconds starts   */
            /*    all the way back to the starting "main" minutia.)      */
            if(first != second){

               print2log("2NE ");

               /* 3. If first and second minutia are the same type ...    */
               if(minutia1->type == minutia2->type){

                  print2log("3TP ");

                  /* 4. If |x-delta| between minutiae is small (ex. <= */
                  /*    20 pixels) ...                                 */
                  if(abs(minutia1->x - minutia2->x) <=
                         lfsparms->max_link_dist){

                     print2log("4DX ");

                     /* 5. If second minutia is NOT on a small loop ... */
                     if(!onloop[second]){

                        print2log("5NL ");

                        /* The second minutia is ridge-ending OR a */
                        /* bifurcation NOT to be skipped ...       */

                        /* Compute the "inner" distance between the */
                        /* first and second minutia's directions.   */
                        deltadir = closest_dir_dist(minutia1->direction,
                                       minutia2->direction, full_ndirs);
                        /* If the resulting direction is INVALID           */
                        /* (this should never happen, but just in case)... */
                        if(deltadir == INVALID_DIR){
                           free(link_table);
                           free(x_axis);
                           free(y_axis);
                           free(queue);
                           free(inqueue);
                           /* Then there is a problem. */
                           fprintf(stderr,
                           "ERROR : create_link_table : INVALID direction\n");
                           return(-335);
                        }

                        /* Compute first minutia's block coords from */
                        /* its pixel coords.                         */
                        xblk = minutia1->x/lfsparms->blocksize;
                        yblk = minutia1->y/lfsparms->blocksize;
                        /* Get corresponding block's NMAP value. */
                        /*    -3 == NO_VALID_NBRS                */
                        /*    -2 == HIGH_CURVATURE               */
                        /*    -1 == INVALID_DIR                  */
                        /*     0 <= VALID_DIR                    */
                        nmapval = *(nmap+(yblk*mw)+xblk);
                        /* 6. CASE I: If block has VALID_DIR and deltadir    */
                        /*            relatively close to 180 degrees (at    */
                        /*            least 123.75 deg when ndirs==16)...    */
                        /*    OR                                             */
                        /*    CASE II: If block is HIGH_CURVATURE and        */
                        /*            deltadir is at least 45 degrees...     */
                        if(((nmapval >= 0) &&
                            (deltadir >= low_curve_min_deltadir)) ||
                           ((nmapval == HIGH_CURVATURE) &&
                            (deltadir >= qtr_ndirs))){

                           print2log("6DA ");

                           /* Then compute direction of "joining" vector. */
                           /* First, compute direction of line from first */
                           /* to second minutia points.                   */
                           joindir = line2direction(minutia1->x, minutia1->y,
                                                    minutia2->x, minutia2->y,
                                                    lfsparms->num_directions);

                           /* Comptue opposite direction of first minutia. */
                           opp1dir = (minutia1->direction+
                                      lfsparms->num_directions)%full_ndirs;
                           /* Take "inner" distance on full circle between */
                           /* the first minutia's opposite direction and   */
                           /* the joining direction.                       */
                           joindir = abs(opp1dir - joindir);
                           joindir = min(joindir, full_ndirs - joindir);
                           /* 7. If join angle is <= 90 deg... */
                           if(joindir <= half_ndirs){

                              print2log("7JA ");

                              /* Convert integer join direction to angle  */
                              /* in radians on full circle.  Multiply     */
                              /* direction by (2PI)/32==PI/16 radians per */
                              /* unit direction and you get radians.      */
                              jointheta = joindir *
                                    (M_PI/(double)lfsparms->num_directions);
                              /* Compute squared distance between frist   */
                              /* and second minutia points.               */
                              joindist = distance(minutia1->x, minutia1->y,
                                                  minutia2->x, minutia2->y);
                              /* 8. If the 2 minutia points are close enough */
                              /*    (ex. thresh == 20 pixels)...             */
                              if(joindist <= lfsparms->max_link_dist){

                                 print2log("8JD ");

                                 /* 9. Does a "free path" exist between the */
                                 /*    2 minutia points?                    */
                                 if(free_path(minutia1->x, minutia1->y,
                                              minutia2->x, minutia2->y,
                                              bdata, iw, ih, lfsparms)){

                                    print2log("9FP ");

                                    /* If the join distance is very small,   */
                                    /* join theta will be unreliable, so set */
                                    /* join theta to zero.                   */
                                    /* (ex. thresh == 5 pixels)...           */
                                    if(joindist < lfsparms->min_theta_dist)
                                       /* Set the join theta to zero. */
                                       jointheta = 0.0;
                                    /* Combine the join theta and distance */
                                    /* to compute a link score.            */
                                    dscore = link_score(jointheta, joindist,
                                                       lfsparms);
                                    /* Round off the floating point score.  */
                                    /* Need to truncate so answers are same */
                                    /* on different computers.              */
                                    dscore = trunc_dbl_precision(dscore,
                                                             TRUNC_SCALE);
                                    iscore = sround(dscore);
                                    /* Add minutia pair and their link score */
                                    /* to the Link Table.                    */

                                    if(iscore > 0){
                                       print2log("UPDATE");

                                       if((ret = update_link_table(link_table,
                                          x_axis, y_axis, &nx_axis, &ny_axis,
                                          &n_entries, tbldim,
                                          queue, &head, &tail, inqueue, 
                                          first, second, iscore))){
                                             /* If update ERROR, deallocate */
                                             /* working memories.           */
                                             free(link_table);
                                             free(x_axis);
                                             free(y_axis);
                                             free(queue);
                                             free(inqueue);
                                             return(ret);
                                       }
                                    } /* Else score is <= 0, so skip second. */
                                 } /* 9. Else no free path, so skip second. */
                              } /* 8. Else joindist too big, so skip second. */
                           } /* 7. Else joindir is too big, so skip second. */
                        } /* 6. Else INVALID DIR or deltadir too small, */
                          /* so skip second.                         */
                     } /* 5. Else second minutia on small loop, so skip it. */
                  } /* 4. Else X distance too big, so skip second. */
               } /* 3. Else first and second NOT same type, so skip second. */
            } /* 2. Else first and second ARE same point, so skip second. */

            /* If we get here, we want to advance to the next secondary. */
            second++;

            print2log("\n");


         }
         /* 1. Otherwise, Y distnace too big, so we are done searching for */
         /*    secondary matches to the current frist minutia.  It is time */
         /*    to take the next minutia in the queue and begin matching    */
         /*    secondaries to it.                                          */
         else{

            print2log("\n");

            /* So, break out of the secondary minutiae while loop. */
            break;
         }
      }
      /* Done matching current first minutia to secondaries. */
   }
   /* Get here when queue is empty, and we have our complete link table. */

   /* Deallocate working memories. */
   free(queue);
   free(inqueue);

   /* Assign link table buffers and attributes to output pointers. */
   *olink_table = link_table;
   *ox_axis = x_axis;
   *oy_axis = y_axis;
   *onx_axis = nx_axis;
   *ony_axis = ny_axis;
   *on_entries = n_entries;

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: update_link_table - Takes the indices of 2 minutia and their link
#cat:                    compatibility score and updates the 2D link table.
#cat:                    The input minutia are registered to positions along
#cat:                    different axes, if they are not already in the table,
#cat:                    and a queue is maintained so that a cluster of
#cat:                    potentially linked points may be gathered.

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      queue      - list of clustered minutiae yet to be used to locate
                   other compatible minutiae
      head       - head of the queue
      tail       - tail of the queue
      inqueue    - flag for each minutia point in minutiae list to signify if
                   it has been clustered with the points in this current link
                   table
      first      - index position of first minutia of current link pair
      second     - index position of second minutia of current link pair
      score      - degree of link compatibility of current link pair
   Output:
      link_table - updated sparse 2D table containing scores of potentially
                   linked minutia pairs
      x_axis     - updated minutia IDs registered along x-axis
      y_axis     - updated minutia IDs registered along y-axis
      nx_axis    - updated number of minutia registered along x-axis
      ny_axis    - updated number of minutia registered along y-axis
      n_entries  - updated number of scores currently entered in the table
      queue      - updated list of clustered minutiae yet to be used to locate
                   other compatible minutiae
      tail       - updated tail of the queue
      inqueue    - updated list of flags, one for each minutia point in
                   minutiae list to signify if it has been clustered with
                   the points in this current link table
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int update_link_table(int *link_table, int *x_axis, int *y_axis,
                 int *nx_axis, int *ny_axis, int *n_entries, const int tbldim,
                 int *queue, int *head, int *tail, int *inqueue,
                 const int first, const int second, const int score)
{
   int x, y, *tptr;

   /* If the link table is empty. */
   if(*n_entries == 0){
      /* Add first and second minutia to the table. */

      /* If horizontal axis of table is full ... */
      if(*nx_axis >= tbldim)
         /* Then ignore the minutia pair and return normally. */
         return(0);
      /* Add first minutia to horizontal axis. */
      x_axis[*nx_axis] = first;

      /* If vertical axis of table is full ... */
      if(*ny_axis >= tbldim)
         /* Then ignore the minutia pair and return normally. */
         return(0);
      /* Add second minutia to vertical axis. */
      y_axis[*ny_axis] = second;

      /* Enter minutia pair score to the link table. */
      tptr = link_table + ((*ny_axis)*tbldim) + (*nx_axis);
      *tptr = score;
      (*n_entries)++;

      /* Bump number of entries in each axis. */
      (*nx_axis)++;
      (*ny_axis)++;

      /* Add second minutia to queue (if not already in queue), so it */
      /* can be processed to see who might link to it.                */
      if(!inqueue[second]){
         queue[*tail] = second;
         (*tail)++;
         inqueue[second] = TRUE;
      }

      /* Done, so return normally. */
      return(0);
   }

   /* We are filling in the table with a "faimily" or "cluster" of  */
   /* potentially inter-linked points.  Once the first entry is     */
   /* made in the table, all subsequent updates will be based on    */
   /* at least the first minutia already being in the table.        */

   /* If first minutia already stored in horizontal axis */
   /* of the link table.                                 */
   if((x = in_int_list(first, x_axis, *nx_axis)) >= 0){
      /* If second minutia already stored in vertical axis */
      /* of the link table.                                */
      if((y = in_int_list(second, y_axis, *ny_axis)) >= 0){
         /* Entry may not be set or the new score may be larger. */
         tptr = link_table + (y*tbldim) + x;
         if(*tptr == 0)
            /* Assign the minutia pair score to the table. */
            *tptr = score;

      }
      /* Otherwise, second minutia not in vertical axis of link table. */
      else{
         /* Add the second minutia to the vertical axis and */
         /* the minutia pair's score to the link table.     */

         /* If vertical axis of table is full ... */
         if(*ny_axis >= tbldim)
            /* Then ignore the minutia pair and return normally. */
            return(0);
         /* Add second minutia to vertical axis. */
         y_axis[*ny_axis] = second;

         /* Enter minutia pair score to the link table. */
         tptr = link_table + ((*ny_axis)*tbldim) + x;
         *tptr = score;
         (*n_entries)++;

         /* Bump number of entries in vertical axis. */
         (*ny_axis)++;

         /* Add second minutia to queue (if not already in queue), so it */
         /* can be processed to see who might link to it.                */
         if(!inqueue[second]){
            queue[*tail] = second;
            (*tail)++;
            inqueue[second] = TRUE;
         }
      }
   }
   /* Otherwise, first minutia not in horizontal axis of link table. */
   else{
      /* If first minutia already stored in vertical axis */
      /* of the link table.                               */
      if((y = in_int_list(first, y_axis, *ny_axis)) >= 0){
         /* If second minutia already stored in horizontal axis */
         /* of the link table.                                  */
         if((x = in_int_list(second, x_axis, *nx_axis)) >= 0){
            /* Entry may not be set or the new score may be larger. */
            tptr = link_table + (y*tbldim) + x;
            if(*tptr == 0)
               /* Assign the minutia pair score to the table. */
               *tptr = score;
         }
         /* Otherwise, second minutia not in horizontal axis of link table. */
         else{
            /* Add the second minutia to the horizontal axis and */
            /* the minutia pair's score to the link table.       */

            /* If horizontal axis of table is full ... */
            if(*nx_axis >= tbldim)
               /* Then ignore the minutia pair and return normally. */
               return(0);
            /* Add second minutia to vertical axis. */
            x_axis[*nx_axis] = second;

            /* Enter minutia pair score to the link table. */
            tptr = link_table + (y*tbldim) + (*nx_axis);
            *tptr = score;
            (*n_entries)++;

            /* Bump number of entries in horizontal axis. */
            (*nx_axis)++;

            /* Add second minutia to queue (if not already in queue), so it */
            /* can be processed to see who might link to it.                */
            if(!inqueue[second]){
               queue[*tail] = second;
               (*tail)++;
               inqueue[second] = TRUE;
            }
         }
      }
      /* Otherwise, first minutia not in vertical or horizontal axis of */
      /* link table.  This is an error, as this should only happen upon */
      /* the first point being entered, which is already handled above. */
      else{
         fprintf(stderr,
            "ERROR : update_link_table : first minutia not found in table\n");
         return(-340);
      }
   }

   /* Done, so return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: order_link_table - Puts the link table in sorted order based on x and 
#cat:                    then y-axis entries.  These minutia are sorted based
#cat:                    on their point of perpendicular intersection with a
#cat:                    line running from the origin at an angle equal to the
#cat:                    average direction of all entries in the link table.

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      minutiae   - list of minutia
      ndirs      - number of IMAP directions (in semicircle)
   Output:
      link_table - sorted sparse 2D table containing scores of potentially
                   linked minutia pairs
      x_axis     - sorted minutia IDs registered along x-axis
      y_axis     - sorted minutia IDs registered along y-axis
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int order_link_table(int *link_table, int *x_axis, int *y_axis,
                const int nx_axis, const int ny_axis, const int n_entries,
                const int tbldim, const MINUTIAE *minutiae,
                const int ndirs)
{
   int i, j, ret, sumdir, avrdir, *order;
   double davrdir, avrtheta, pi_factor, cs, sn;
   double *dlist;
   MINUTIA *minutia;
   int *tlink_table, *tx_axis, *ty_axis, tblalloc;
   int *toptr, *frptr;

   /* If the table is empty or if there is only one horizontal or */
   /* vertical entry in the  table ...                            */
   if((nx_axis <= 1) || (ny_axis <= 1))
      /* Then don't reorder the table, just return normally. */
      return(0);

   /* Compute average direction (on semi-circle) of all minutia entered */
   /* in the link table.  This gives an average ridge-flow direction    */
   /* among all the potentially linked minutiae in the table.            */

   /* Initialize direction accumulator to 0. */
   sumdir = 0;

   /* Accumulate directions (on semi-circle) of minutia entered in the */
   /* horizontal axis of the link table.                               */
   for(i = 0; i < nx_axis; i++)
      sumdir += (minutiae->list[x_axis[i]]->direction % ndirs);

   /* Accumulate directions of minutia entered in the vertical axis */
   /* of the link table.                                            */
   for(i = 0; i < ny_axis; i++)
      sumdir += (minutiae->list[y_axis[i]]->direction % ndirs);

   /* Compute the average direction and round off to integer. */
   davrdir = (sumdir / (double)(nx_axis + ny_axis));
   /* Need to truncate precision so that answers are consistent */
   /* on different computer architectures when rounding doubles. */
   davrdir = trunc_dbl_precision(davrdir, TRUNC_SCALE);
   avrdir = sround(davrdir);

   /* Conversion factor from integer directions to radians. */
   pi_factor = M_PI / (double)ndirs;

   /* Compute sine and cosine of average direction in radians. */
   avrtheta = avrdir*pi_factor;
   sn = sin(avrtheta);
   cs = cos(avrtheta);

   /* Allocate list to hold distances to be sorted on. */
   dlist = (double *)malloc(tbldim * sizeof(double));
   if(dlist == (double *)NULL){
      fprintf(stderr, "ERROR : order_link_table : malloc : dlist\n");
      return(-350);
   }

   /* Allocate and initialize temporary link table buffers. */
   tblalloc = tbldim * tbldim;
   tlink_table = (int *)calloc(tblalloc, sizeof(int));
   if(tlink_table == (int *)NULL){
      free(dlist);
      fprintf(stderr, "ERROR : order_link_table : calloc : tlink_table\n");
      return(-351);
   }
   tx_axis = (int *)malloc(tbldim * sizeof(int));
   if(tx_axis == (int *)NULL){
      free(dlist);
      free(tlink_table);
      fprintf(stderr, "ERROR : order_link_table : malloc : tx_axis\n");
      return(-352);
   }
   ty_axis = (int *)malloc(tbldim * sizeof(int));
   if(ty_axis == (int *)NULL){
      free(dlist);
      free(tlink_table);
      free(tx_axis);
      fprintf(stderr, "ERROR : order_link_table : malloc : ty_axis\n");
      return(-353);
   }

   /* Compute distance measures for each minutia entered in the  */
   /* horizontal axis of the link table.                         */
   /* The measure is: dist = X*cos(avrtheta) + Y*sin(avrtheta)   */
   /* which measures the distance from the origin along the line */
   /* at angle "avrtheta" to the point of perpendicular inter-   */
   /* section from the point (X,Y).                              */

   /* Foreach minutia in horizontal axis of the link table ... */
   for(i = 0; i < nx_axis; i++){
      minutia = minutiae->list[x_axis[i]];
      dlist[i] = (minutia->x * cs) + (minutia->y * sn);
      /* Need to truncate precision so that answers are consistent */
      /* on different computer architectures when rounding doubles. */
      dlist[i] = trunc_dbl_precision(dlist[i], TRUNC_SCALE);
   }

   /* Get sorted order of distance for minutiae in horizontal axis. */
   if((ret = sort_indices_double_inc(&order, dlist, nx_axis))){
      free(dlist);
      return(ret);
   }

   /* Store entries on y_axis into temporary list. */
   memcpy(ty_axis, y_axis, ny_axis * sizeof(int));

   /* For each horizontal entry in link table ... */
   for(i = 0; i < nx_axis; i++){
      /* Store next minutia in sorted order to temporary x_axis. */
      tx_axis[i] = x_axis[order[i]];
      /* Store corresponding column of scores into temporary table. */
      frptr = link_table + order[i];
      toptr = tlink_table + i;
      for(j = 0; j < ny_axis; j++){
         *toptr = *frptr;
         toptr += tbldim;
         frptr += tbldim;
      }
   }

   /* Deallocate sorted order of distance measures. */
   free(order);

   /* Compute distance measures for each minutia entered in the  */
   /* vertical axis of the temporary link table (already sorted  */
   /* based on its horizontal axis entries.                      */

   /* Foreach minutia in vertical axis of the link table ... */
   for(i = 0; i < ny_axis; i++){
      minutia = minutiae->list[y_axis[i]];
      dlist[i] = (minutia->x * cs) + (minutia->y * sn);
      /* Need to truncate precision so that answers are consistent */
      /* on different computer architectures when rounding doubles. */
      dlist[i] = trunc_dbl_precision(dlist[i], TRUNC_SCALE);
   }

   /* Get sorted order of distance for minutiae in vertical axis. */
   if((ret = sort_indices_double_inc(&order, dlist, ny_axis))){
      free(dlist);
      return(ret);
   }

   /* Store entries in temporary x_axis. */
   memcpy(x_axis, tx_axis, nx_axis * sizeof(int));

   /* For each vertical entry in the temporary link table ... */
   for(i = 0; i < ny_axis; i++){
      /* Store next minutia in sorted order to y_axis. */
      y_axis[i] = ty_axis[order[i]];
      /* Store corresponding row of scores into link table. */
      frptr = tlink_table + (order[i] * tbldim);
      toptr = link_table + (i * tbldim);
      for(j = 0; j < nx_axis; j++){
         *toptr++ = *frptr++;
      }
   }

   /* Deallocate sorted order of distance measures. */
   free(order);

   /* Link table is now sorted on x and y axes. */
   /* Deallocate the working memories. */
   free(dlist);
   free(tlink_table);
   free(tx_axis);
   free(ty_axis);

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: process_link_table - Processes the link table deciding which minutia
#cat:                      pairs in the table should be linked (ie. joined in
#cat:                      the image and removed from the minutiae list (and
#cat:                      from onloop).

   Input:
      link_table - sparse 2D table containing scores of potentially linked
                   minutia pairs
      x_axis     - minutia IDs registered along x-axis
      y_axis     - minutia IDs registered along y-axis
      nx_axis    - number of minutia registered along x-axis
      ny_axis    - number of minutia registered along y-axis
      n_entries  - number of scores currently entered in the table
      tbldim     - dimension of each axes of the link table
      minutiae   - list of minutia
      onloop     - list of flags signifying which minutia lie on small lakes
      bdata      - binary image data (0==while & 1==black)
      iw         - width (in pixels) of image
      ih         - height (in pixels) of image
      lfsparms   - parameters and thresholds for controlling LFS
   Output:
      minutiae   - list of pruned minutiae
      onloop     - updated loop flags
      bdata      - edited image with minutia features joined
   Return Code:
      Zero      - successful completion
      Negative  - system error
**************************************************************************/
int process_link_table(const int *link_table,
                 const int *x_axis, const int *y_axis,
                 const int nx_axis, const int ny_axis, const int n_entries,
                 const int tbldim, MINUTIAE *minutiae, int *onloop,
                 unsigned char *bdata, const int iw, const int ih,
                 const LFSPARMS *lfsparms)
{
   int i, j, ret, first, second;
   MINUTIA *minutia1, *minutia2;
   int rm1, rm2;
   int *to_remove;
   int n_lines, line_len, entry_incr, line_incr;
   int line_i, entry_i;
   int start, end;
   int max_v, max_tbl_i, max_line_i, max_x, max_y;


   print2log("LINKING FROM TABLE:\n");

   /* If link table is empty, just return normally. */
   if(n_entries == 0)
      return(0);

   /* If there is only 1 entry in the table, then join the minutia pair. */
   if(n_entries == 1){
      /* Join the minutia pair in the image. */
      first = x_axis[0];
      second = y_axis[0];
      minutia1 = minutiae->list[first];
      minutia2 = minutiae->list[second];

      /* Connect the points with a line with specified radius (ex. 1 pixel). */
      if((ret = join_minutia(minutia1, minutia2, bdata, iw, ih,
                            WITH_BOUNDARY, lfsparms->join_line_radius)))
         return(ret);

      /* Need to remove minutiae from onloop first, as onloop is dependent */
      /* on the length of the minutiae list.  We also need to remove the   */
      /* highest index from the lists first or the indices will be off.   */
      if(first > second){
         rm1 = first;
         rm2 = second;
      }
      else{
         rm1 = second;
         rm2 = first;
      }
      if((ret = remove_from_int_list(rm1, onloop, minutiae->num)))
         return(ret);
      if((ret = remove_from_int_list(rm2, onloop, minutiae->num-1)))
         return(ret);

      /* Now, remove the minutia from the minutiae list. */
      if((ret = remove_minutia(rm1, minutiae)))
         return(ret);
      if((ret = remove_minutia(rm2, minutiae)))
         return(ret);

      /* Return normally. */
      return(0);
   }

   /* Otherwise, we need to make decisions as to who links to who. */

   /* Allocate list of minutia indices that upon completion of linking */
   /* should be removed from the onloop and minutiae lists.  Note: That */
   /* using "calloc" initializes the list to FALSE.                    */
   to_remove = (int *)calloc(minutiae->num, sizeof(int));
   if(to_remove == (int *)NULL){
      fprintf(stderr, "process_link_table : calloc : to_remove\n");
      return(-360);
   }

   /* If the number of horizontal entries is <= vertical entries ... */
   if(nx_axis <= ny_axis){
      /* Process columns in table as lines. */
      n_lines = nx_axis;
      /* Set length of each line to number of vertical entries. */
      line_len = ny_axis;
      /* Increment down column to next entry in line. */
      entry_incr = tbldim;
      /* Increment across row to next line. */
      line_incr = 1;
   }
   /* Otherwise, the number of vertical entreis < horizontal entries ... */
   else{
      /* Process rows in table as lines. */
      n_lines = ny_axis;
      /* Set length of each line to number of horizontal entries. */
      line_len = nx_axis;
      /* Increment across row to next entry in line. */
      entry_incr = 1;
      /* Increment down column to next line. */
      line_incr = tbldim;
   }

   /* Set start of next line index to origin of link table. */
   line_i = 0;

   /* Initialize the search limits for the line ... */
   start = 0;
   end = line_len - n_lines + 1;

   /* Foreach line in table ... */
   for(i = 0; i < n_lines; i++){

      /* Find max score in the line given current search limits. */

      /* Set table entry index to start of next line. */
      entry_i = line_i;

      /* Initialize running maximum with score in current line */
      /* at offset 'start'.                                    */
      entry_i += (start*entry_incr);

      /* Set running maximum score. */
      max_v = link_table[entry_i];
      /* Set table index of maximum score. */
      max_tbl_i = entry_i;
      /* Set line index of maximum score. */
      max_line_i = start;

      /* Advance table entry index along line. */
      entry_i += entry_incr;
      /* Foreach successive entry in line up to line index 'end' ... */

      for(j = start+1; j < end; j++){
         /* If current entry > maximum score seen so far ... */
         if(link_table[entry_i] >= max_v){
            /* Store current entry as new maximum. */
            max_v = link_table[entry_i];
            max_tbl_i = entry_i;
            max_line_i = j;
         }
         /* Advance table entry index along line. */
         entry_i += entry_incr;
      }

      /* Convert entry index at maximum to table row and column indices. */
      max_x = max_tbl_i % tbldim;
      max_y = max_tbl_i / tbldim;

      /* Set indices and pointers corresponding to minutia pair */
      /* with maximum score this pass.                          */
      first = x_axis[max_x];
      second = y_axis[max_y];
      minutia1 = minutiae->list[first];
      minutia2 = minutiae->list[second];

      /* Check to make sure the the maximum score found in the current */
      /* line is > 0 (just to be safe)  AND                            */
      /* If a "free path" exists between minutia pair ...              */
      if( /* (max_v >0) && */
         free_path(minutia1->x, minutia1->y, minutia2->x, minutia2->y,
                   bdata, iw, ih, lfsparms)){

         print2log("%d,%d to %d,%d LINK\n",
                    minutia1->x, minutia1->y, minutia2->x, minutia2->y);

         /* Join the minutia pair in the image. */
         if((ret = join_minutia(minutia1, minutia2, bdata, iw, ih,
                               WITH_BOUNDARY, lfsparms->join_line_radius))){
            free(to_remove);
            return(ret);
         }

         /* Set remove flags for minutia pair.  A minutia point may  */
         /* be linked to more than one other minutia in this process */
         /* so, just flag them to be removed for now and actually    */
         /* conduct the removal after all linking is complete.       */
         to_remove[first] = TRUE;
         to_remove[second] = TRUE;

      }
      /* Set starting line index to one passed maximum found this pass. */
      start = max_line_i + 1;
      /* Bump ending line index. */
      end++;

      /* Advance start of line index to next line. */
      line_i += line_incr;

   } /* End for lines */

   /* Now that all linking from the current table is complete,     */
   /* remove any linked minutia from the onloop and minutiae lists. */
   /* NOTE: Need to remove the minutia from their lists in reverse */
   /*       order, otherwise, indices will be off.                 */
   for(i = minutiae->num-1; i >= 0; i--){
      /* If the current minutia index is flagged for removal ... */
      if(to_remove[i]){
         /* Remove the minutia from the onloop list before removing  */
         /* from minutiae list as the length of onloop depends on the */
         /* length of the minutiae list.                              */
         if((ret = remove_from_int_list(i, onloop, minutiae->num))){
            free(to_remove);
            return(ret);
         }
         /* Now, remove the minutia from the minutiae list. */
         if((ret = remove_minutia(i, minutiae))){
            free(to_remove);
            return(ret);
         }
      }
   }

   /* Deallocate remove list. */
   free(to_remove);

   /* Return normally. */
   return(0);
}

/*************************************************************************
**************************************************************************
#cat: link_score - Takes 2 parameters, a 'join angle' and a 'join distance'
#cat:              computed between 2 minutia and combines these to compute
#cat:              a score representing the degree of link compatibility
#cat:              between the 2 minutiae.

   Input:
      jointheta - angle measured between 2 minutiae
      joindist  - distance between 2 minutiae
      lfsparms  - parameters and thresholds for controlling LFS
   Return Code:
      Score     - degree of link compatibility
**************************************************************************/
double link_score(const double jointheta, const double joindist,
                  const LFSPARMS *lfsparms)
{
   double score, theta_factor, dist_factor;

   /* Calculates a weighted score between the distance between the 1st &   */
   /* 2nd features and the delta angle between the reflected 1st feature   */
   /* and the "joining" vector to the 2nd feature.                         */
   /*                                                                      */
   /*                       SCORE_NUMERATOR                                */
   /* SCORE =  ------------------------------------------                  */
   /*                  THETA_FACTOR * DIST_FACTOR                          */
   /*                                                                      */
   /* THETA_FACTOR = exp((jointheta/SCORE_THETA_NORM)^2)                   */
   /* DIST_FACTOR  = (1+exp(((joindist/SCORE_DIST_NORM)-1)                 */
   /*                        *SCORE_DIST_WEIGHT))                          */
   /*                                                                      */
   /* For example:                                                         */
   /*          SCORE_NUMERATOR   = 32000.0                                 */
   /*          SCORE_THETA_NORM  =   15.0*(PI/180)                         */
   /*          SCORE_DIST_NORM   =   10.0                                  */
   /*          SCORE_DIST_WEIGHT =    4.0                                  */
   /*                                                                      */
   /*                                     3200.0                           */
   /* SCORE =  ----------------------------------------------------------- */
   /*          exp((jointheta/15.0)^2) * (1+exp(((joindist/10.0)-1.0)*4.0))*/
   /*                                                                      */
   /*         In this case, increases in distance drops off faster than    */
   /*         in theta.                                                    */

   /* Compute the THETA_FACTOR. */
   theta_factor = jointheta / (lfsparms->score_theta_norm * DEG2RAD);
   theta_factor = exp(theta_factor * theta_factor);
   /* Compute the DIST_FACTOR. */
   dist_factor = 1.0+exp(((joindist/lfsparms->score_dist_norm)-1.0) *
                 lfsparms->score_dist_weight);

   /* Compute the floating point score. */
   score = lfsparms->score_numerator / (theta_factor * dist_factor);

   /* Return the floating point score. */
   return(score);
}

