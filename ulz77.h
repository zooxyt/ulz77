/* libulz77 -- Unusable LZ77 Library
 * Copyright(C) 2013-2014 Chery Natsu 

 * This file is part of libulz77

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

#ifndef _ULZ77_H_
#define _ULZ77_H_

#include <stdio.h>

/*******************
 *  Return Values  *
 *******************/

enum 
{
    ULZ77_ERR_UNKNOWN = 1,
    ULZ77_ERR_NULL_PTR = 2,
    ULZ77_ERR_INVALID_ARGS = 3,
    ULZ77_ERR_MALLOC = 4,
    ULZ77_ERR_FILE_OPEN = 5,
    ULZ77_ERR_FILE_READ = 6,
    ULZ77_ERR_FILE_WRITE = 7,
    ULZ77_ERR_BUFFER_FULL = 8,
    ULZ77_ERR_UNKNOWN_OP = 9,
    ULZ77_ERR_INVALID_WRITER = 10,
    ULZ77_ERR_UNKNOWN_WRITER = 11,
    ULZ77_ERR_INVALID_READER = 12,
    ULZ77_ERR_UNKNOWN_READER = 13,
    ULZ77_ERR_NARROW_BUFFER_SIZE = 14,
};

/* Buffer */
#define ULZ77_BUFFER_RESERVED_SIZE 10 /* 10 Bytes = Last 3 Sentinels's length at worst situation */

/* Hash */
#define ULZ77_HASH_LITERAL_SIZE (3) /* length of literal used to compute hash (bytes) */
#define ULZ77_HASH_SIZE_BIT (17) /* hash size (bit) */
#define ULZ77_HASH_SIZE (1<<(ULZ77_HASH_SIZE_BIT)) /* hash size (bit) */
#define ULZ77_HASH_MASK ((1<<(ULZ77_HASH_SIZE_BIT))-1) /* hash size (bit) */
/* Compute hash */
#define ULZ77_HASH(x) ((x)&ULZ77_HASH_MASK)

/* Match Chain */
#define ULZ77_RECENT_POS_SIZE (ULZ77_HASH_LITERAL_SIZE + ULZ77_MATCH_CHAIN_SIZE)
#define ULZ77_MATCH_CHAIN_SIZE (10)
/*#define USE_MATCH_CHAIN (0)*/


/************************************************
 *  Data Structures of Buffer Ring and Encoder  *
 ************************************************/

struct buffer_ring
{
	unsigned char *buf; /* ring body */

    /* old <-- recent_pos[2], recent_pos[1], recent_pos_[0], pos --> new */
	unsigned int recent_pos[ULZ77_RECENT_POS_SIZE]; /* position in the ring when added */
	unsigned int pos; /* current position in the ring */

	unsigned int grow; /* size of growing ring */
	unsigned int size; /* ring size */
	int second_pass; /* is the ring buffer grown to the top size? */

    unsigned int absolute_pos; /* compressed data length */

	/* linked list part : 4 tables for fast pattern-matching */

    /* CAUTION: first and final table store ABSOLUTE POSITION */
    /* after hash applied, first and final table should able to contain hash size (1 << HASH_SIZE_BIT) of data */
	int *first_table; /* point to the slot in offset table which stores the first position appeared in ring */
	int *final_table; /* point to the slot in offset table which stores the final position appeared in ring */

	int *hash_jump_next_table; /* next position of the same hash result as the one in current pos in jump table */
	int *hash_jump_prev_table; /* prev position of the same hash result as the one in current pos in jump table */
#if defined(USE_MATCH_CHAIN)
    int *match_jump_next_table[ULZ77_MATCH_CHAIN_SIZE]; /* next position of the same match result as the one in current pos in jump table */
    int *match_jump_prev_table[ULZ77_MATCH_CHAIN_SIZE]; /* next position of the same match result as the one in current pos in jump table */
#endif

	int *offset_table; /* offset of specified jump table slot */
};

/* Encoder used both in Compression and Decompression */
struct ulz77_encoder
{
    struct buffer_ring br;

    unsigned int future_bytes;
    unsigned int last_bytes;

    unsigned char *src_p_interrupted; /* keep last position when interrupted */

    size_t src_len; /* number of input data of one turn */
    size_t dst_len; /* number of output data of one turn */
    size_t src_total_len; /* number of input data of total */
    size_t dst_total_len; /* number of output data of total */
};


/*************************
 *  Low-Level Interface  *
 *************************/

/* Create new encoder */
struct ulz77_encoder *ulz77_encoder_new(void);

/* Destroy encoder */
int ulz77_encoder_destroy(struct ulz77_encoder *enc);

/* Encode data */
int ulz77_encoder_encode(struct ulz77_encoder *enc, unsigned char *dst, size_t dst_buffer_size, unsigned char *src, size_t len);

/* Decode data */
int ulz77_encoder_decode(struct ulz77_encoder *enc, unsigned char *dst, size_t dst_buffer_size, unsigned char *src, size_t len);

/* Get Previous position of src */
unsigned char *ulz77_encoder_get_previous(struct ulz77_encoder *enc);

/**************************
 *  High-Level Interface  *
 **************************/

/* Compress data */
int ulz77_compress_data(unsigned char **dst_out, size_t *dst_out_len, unsigned char *src, size_t src_len);

/* Decompress data */
int ulz77_decompress_data(unsigned char **dst_out, size_t *dst_out_len, unsigned char *src, size_t src_len);

/* Compress file */
int ulz77_compress_file(const char *filename_dst, const char *filename_src);

/* Decompress file */
int ulz77_decompress_file(const char *filename_dst, const char *filename_src);

/**********************
 *  Stream Interface  *
 **********************/

struct ulz77_stream
{
    /* Writer */
    int writer_type;
    FILE *writer_fp;
    int (*writer_cb)(unsigned char *data, size_t size);

    /* Reader */
    int reader_type;
    FILE *reader_fp;
    int (*reader_cb)(unsigned char *data, size_t size);
    size_t reader_count;
    size_t reader_total_count;
};

/* Create a new stream */
struct ulz77_stream *ulz77_stream_new(void);

/* Destroy a stream */
int ulz77_stream_destroy(struct ulz77_stream *stream);


/* Stream writer Null */
int ulz77_stream_set_writer_null(struct ulz77_stream *stream);

/* Stream writer File Pointer */
int ulz77_stream_set_writer_fp(struct ulz77_stream *stream, FILE *fp);

/* Stream writer Callback */
int ulz77_stream_set_writer_callback(struct ulz77_stream *stream, int (*writer_cb)(unsigned char *data, size_t size));

/* Push data into stream */
int ulz77_stream_push(struct ulz77_stream *stream, unsigned char *data, size_t size);

/* Stream reader Null */
int ulz77_stream_set_reader_null(struct ulz77_stream *stream);

/* Stream reader File Pointer */
int ulz77_stream_set_reader_fp(struct ulz77_stream *stream, FILE *fp);

/* Stream reader Callback */
int ulz77_stream_set_reader_callback(struct ulz77_stream *stream, int (*readr_cb)(unsigned char *data, size_t size));

/* Pull data from stream */
int ulz77_stream_pull(struct ulz77_stream *stream, unsigned char **data, size_t *size);

/***********
 *  Error  *
 ***********/

/* Copy Error description */
int ulz77_error_description_cpy(char *buf, size_t buf_len, int err_no);

/* Print Error description */
int ulz77_error_description_print(int err_no);

#endif

