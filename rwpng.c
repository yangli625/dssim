/*---------------------------------------------------------------------------

   pngquant:  RGBA -> RGBA-palette quantization program             rwpng.c

  ---------------------------------------------------------------------------

      Copyright (c) 1998-2000 Greg Roelofs.  All rights reserved.

      This software is provided "as is," without warranty of any kind,
      express or implied.  In no event shall the author or contributors
      be held liable for any damages arising in any way from the use of
      this software.

      Permission is granted to anyone to use this software for any purpose,
      including commercial applications, and to alter it and redistribute
      it freely, subject to the following restrictions:

      1. Redistributions of source code must retain the above copyright
         notice, disclaimer, and this list of conditions.
      2. Redistributions in binary form must reproduce the above copyright
         notice, disclaimer, and this list of conditions in the documenta-
         tion and/or other materials provided with the distribution.
      3. All advertising materials mentioning features or use of this
         software must display the following acknowledgment:

            This product includes software developed by Greg Roelofs
            and contributors for the book, "PNG: The Definitive Guide,"
            published by O'Reilly and Associates.

  ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>

#include "png.h"        /* libpng header; includes zlib.h */
#include "rwpng.h"      /* typedefs, common macros, public prototypes */

static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg);


void rwpng_version_info(void)
{
    fprintf(stderr, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, png_get_header_ver(NULL));
    fprintf(stderr, "   Compiled with zlib %s; using zlib %s.\n",
      ZLIB_VERSION, zlib_version);
}



/*
   retval:
     0 = success
    21 = bad sig
    22 = bad IHDR
    24 = insufficient memory
    25 = libpng error (via longjmp())
    26 = wrong PNG color type (no alpha channel)
 */

int rwpng_read_image(FILE *infile, read_info *mainprog_ptr)
{
    png_structp  png_ptr = NULL;
    png_infop    info_ptr = NULL;
    png_uint_32  i, rowbytes;
    int          color_type, bit_depth;
    unsigned char sig[8];


    /* first do a quick check that the file really is a PNG image; could
     * have used slightly more general png_sig_cmp() function instead */

    fread(sig, 1, 8, infile);
    if (png_sig_cmp(sig, 0, 8)) {
        return BAD_SIGNATURE_ERROR;   /* bad signature */
    }


    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      NULL, NULL);
    if (!png_ptr) {
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }


    /* GRR TO DO:  use end_info struct */
    /* we could create a second info struct here (end_info), but it's only
     * useful if we want to keep pre- and post-IDAT chunk info separated
     * (mainly for PNG-aware image editors and converters) */


    /* setjmp() must be called in every function that calls a non-trivial
     * libpng function */

    jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return LIBPNG_FATAL_ERROR;   /* fatal libpng error (via longjmp()) */
    }


    png_init_io(png_ptr, infile);
    png_set_sig_bytes(png_ptr, 8);  /* we already read the 8 signature bytes */

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */


    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

    png_get_IHDR(png_ptr, info_ptr, &mainprog_ptr->width, &mainprog_ptr->height,
      &bit_depth, &color_type, &mainprog_ptr->interlaced, NULL, NULL);


    /* expand palette images to RGB, low-bit-depth grayscale images to 8 bits,
     * transparency chunks to full alpha channel; strip 16-bit-per-sample
     * images to 8 bits per sample; and convert grayscale to RGB[A] */

    /* GRR TO DO:  handle each of GA, RGB, RGBA without conversion to RGBA */
    /* GRR TO DO:  allow sub-8-bit quantization? */
    /* GRR TO DO:  preserve all safe-to-copy ancillary PNG chunks */
    /* GRR TO DO:  get and map background color? */

    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
#ifdef PNG_READ_FILLER_SUPPORTED
        /* GRP:  expand palette to RGB, and grayscale or RGB to GA or RGBA */
        if (color_type == PNG_COLOR_TYPE_PALETTE)
            png_set_expand(png_ptr);
        png_set_filler(png_ptr, 65535L, PNG_FILLER_AFTER);
#else
        fprintf(stderr, "pngquant readpng:  image is neither RGBA nor GA\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        mainprog_ptr->retval = 26;
        return mainprog_ptr->retval;
#endif
    }
