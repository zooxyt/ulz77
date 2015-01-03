/* ulz77c -- Unusable LZ77 Compressor
 * Copyright(C) 2013-2014 Chery Natsu 

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* The program demonstrates several methods with the interface provided by 
 * libulz77 */

/* Visual Studio CRT functions warnings */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "argsparse.h"
#include "ulz77.h"

#define _VERSION_ "0.0.2"

int show_version(void)
{
    const char *version_info =
        "Unusable LZ77 Compressor -- " _VERSION_ "\n"
        "For demonstrating libulz77 (Unusable LZ77 Library)\n"
        "Copyright(C) 2013-2014 Cheryl Natsu\n";
    puts(version_info);
    return 0;
}

int show_help(void)
{
    const char *help_info = 
        "usage : ulz77 [-options]\n\n"
        "  --method   <method>       Specify interface of compression library\n"
        "    [stream|file]\n"
        "  -c         <sourcefile>   Input file\n"
        "  -o         <destfile>     Output file\n"
        "  -bs        <blocksize>    Specify block size of stream\n"
        "\n"
        "  --help                    Show help info\n"
        "  --version                 Show version info\n";
    
    show_version();
    puts(help_info);
    return 0;
}

#define ULZ77C_MODE_COMPRESSION 0
#define ULZ77C_MODE_DECOMPRESSION 1
#define ULZ77C_METHOD_STREAM 0
#define ULZ77C_METHOD_FILE 1

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 4096
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

int ulz77_stream_compress(char *filename_dst, char *filename_src, size_t bs)
{
    int ret = 0;
    struct ulz77_stream *stream = NULL;
    FILE *fp_src = NULL, *fp_dst = NULL;
    long fp_src_len = 0;
    unsigned char *buffer = NULL;
    long remain_size, task_size;

    /* Create stream */
    stream = ulz77_stream_new();
    if (stream == NULL)
    {
        ret = -ULZ77_ERR_MALLOC;
        goto fail;
    }

    /* Open source file */
    fp_src = fopen(filename_src, "rb");
    if (fp_src == NULL)
    {
        ret = -ULZ77_ERR_FILE_OPEN;
        goto fail;
    }

    /* Open destination file */
    fp_dst = fopen(filename_dst, "wb+");
    if (fp_dst == NULL)
    {
        ret = -ULZ77_ERR_FILE_OPEN;
        goto fail;
    }

    /* Set write file pointer */
    ret = ulz77_stream_set_writer_fp(stream, fp_dst);
    if (ret != 0)
    {
        goto fail;
    }

    /* Get length of source file */
    fseek(fp_src, 0, SEEK_END);
    fp_src_len = ftell(fp_src);
    fseek(fp_src, 0, SEEK_SET);
    remain_size = fp_src_len;

    buffer = (unsigned char *)malloc(sizeof(unsigned char) * bs);
    if (buffer == NULL)
    {
        ret = -ULZ77_ERR_MALLOC;
        goto fail;
    }

    while (remain_size != 0)
    {
        task_size = MIN(remain_size, 1024 * 1024 * 1);
        if (fread(buffer, task_size, 1, fp_src) < 1)
        {
            ret = -ULZ77_ERR_FILE_READ;
            goto fail;
        }
        ulz77_stream_push(stream, buffer, task_size);

        remain_size -= task_size;
    }

    ret = 0;
fail:
    if (stream != NULL) ulz77_stream_destroy(stream);
    if (fp_src != NULL) fclose(fp_src);
    if (fp_dst != NULL) fclose(fp_dst);
    if (buffer != NULL) free(buffer);
    return ret;
}

