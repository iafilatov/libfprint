#!/bin/sh -e

help()
{
        echo "Usage:"
        echo "         `basename $0` [NBIS directory]"
        exit
}

replace_global()
{
	if [ ! -f "$2" ] ; then
		FILES=`find -name *.[ch]`
	else
		FILES=$2
	fi
	sed -i "s/$1/g_$1/" $FILES
}

rename_variable()
{
	if [ ! -f "$3" ] ; then
		FILES=`find -name *.[ch]`
	else
		FILES=$3
	fi
	sed -i "s/$1/$2/" $FILES
}

remove_function()
{
	if [ ! -f "$2" ] ; then
		FILES=`find -name *.[ch]`
	else
		FILES=$2
	fi
	./remove-function.lua $1 $FILES
}

if [ $# -ne 1 ] ; then echo "*** Wrong number of arguments ***" ; help ; fi
if [ ! -d $1 ] ; then echo "*** $1 not a directory ***" ; help ; fi

DIR="$1"

if [ ! -d $DIR/bozorth3 ] ; then echo "*** $DIR not an NBIS source directory ***" ; help ; fi
if [ ! -d $DIR/mindtct ] ; then echo "*** $DIR not an NBIS source directory ***" ; help ; fi

# Update files
for i in bozorth3/*.c ; do
	cp -a $DIR/bozorth3/src/lib/bozorth3/`basename $i` bozorth3/
	chmod 0644 bozorth3/`basename $i`
done

for i in mindtct/*.c chaincod.c getmin.c link.c xytreps.c; do
	cp -a $DIR/mindtct/src/lib/mindtct/`basename $i` mindtct/
	chmod 0644 mindtct/`basename $i`
done

for i in include/*.h mytime.h ; do
	FILE=`basename $i`
	ORIG=`find $DIR -name $FILE | grep -v misc/ | grep -v exports/`

	cp -a -f $ORIG include/
	chmod 0644 include/$FILE
done

# Replace global variables
replace_global dft_coefs include/lfs.h
replace_global dft_coefs mindtct/globals.c
replace_global dft_coefs mindtct/maps.c
replace_global dft_coefs mindtct/detect.c
# Also does lfsparms_V2
replace_global lfsparms include/lfs.h
replace_global lfsparms mindtct/globals.c
replace_global nbr8_dx
replace_global nbr8_dy
replace_global chaincodes_nbr8
replace_global feature_patterns
rename_variable errorfp stderr

# Remove command-line options globals
for i in m1_xyt max_minutiae min_computable_minutiae verbose_bozorth verbose_main verbose_load; do
	sed -i "/$i/d" include/bozorth.h
done
rename_variable max_minutiae DEFAULT_BOZORTH_MINUTIAE
rename_variable m1_xyt 0
rename_variable min_computable_minutiae MIN_COMPUTABLE_BOZORTH_MINUTIAE
rename_variable verbose_bozorth 0
rename_variable verbose_main 0
rename_variable verbose_load 0
# Remove logging globals
for i in logfp avrdir dir_strength nvalid ; do
	sed -i "/$i/d" mindtct/log.c
	sed -i "/$i/d" include/log.h
done
# Remove an unused static variable
sed -i "/stack_pointer = stack/d" bozorth3/bz_sort.c
sed -i "/stack\[BZ_STACKSIZE\]/d" bozorth3/bz_sort.c

# +extern int verbose_load;
#  extern int verbose_threshold;
# +/* Global supporting error reporting */
# +extern FILE *stderr;

# Remove unused functions
patch -p0 < lfs.h.patch
remove_function binarize mindtct/binar.c
remove_function binarize_image mindtct/binar.c
remove_function isobinarize mindtct/binar.c
remove_function lfs_detect_minutiae mindtct/detect.c
remove_function bits_6to8 mindtct/imgutil.c
remove_function bozorth_main bozorth3/bz_drvrs.c
remove_function bz_load bozorth3/bz_io.c
remove_function bz_prune bozorth3/bz_io.c
remove_function get_next_file bozorth3/bz_io.c
remove_function get_score_filename bozorth3/bz_io.c
remove_function get_score_line bozorth3/bz_io.c
remove_function parse_line_range bozorth3/bz_io.c
remove_function set_gallery_filename bozorth3/bz_io.c
remove_function set_probe_filename bozorth3/bz_io.c
remove_function set_progname bozorth3/bz_io.c
remove_function detect_minutiae mindtct/minutia.c
remove_function dump_minutiae_pts mindtct/minutia.c
remove_function dump_minutiae mindtct/minutia.c
remove_function dump_reliable_minutiae_pts mindtct/minutia.c
remove_function scan4minutiae mindtct/minutia.c
remove_function scan4minutiae_horizontally mindtct/minutia.c
remove_function scan4minutiae_vertically mindtct/minutia.c
remove_function rescan4minutiae_horizontally mindtct/minutia.c
remove_function rescan4minutiae_vertically mindtct/minutia.c
remove_function process_horizontal_scan_minutia mindtct/minutia.c
remove_function process_vertical_scan_minutia mindtct/minutia.c
remove_function get_nbr_block_index mindtct/minutia.c
remove_function join_minutia mindtct/minutia.c
remove_function rescan_partial_horizontally mindtct/minutia.c
remove_function rescan_partial_vertically mindtct/minutia.c
remove_function adjust_high_curvature_minutia mindtct/minutia.c
remove_function adjust_horizontal_rescan mindtct/minutia.c
remove_function adjust_vertical_rescan mindtct/minutia.c
remove_function dump_shape mindtct/shape.c
remove_function flood_loop mindtct/loop.c
remove_function get_loop_list mindtct/loop.c
remove_function process_loop mindtct/loop.c
remove_function gen_imap mindtct/maps.c
remove_function gen_nmap mindtct/maps.c
remove_function gen_initial_imap mindtct/maps.c
remove_function smooth_imap mindtct/maps.c
remove_function get_max_padding mindtct/init.c
remove_function lfs2m1_minutia_XYT mindtct/xytreps.c
remove_function lfs2nist_format mindtct/xytreps.c
remove_function malloc_or_exit bozorth3/bz_alloc.c
remove_function malloc_or_return_error bozorth3/bz_alloc.c
remove_function reliability_fr_quality_map mindtct/quality.c
remove_function remove_false_minutia mindtct/remove.c
remove_function remove_pores mindtct/remove.c
remove_function remove_pointing_invblock mindtct/remove.c
remove_function remove_hooks_islands_lakes_overlaps mindtct/remove.c
remove_function remove_near_invblock mindtct/remove.c
remove_function remove_or_adjust_side_minutiae mindtct/remove.c
remove_function sort_quality_decreasing bozorth3/bz_sort.c
remove_function sort_order_decreasing bozorth3/bz_sort.c
remove_function qsort_decreasing bozorth3/bz_sort.c
remove_function partition_dec bozorth3/bz_sort.c
remove_function popstack bozorth3/bz_sort.c
remove_function pushstack bozorth3/bz_sort.c
remove_function select_pivot bozorth3/bz_sort.c
remove_function sort_indices_double_inc mindtct/sort.c
remove_function link_minutiae mindtct/link.c
remove_function create_link_table mindtct/link.c
remove_function order_link_table mindtct/link.c
remove_function process_link_table mindtct/link.c
remove_function update_link_table mindtct/link.c
remove_function link_score mindtct/link.c
remove_function remove_from_int_list mindtct/util.c

# Remove trailing spaces
sed -i 's/[ \t]*$//' `find -name "*.[ch]"`

# Remove usebsd.h
sed -i '/usebsd.h/d' `find -name "*.[ch]"`