/*
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_expand(png_ptr);
 */
    /* GRR TO DO:  handle 16-bps data natively? */
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);
    /* GRR TO DO:  probably want to handle this separately, without expansion */
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);


    /* get and save the gamma info (if any) for writing */

    if (!png_get_gAMA(png_ptr, info_ptr, &mainprog_ptr->gamma)) {
        mainprog_ptr->gamma = 0.45455;
    }

    /* all transformations have been registered; now update info_ptr data,
     * get rowbytes and channels, and allocate image memory */

    png_read_update_info(png_ptr, info_ptr);

    mainprog_ptr->rowbytes = rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    //mainprog_ptr->channels = (int)png_get_channels(png_ptr, info_ptr);

    if ((mainprog_ptr->rgba_data = malloc(rowbytes*mainprog_ptr->height)) == NULL) {
        fprintf(stderr, "pngquant readpng:  unable to allocate image data\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;
    }
    if ((mainprog_ptr->row_pointers = (png_bytepp)malloc(mainprog_ptr->height*sizeof(png_bytep))) == NULL) {
        fprintf(stderr, "pngquant readpng:  unable to allocate row pointers\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        free(mainprog_ptr->rgba_data);
        mainprog_ptr->rgba_data = NULL;
        return PNG_OUT_OF_MEMORY_ERROR;
    }

    /* set the individual row_pointers to point at the correct offsets */

    for (i = 0;  i < mainprog_ptr->height;  ++i)
        mainprog_ptr->row_pointers[i] = mainprog_ptr->rgba_data + i*rowbytes;


    /* now we can go ahead and just read the whole image */

    png_read_image(png_ptr, (png_bytepp)mainprog_ptr->row_pointers);


    /* and we're done!  (png_read_end() can be omitted if no processing of
     * post-IDAT text/time/etc. is desired) */

    png_read_end(png_ptr, NULL);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return SUCCESS;
}





/*
   retval:
     0 = success
    34 = insufficient memory
    35 = libpng error (via longjmp())
 */

int rwpng_write_image_init(FILE *outfile, write_info *mainprog_ptr)
{
    png_structp png_ptr;       /* note:  temporary variables! */
    png_infop info_ptr;


    /* could also replace libpng warning-handler (final NULL), but no need: */

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      NULL, NULL);
    if (!png_ptr) {
        return INIT_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }
    mainprog_ptr->png_ptr = png_ptr;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        return INIT_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }


    /* setjmp() must be called in every function that calls a PNG-writing
     * libpng function, unless an alternate error handler was installed--
     * but compatible error handlers must either use longjmp() themselves
     * (as in this program) or exit immediately, so here we go: */

    jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return LIBPNG_INIT_ERROR;   /* libpng error (via longjmp()) */
    }


    /* make sure outfile is (re)opened in BINARY mode */

    png_init_io(png_ptr, outfile);


    /* set the compression levels--in general, always want to leave filtering
     * turned on (except for palette images) and allow all of the filters,
     * which is the default; want 32K zlib window, unless entire image buffer
     * is 16K or smaller (unknown here)--also the default; usually want max
     * compression (NOT the default); and remaining compression flags should
     * be left alone */

    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
/*
    >> this is default for no filtering; Z_FILTERED is default otherwise:
    png_set_compression_strategy(png_ptr, Z_DEFAULT_STRATEGY);
    >> these are all defaults:
    png_set_compression_mem_level(png_ptr, 8);
    png_set_compression_window_bits(png_ptr, 15);
    png_set_compression_method(png_ptr, 8);
 */


    /* set the image parameters appropriately */

    int sample_depth;
    if (mainprog_ptr->num_palette <= 2)
        sample_depth = 1;
    else if (mainprog_ptr->num_palette <= 4)
        sample_depth = 2;
    else if (mainprog_ptr->num_palette <= 16)
        sample_depth = 4;
    else
        sample_depth = 8;

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
      sample_depth, PNG_COLOR_TYPE_PALETTE,
      mainprog_ptr->interlaced, PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_DEFAULT);

    /* GRR WARNING:  cast of rwpng_colorp to png_colorp could fail in future
     * major revisions of libpng (but png_ptr/info_ptr will fail, regardless) */
    png_set_PLTE(png_ptr, info_ptr, &mainprog_ptr->palette[0], mainprog_ptr->num_palette);

    if (mainprog_ptr->num_trans > 0)
        png_set_tRNS(png_ptr, info_ptr, mainprog_ptr->trans, mainprog_ptr->num_trans, NULL);

    if (mainprog_ptr->gamma > 0.0)
        png_set_gAMA(png_ptr, info_ptr, mainprog_ptr->gamma);