int ulz77_stream_decompress(char *filename_dst, char *filename_src)
{
    int ret = 0;
    struct ulz77_stream *stream = NULL;
    FILE *fp_src = NULL, *fp_dst = NULL;
    long fp_src_len = 0;
    unsigned char *dst = NULL;
    size_t dst_len;
    long remain_size;

    /* Create stream */
    stream = ulz77_stream_new();
    if (stream == NULL)
    {
        ret = -ULZ77_ERR_MALLOC;
        goto fail;
    }

    /* Open source file */
    fp_src = fopen(filename_src, "rb");
    if (fp_src == NULL)
    {
        ret = -ULZ77_ERR_FILE_OPEN;
        goto fail;
    }

    /* Open destination file */
    fp_dst = fopen(filename_dst, "wb+");
    if (fp_dst == NULL)
    {
        ret = -ULZ77_ERR_FILE_OPEN;
        goto fail;
    }

    /* Set write file pointer */
    ret = ulz77_stream_set_reader_fp(stream, fp_src);
    if (ret != 0)
    {
        goto fail;
    }

    /* Get length of source file */
    fseek(fp_src, 0, SEEK_END);
    fp_src_len = ftell(fp_src);
    fseek(fp_src, 0, SEEK_SET);
    remain_size = fp_src_len;

    while (remain_size != 0)
    {
        ret = ulz77_stream_pull(stream, &dst, &dst_len);
        if (ret != 0)
        {
            goto fail;
        }
        fwrite(dst, dst_len, 1, fp_dst);
        free(dst);
        remain_size -= stream->reader_count;
    }

    ret = 0;
fail:
    if (stream != NULL) ulz77_stream_destroy(stream);
    if (fp_src != NULL) fclose(fp_src);
    if (fp_dst != NULL) fclose(fp_dst);
    return ret;
}

int main(int argc, const char *argv[])
{
    int ret = 0;
    int mode = 0;
    /*int method = ULZ77C_METHOD_STREAM;*/
    int method = ULZ77C_METHOD_FILE;
    char *src_file = NULL;
    char *dst_file = NULL;
    size_t bs = 1024 * 1024 * 1;  /* 1M */

    /* Argument Parser */
    int arg_idx;
    char *arg_p;
    argsparse_init(&arg_idx);

    /* Parse arguments */
    while (argsparse_request(argc, argv, &arg_idx, &arg_p) == 0)
    {
        if (!strcmp(arg_p, "--version"))
        {
            show_version();
            goto done;
        }
        else if (!strcmp(arg_p, "--help"))
        {
            show_help();
            goto done;
        }
        else if (!strcmp(arg_p, "-c"))
        {
            if (argsparse_request(argc, argv, &arg_idx, &arg_p) != 0)
            {
                fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
                goto fail;
            }
            src_file = arg_p;
            mode = ULZ77C_MODE_COMPRESSION;
        }
        else if (!strcmp(arg_p, "-d"))
        {
            if (argsparse_request(argc, argv, &arg_idx, &arg_p) != 0)
            {
                fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
                goto fail;
            }
            src_file = arg_p;
            mode = ULZ77C_MODE_DECOMPRESSION;
        }
        else if (!strcmp(arg_p, "-o"))
        {
            if (argsparse_request(argc, argv, &arg_idx, &arg_p) != 0)
            {
                fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
                goto fail;
            }
            dst_file = arg_p;
        }
        else if (!strcmp(arg_p, "--method"))
        {
            if (argsparse_request(argc, argv, &arg_idx, &arg_p) != 0)
            {
                fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
                goto fail;
            }
            if (!strcmp(arg_p, "stream"))
            {
                method = ULZ77C_METHOD_STREAM;
            }
            else if (!strcmp(arg_p, "file"))
            {
                method = ULZ77C_METHOD_FILE;
            }
            else
            {
                fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
                goto fail;
            }
        }
        else
        {
            fprintf(stderr, "Error : Invalid argument\n"); ret = 0;
            goto fail;
        }
    }

    if (src_file == NULL)
    {
        fprintf(stderr, "Error : Not specified source file\n"); ret = 0;
        goto fail;
    }

    if (dst_file == NULL)
    {
        fprintf(stderr, "Error : Not specified destination file\n"); ret = 0;
        goto fail;
    }

    if (mode == ULZ77C_MODE_COMPRESSION)
    {
        if (method == ULZ77C_METHOD_FILE)
        {
            ret = ulz77_compress_file(dst_file, src_file);
        }
        else
        {
            ret = ulz77_stream_compress(dst_file, src_file, bs);
        }
    }
    else if (mode == ULZ77C_MODE_DECOMPRESSION)
    {
        if (method == ULZ77C_METHOD_FILE)
        {
            ret = ulz77_decompress_file(dst_file, src_file);
        }
        else
        {
            ret = ulz77_stream_decompress(dst_file, src_file);
        }
    }
    if (ret != 0) goto fail;

    goto done;
fail:
    if (ret != 0)
    {
        ulz77_error_description_print(ret);
    }
done:
    return 0;
}