#if 0
    if (mainprog_ptr->have_bg) {   /* we know it's RGBA, not gray+alpha */
        png_color_16  background;

        background.red = mainprog_ptr->bg_red;
        background.green = mainprog_ptr->bg_green;
        background.blue = mainprog_ptr->bg_blue;
        png_set_bKGD(png_ptr, info_ptr, &background);
    }

    if (mainprog_ptr->have_time) {
        png_time  modtime;

        png_convert_from_time_t(&modtime, mainprog_ptr->modtime);
        png_set_tIME(png_ptr, info_ptr, &modtime);
    }

    if (mainprog_ptr->have_text) {
        png_text  text[6];
        int  num_text = 0;

        if (mainprog_ptr->have_text & TEXT_TITLE) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Title";
            text[num_text].text = mainprog_ptr->title;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_AUTHOR) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Author";
            text[num_text].text = mainprog_ptr->author;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_DESC) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Description";
            text[num_text].text = mainprog_ptr->desc;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_COPY) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "Copyright";
            text[num_text].text = mainprog_ptr->copyright;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_EMAIL) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "E-mail";
            text[num_text].text = mainprog_ptr->email;
            ++num_text;
        }
        if (mainprog_ptr->have_text & TEXT_URL) {
            text[num_text].compression = PNG_TEXT_COMPRESSION_NONE;
            text[num_text].key = "URL";
            text[num_text].text = mainprog_ptr->url;
            ++num_text;
        }
        png_set_text(png_ptr, info_ptr, text, num_text);
    }
#endif /* 0 */


    /* write all chunks up to (but not including) first IDAT */

    png_write_info(png_ptr, info_ptr);


    /* if we wanted to write any more text info *after* the image data, we
     * would set up text struct(s) here and call png_set_text() again, with
     * just the new data; png_set_tIME() could also go here, but it would
     * have no effect since we already called it above (only one tIME chunk
     * allowed) */


    /* set up the transformations:  for now, just pack low-bit-depth pixels
     * into bytes (one, two or four pixels per byte) */

    png_set_packing(png_ptr);
/*  png_set_shift(png_ptr, &sig_bit);  to scale low-bit-depth values */


    /* make sure we save our pointers for use in writepng_encode_image() */

    mainprog_ptr->png_ptr = png_ptr;
    mainprog_ptr->info_ptr = info_ptr;


    /* OK, that's all we need to do for now; return happy */
    return SUCCESS;
}






/* returns 0 for success, 45 for libpng (longjmp) problem */

int rwpng_write_image_whole(write_info *mainprog_ptr)
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;

    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return LIBPNG_WRITE_WHOLE_ERROR; /* libpng error (via longjmp()) */
    }


    /* and now we just write the whole image; libpng takes care of interlacing
     * for us */

    png_write_image(png_ptr, mainprog_ptr->row_pointers);


    /* since that's it, we also close out the end of the PNG file now--if we
     * had any text or time info to write after the IDATs, second argument
     * would be info_ptr, but we optimize slightly by sending NULL pointer: */

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    mainprog_ptr->png_ptr = NULL;
    mainprog_ptr->info_ptr = NULL;

    return SUCCESS;
}





/* this routine is called only for non-interlaced images */
/* returns 0 if succeeds, 55 if libpng problem */

int rwpng_write_image_row(write_info *mainprog_ptr)
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;


    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return LIBPNG_WRITE_ERROR;   /* libpng error (via longjmp()) */
    }


    /* indexed_data points at our one row of indexed data */

    png_write_row(png_ptr, mainprog_ptr->indexed_data);

    return SUCCESS;
}





/* this routine is called only after rwpng_write_image_row() */
/* returns 0 if succeeds, 65 if libpng problem */

int rwpng_write_image_finish(write_info *mainprog_ptr)
{
    png_structp png_ptr = (png_structp)mainprog_ptr->png_ptr;
    png_infop info_ptr = (png_infop)mainprog_ptr->info_ptr;


    /* as always, setjmp() must be called in every function that calls a
     * PNG-writing libpng function */

    jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        mainprog_ptr->png_ptr = NULL;
        mainprog_ptr->info_ptr = NULL;
        return LIBPNG_WRITE_ERROR;   /* libpng error (via longjmp()) */
    }


    /* close out PNG file; if we had any text or time info to write after
     * the IDATs, second argument would be info_ptr */
    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);
    mainprog_ptr->png_ptr = NULL;
    mainprog_ptr->info_ptr = NULL;

    return SUCCESS;
}
