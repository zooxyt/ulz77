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

/* Visual Studio CRT functions warnings */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "ulz77.h"

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

/* Compiler related functions */
#ifdef __builtin_expect
#define expect(expr, value) (__builtin_expect((expr),(value)))
#else
#define expect(expr, value) (expr)
#endif

/***************************************************************************
 * A matched pattern will be encoded into at least 3 bytes
 *
 * 8 bits     4 bits           12 bits            7 bits          1 bit
 * sentinel + matched length + matched position + (extra length + extra sig)
 *
 * SENTINEL + 3 + 0 means source data is a SENTINEL
 *
 * Matched length  Encoded value
 * 3               reserved for sentinel
 * 4               1
 * 17              14
 * 18              15, 1 | 0(no extra)
 * 
 ***************************************************************************/

#define BUFFER_SIZE 4096 /* Size of ring buffer */
#define MATCH_LEN_MAX (15 + 3) /* Match longer than this value should be put 
                                  into extra bytes */
#define MATCH_LEN_MIN (4)
#define SENTINEL 255

/* Return the true position in ring with specified offset 
 * relative to the beginning of ring */
#define BUFCVT_FROM_RELATIVE(idx_relative, br) \
    ((idx_relative)+(br->second_pass?\
        (br->pos+(((idx_relative)<(signed int)((br->size)-(br->pos)))?\
                  (0)\
                  :(-(signed int)(br->size))))\
        :(0)))

#define RELATIVE_TO_ABSOLUTE(idx_relative, br) \
    (((br->absolute_pos)-(br->grow))+(idx_relative))

#define ABSOLUTE_TO_RELATIVE(idx_absolute, br) \
    (idx_absolute-(br->absolute_pos-br->grow))

#define NO_WHERE (-1) /* for jump table, indicates no where to jump */
#define LITERAL_SIZE (256)

/* initialize ring buffer data structure */
static int buffer_ring_init(struct buffer_ring *br, unsigned int size)
{
#if defined(USE_MATCH_CHAIN)
    int chain_idx;
#endif
    unsigned int i;

    /* Clean pointers */
    br->buf = NULL;
    br->first_table = br->final_table = br->hash_jump_next_table = NULL;
    /* Allocate body for ring */
    br->buf = (unsigned char *)malloc(sizeof(unsigned char) * size);
    if (br->buf == NULL) goto fail;
    /* Basic settings */
    for (i = 0; i != ULZ77_RECENT_POS_SIZE; i++)
        br->recent_pos[i] = 0; /* Recent positions when added */
    br->pos = 0; 
    br->grow = 0;
    br->size = size;
    br->second_pass = 0;
    br->absolute_pos = 0;
#if defined(USE_MATCH_CHAIN)
    for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
    {
        br->match_jump_next_table[chain_idx] = NULL;
        br->match_jump_prev_table[chain_idx] = NULL;
    }
#endif
    /* Allocate space for 3 tables */
    br->first_table = (int *)malloc(sizeof(int) * (ULZ77_HASH_SIZE));
    if (br->first_table == NULL) goto fail;
    br->final_table = (int *)malloc(sizeof(int) * (ULZ77_HASH_SIZE));
    if (br->final_table == NULL) goto fail;
    br->hash_jump_next_table = (int *)malloc(sizeof(int) * size);
    if (br->hash_jump_next_table == NULL) goto fail;
    br->hash_jump_prev_table = (int *)malloc(sizeof(int) * size);
    if (br->hash_jump_prev_table == NULL) goto fail;
#if defined(USE_MATCH_CHAIN)
    for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
    {
        br->match_jump_next_table[chain_idx] = (int *)malloc(sizeof(int) * size);
        if (br->match_jump_next_table[chain_idx] == NULL) goto fail;
        br->match_jump_prev_table[chain_idx] = (int *)malloc(sizeof(int) * size);
        if (br->match_jump_prev_table[chain_idx] == NULL) goto fail;
    }
#endif
    br->offset_table = (int *)malloc(sizeof(int) * size);
    if (br->offset_table == NULL) goto fail;
    for (i = 0; i < (ULZ77_HASH_SIZE); i++)
    {
        br->first_table[i] = NO_WHERE;
        br->final_table[i] = NO_WHERE;
    }
    for (i = 0; i < size; i++)
    {
        br->hash_jump_next_table[i] = NO_WHERE;
        br->hash_jump_prev_table[i] = NO_WHERE;
#if defined(USE_MATCH_CHAIN)
        for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
        {
            br->match_jump_next_table[chain_idx][i] = NO_WHERE;
            br->match_jump_prev_table[chain_idx][i] = NO_WHERE;
        }
#endif
        br->offset_table[i] = 0;
    }
    for (i = 0; i < ULZ77_RECENT_POS_SIZE; i++)
    {
        br->recent_pos[i] = 0;
    }
    return 0;
fail:
    if (br->buf) free(br->buf);
    if (br->first_table) free(br->first_table);
    if (br->final_table) free(br->final_table);
    if (br->hash_jump_next_table) free(br->hash_jump_next_table);
    if (br->hash_jump_prev_table) free(br->hash_jump_prev_table);
#if defined(USE_MATCH_CHAIN)
    for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
    {
        if (br->match_jump_next_table[chain_idx] != NULL) 
            free(br->match_jump_next_table[chain_idx]);
        if (br->match_jump_prev_table[chain_idx] != NULL) 
            free(br->match_jump_prev_table[chain_idx]);
    }
#endif
    if (br->offset_table) free(br->offset_table);
    return -1;
}

/* Get the data in ring buffer with specified relative offset */
static __inline unsigned char buffer_ring_get_from_relative(struct buffer_ring *br, int idx)
{
    return br->buf[BUFCVT_FROM_RELATIVE(idx, br)];
}

/* Get the data in ring buffer with specified slot */
/*
static __inline unsigned char buffer_ring_get_from_slot(struct buffer_ring *br, int slot)
{
    return buffer_ring_get_from_relative(br, ABSOLUTE_TO_RELATIVE(br->offset_table[slot], br));
}
*/


/* Append symbol to the tail of ring buffer */
static int buffer_ring_append(struct buffer_ring *br, unsigned char symbol)
{
#if defined(USE_MATCH_CHAIN)
    int chain_idx;
#endif
    int i, j;
    unsigned int hash_value;
    int hash_literal_idx;
    int next_pos;

    /* Oldest symbol will be erased, so update its first_table value via jump table */
    if (br->second_pass)
    {
        hash_value = 0;
        hash_literal_idx = ULZ77_HASH_LITERAL_SIZE;
        while (hash_literal_idx > 0)
        {
            hash_value = (hash_value << 8) | buffer_ring_get_from_relative(br, ULZ77_HASH_LITERAL_SIZE - hash_literal_idx);
            hash_literal_idx--;
        }
        hash_value &= ULZ77_HASH_MASK;
        next_pos = br->hash_jump_next_table[BUFCVT_FROM_RELATIVE(0, br)];
        if (next_pos != NO_WHERE)
        {
            br->hash_jump_prev_table[next_pos] = NO_WHERE;
            br->first_table[hash_value] = next_pos;
        }
        else
        {
            br->first_table[hash_value] = NO_WHERE;
            br->final_table[hash_value] = NO_WHERE;
        }
    }

    /* Append symbol into ring */
    br->buf[br->pos] = symbol;
    br->hash_jump_next_table[br->pos] = NO_WHERE; /* The symbol is new (and the last one) here, so no one is in my next */

#if defined(USE_MATCH_CHAIN)
    for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
    {
        br->match_jump_next_table[chain_idx][br->pos] = NO_WHERE;
        br->match_jump_prev_table[chain_idx][br->pos] = NO_WHERE;
    }
#endif

    br->offset_table[br->pos] = br->absolute_pos; /* The symbol is new (and the last one) here, so no one is in my next */

    i = ULZ77_RECENT_POS_SIZE - 1;
    j = ULZ77_RECENT_POS_SIZE - 2;
    while (j >= 0)
    {
        br->recent_pos[i] = br->recent_pos[j];
        i--;j--;
    }
    br->recent_pos[0] = br->pos;

    br->pos++;
    if (br->pos > br->grow) br->grow = br->pos;

    /* jump to head and mark second pass */
    if (br->pos >= br->size)
    {
        br->pos = 0;
        br->second_pass = 1;
    }

    /* update absolute position */
    br->absolute_pos++;

    return 0;
}

static int buffer_ring_update_tables(struct buffer_ring *br, unsigned int hash_value, unsigned int relative_pos)
{
#if defined(USE_MATCH_CHAIN)
    int least_match_final_pos, least_match_cur_pos;
    int chain_upgrade = -1;
    int recent_pos_slot;
    int i;
#endif
    if (br->first_table[hash_value] == NO_WHERE)
    {
        /* this is the first time it came into ring buffer */
        br->first_table[hash_value] = relative_pos;
        br->final_table[hash_value] = relative_pos;
    }
    else
    {
        /* it is already exists in ring buffer */
        /* connect the existent final one to this one */
        br->hash_jump_next_table[br->final_table[hash_value]] = relative_pos;
        br->hash_jump_prev_table[relative_pos] = br->final_table[hash_value];

        /* this one is now the new final one */
        br->final_table[hash_value] = relative_pos;
    }

    /* update least table */
#if defined(USE_MATCH_CHAIN)
    if (br->absolute_pos < RECENT_POS_SIZE)
    {
        return 0;
    }
    least_match_final_pos = ABSOLUTE_TO_RELATIVE(br->offset_table[br->recent_pos[RECENT_POS_SIZE - 1]], br);
    least_match_cur_pos = br->hash_jump_prev_table[least_match_final_pos];
    while (least_match_cur_pos != NO_WHERE)
    {
        if (buffer_ring_get_from_relative(br, ABSOLUTE_TO_RELATIVE(br->offset_table[least_match_cur_pos], br)) ==\
                buffer_ring_get_from_relative(br, ABSOLUTE_TO_RELATIVE(br->offset_table[least_match_final_pos], br)))
        {
            /* 3 chars matched */
            chain_upgrade = 0;
            br->match_jump_next_table[0][least_match_cur_pos] = least_match_final_pos;
            br->match_jump_prev_table[0][least_match_final_pos] = least_match_cur_pos;
            break;
        }
        else
        {
            least_match_cur_pos = br->hash_jump_prev_table[least_match_cur_pos];
        }
    }
    if (chain_upgrade == -1) return 0; /* matched not found */

    /* try 4 and more bytes */
    for (i = 1; i < MATCH_CHAIN_SIZE; i++)
    {
        /* try to link in chain 'i', so search gateway of chain 'i' in chain 'i-1' */
        recent_pos_slot = RECENT_POS_SIZE - 1 - 2 - i;
        least_match_final_pos = ABSOLUTE_TO_RELATIVE(br->offset_table[br->recent_pos[RECENT_POS_SIZE - 1]], br);
        least_match_cur_pos = br->match_jump_prev_table[chain_upgrade][least_match_final_pos];
        while (least_match_cur_pos != NO_WHERE)
        {
            if (buffer_ring_get_from_relative(br, br->recent_pos[recent_pos_slot]) ==\
                    buffer_ring_get_from_relative(br, ABSOLUTE_TO_RELATIVE(br->offset_table[least_match_cur_pos] + 2 + i, br)))
            {
                chain_upgrade = i;
                br->match_jump_next_table[chain_upgrade][least_match_cur_pos] = least_match_final_pos;
                br->match_jump_prev_table[chain_upgrade][least_match_final_pos] = least_match_cur_pos;
                break;
            }
            else
            {
                least_match_cur_pos = br->match_jump_prev_table[chain_upgrade][least_match_cur_pos];
            }
        }
        if (least_match_cur_pos == NO_WHERE) return 0; /* matched not found */
    }
#endif

    return 0;
}

/* return the relative position of founded object */
static int buffer_ring_find(struct buffer_ring *br, /* buffer ring */
        unsigned int hash_value, unsigned char *pat, unsigned char *pat_endp, /* arguments */
        unsigned int *ret_pos, unsigned int *ret_len) /* return values */
{
#if defined(USE_MATCH_CHAIN)
    int chain = -1; /* chain using, -1 for hash chain, 0..9 for match chain */
    int chain_save = -1;
#endif
    int next_point;
    int i; /* jump table slot idx */
    int ret_jump_table_slot;
    int j; /* relative offset */
    ret_jump_table_slot = 0;
    *ret_pos = 0;
    *ret_len = 0;
    if (br->grow == 0) return 0;
    /* locate first char pos */
    if (br->first_table[hash_value] == NO_WHERE) return 0; /* not found */
    else i = br->first_table[hash_value]; /* found */

    for (;;) {
        unsigned char *pat_p = pat;
        unsigned int matched_len;
        matched_len = 0;
        j = br->offset_table[i];
        j = ABSOLUTE_TO_RELATIVE(j, br);

        while (((unsigned int)j < br->grow) && expect((pat_p != pat_endp), 1) &&\
                (br->buf[BUFCVT_FROM_RELATIVE(j, br)] == *pat_p))
        {
            matched_len++;
            pat_p++;
            j++;
        }
        if ((matched_len > *ret_len))
        {
            ret_jump_table_slot = i;
            *ret_len = matched_len;
        }
#if defined(USE_MATCH_CHAIN)
        if (matched_len >= 3)
        {
            chain_save = chain;
            chain = MIN(matched_len - 3, MATCH_CHAIN_SIZE - 1);
        }
        if (chain != -1)
        {
            next_point = br->match_jump_next_table[chain][i];
            if (next_point == NO_WHERE)
            {
                chain = chain_save;
                next_point = br->hash_jump_next_table[i];
            }
        }
        else
        {
            next_point = br->hash_jump_next_table[i];
        }
#else
        next_point = br->hash_jump_next_table[i];
#endif
        if (next_point == NO_WHERE) break;
        else
        {
            i = next_point;
        }
    }
    *ret_pos = ABSOLUTE_TO_RELATIVE(br->offset_table[ret_jump_table_slot], br); /* return relative position ? */
    return 0;
}

int buffer_ring_uninit(struct buffer_ring *br)
{
#if defined(USE_MATCH_CHAIN)
    unsigned int chain_idx;
#endif
    if (br->buf) free(br->buf);
    if (br->first_table) free(br->first_table);
    if (br->final_table) free(br->final_table);
    if (br->hash_jump_next_table) free(br->hash_jump_next_table);
    if (br->hash_jump_prev_table) free(br->hash_jump_prev_table);
#if defined(USE_MATCH_CHAIN)
    for (chain_idx = 0; chain_idx < MATCH_CHAIN_SIZE; chain_idx++)
    {
        if (br->match_jump_next_table[chain_idx] != NULL) free(br->match_jump_next_table[chain_idx]);
        if (br->match_jump_prev_table[chain_idx] != NULL) free(br->match_jump_prev_table[chain_idx]);
    }
#endif
    if (br->offset_table) free(br->offset_table);
    return 0;
}

/* Create new encoder */
struct ulz77_encoder *ulz77_encoder_new(void)
{
    struct ulz77_encoder *enc;

    enc = (struct ulz77_encoder *)malloc(sizeof(struct ulz77_encoder));
    if (enc == NULL) return NULL;
    if (buffer_ring_init(&enc->br, BUFFER_SIZE) != 0)
    {
        free(enc);
        return NULL;
    }
    enc->future_bytes = 0;
    enc->src_p_interrupted = NULL;
    enc->src_len = 0;
    enc->dst_len = 0;
    enc->src_total_len = 0;
    enc->dst_total_len = 0;

    return enc;
}

/* Destroy encoder */
int ulz77_encoder_destroy(struct ulz77_encoder *enc)
{
    buffer_ring_uninit(&enc->br);
    free(enc);
    return 0;
}

/* Encode data */
int ulz77_encoder_encode(struct ulz77_encoder *enc, \
        unsigned char *dst, size_t dst_buffer_size, \
        unsigned char *src, size_t len)
{
    int ret = 0;
    unsigned char *dst_p = dst; /* reserve 4 bytes for block size */
    unsigned int dst_count = 0;
    unsigned int future_bytes = enc->future_bytes;
    unsigned char *src_p = src, *src_endp;
    unsigned int matched_pos, matched_len, matched_len_sub;
    unsigned int i;

    /* push the first 3 bytes */
    if (enc->src_p_interrupted == NULL)
    {
        src_endp = src + MIN(len, 3);
        if (len >= 2)
        {
            future_bytes = (*src_p << 8) | (*(src_p + 1));
        }
        while (src_p != src_endp)
        {
            if (len >= 3)
            {
                future_bytes = (future_bytes << 8) | *(src_p + 2);
            }
            buffer_ring_append(&enc->br, *src_p);
            if (len >= 3)
            {
                buffer_ring_update_tables(&enc->br, ULZ77_HASH(future_bytes), enc->br.recent_pos[0]);
            }
            *dst_p++ = *src_p++;
            dst_count++;
        }
    }

    /* position and length to be copy from the buffer */
    if (len > 3 + 3)
    {
        src_endp = src + len - 3;
        while (src_p != src_endp) 
        {

            /* yield if buffer full */
            if (dst_count >= dst_buffer_size - ULZ77_BUFFER_RESERVED_SIZE)
            {
                enc->src_p_interrupted = src_p;
                enc->future_bytes = future_bytes;
                enc->src_len = src_p - src;
                enc->dst_len = dst_count;
                enc->src_total_len += enc->src_len;
                enc->dst_total_len += enc->dst_len;
                return -ULZ77_ERR_BUFFER_FULL;
            }

            /* find from history */
            buffer_ring_find(&enc->br, ULZ77_HASH((future_bytes << 8) | *(src_p + 2)), src_p, src_endp, &matched_pos, &matched_len);

            /* repeat string in history ring? */
            if (matched_len >= MATCH_LEN_MIN)
            {
                /* reference history */

                matched_len &= ((0x1 << 14) - 1);

                /* basic match part */
                *dst_p++ = SENTINEL;
                *dst_p++ = ((MIN(matched_len - 3, 15) & 0xF) << 4) | ((matched_pos >> 8) & 0xF);
                *dst_p++ = matched_pos & 0xFF;
                dst_count += 3;
                /* extra bytes */
                if (matched_len >= 18)
                {
                    matched_len_sub = matched_len - 17;
                    while (matched_len_sub != 0)
                    {
                        *dst_p++ = (((matched_len_sub >> 7) != 0 ? 1 : 0) << 7) | (matched_len_sub & 127);
                        dst_count++;
                        matched_len_sub >>= 7;
                    }
                }

                /* add symbols into history buffer */
                for (i = 0; i < matched_len; i++) {
                    future_bytes = (future_bytes << 8) | *(src_p + i + 2);
                    buffer_ring_append(&enc->br, *(src_p + i));
                    buffer_ring_update_tables(&enc->br, ULZ77_HASH(future_bytes), enc->br.recent_pos[0]);
                }
                src_p += matched_len;
            }
            else
            {
                future_bytes = (future_bytes << 8) | *(src_p + 2);
                buffer_ring_append(&enc->br, *src_p);
                buffer_ring_update_tables(&enc->br, ULZ77_HASH(future_bytes), enc->br.recent_pos[0]);
                if (*src_p == SENTINEL)
                {
                    *dst_p++ = SENTINEL;
                    *dst_p++ = 0;
                    *dst_p++ = 0;
                    dst_count += 3;
                }
                else
                {
                    *dst_p++ = *src_p;
                    dst_count++;
                }
                src_p++;
            }
        }
    }
    /* Normally encoded the middle part */
    /* push the final 3 bytes */
    src_endp = src + len;
    ret = 0;
    while (src_p != src_endp) 
    {
        if (*src_p == SENTINEL)
        {
            *dst_p++ = SENTINEL;
            *dst_p++ = 0;
            *dst_p++ = 0;
            dst_count += 3;
        }
        else
        {
            *dst_p++ = *src_p;
            dst_count++;
        }
        src_p++;
    }

    enc->src_p_interrupted = NULL;
    enc->future_bytes = future_bytes;
    enc->src_len = src_p - src;
    enc->dst_len = dst_count;
    enc->src_total_len += enc->src_len;
    enc->dst_total_len += enc->dst_len;

    return ret;
}

unsigned char *ulz77_encoder_get_previous(struct ulz77_encoder *enc)
{
    return enc->src_p_interrupted;
}

/* Decode data */
int ulz77_encoder_decode(struct ulz77_encoder *enc, unsigned char *dst, size_t dst_buffer_size, unsigned char *src, size_t len)
{
    unsigned char *dst_p = dst;
    unsigned char *src_p = src, *src_endp = src + len;
    unsigned int dst_count = 0;
    unsigned int matched_pos, matched_len;
    unsigned int i;
    unsigned char ch;
    unsigned int last_bytes = 0;

    if (enc->src_p_interrupted == NULL)
    {
        /* first 3 bytes */
        i = 2;
        while (src_p != src_endp && i > 0)
        {
            last_bytes = (last_bytes << 8) | *(src_p);
            buffer_ring_append(&enc->br, *(src_p));
            /*buffer_ring_update_tables(&enc->br, ULZ77_HASH(last_bytes), enc->br.recent_pos[2]);*/
            *dst_p++ = *src_p++;
            dst_count++;
            i--;
        }
        if (len >= 3)
        {
            last_bytes = (last_bytes << 8) | *(src_p);
            buffer_ring_append(&enc->br, *(src_p));
            buffer_ring_update_tables(&enc->br, ULZ77_HASH(last_bytes), enc->br.recent_pos[2]);
            *dst_p++ = *src_p++;
            dst_count++;
        }
    }

    /* Middle part */
    src_endp = src + len;
    while (src_p != src_endp)
    {
        /* yield if buffer full */
        if (dst_count >= dst_buffer_size - ULZ77_BUFFER_RESERVED_SIZE)
        {
            enc->src_p_interrupted = src_p;
            enc->last_bytes = last_bytes;
            enc->src_len = src_p - src;
            enc->dst_len = dst_count;
            enc->src_total_len += enc->src_len;
            enc->dst_total_len += enc->dst_len;
            return -ULZ77_ERR_BUFFER_FULL;
        }

        if (*src_p == SENTINEL)
        {
            /* matched */
            src_p++;
            matched_len = ((*src_p >> 4) & 0xF) + 3;
            matched_pos = ((*src_p & 0xF) << 8) | (*(src_p + 1));
            src_p += 2;
            if (matched_len == 3 && matched_pos == 0)
            {
                last_bytes = (last_bytes << 8) | SENTINEL;
                buffer_ring_append(&enc->br, SENTINEL);
                buffer_ring_update_tables(&enc->br, ULZ77_HASH(last_bytes), enc->br.recent_pos[2]);
                *dst_p++ = SENTINEL;
                dst_count++;
            }
            else
            {
                if (matched_len == 18)
                {
                    if (((*src_p >> 7) & 0x01) == 0)
                    {
                        matched_len = 17 + (*src_p & 127);
                        src_p++;
                    }
                    else if ((((*src_p >> 7) & 0x01) == 1) &&
                            (src_p + 1 != src_endp) && (((*(src_p + 1) >> 7) & 0x01) == 0))
                    {
                        matched_len = 17 + (((*(src_p + 1) & 127) << 7) | (*src_p & 127));
                        src_p += 2;
                    }
                    else
                    {
                        return -1; /* Unknown error */
                    }
                }
                for (i = 0; i < matched_len; i++)
                {
                    ch = buffer_ring_get_from_relative(&enc->br, matched_pos + i);
                    *(dst_p + i) = ch;
                }
                for (i = 0; i < matched_len; i++)
                {
                    last_bytes = (last_bytes << 8) | *(dst_p + i);
                    buffer_ring_append(&enc->br, *(dst_p + i));
                    buffer_ring_update_tables(&enc->br, ULZ77_HASH(last_bytes), enc->br.recent_pos[2]);
                }
                dst_count += matched_len;
                dst_p += matched_len;
            }
        }
        else
        {
            last_bytes = (last_bytes << 8) | *src_p;
            buffer_ring_append(&enc->br, *src_p);
            buffer_ring_update_tables(&enc->br, ULZ77_HASH(last_bytes), enc->br.recent_pos[2]);
            *dst_p++ = *src_p++;
            dst_count++;
        }
    }

    enc->src_p_interrupted = NULL;
    enc->last_bytes = last_bytes;
    enc->src_len = src_p - src;
    enc->dst_len = dst_count;
    enc->src_total_len += enc->src_len;
    enc->dst_total_len += enc->dst_len;

    return 0;
}

#define ULZ77_TYPE_COMPRESSION 0
#define ULZ77_TYPE_DECOMPRESSION 1

/* Encode data */
int ulz77_encode_data(unsigned char **dst_out, size_t *dst_out_len, unsigned char *src, size_t src_len, int type)
{
    int ret = 0;
    struct ulz77_encoder *enc = NULL;

    /* src */
    unsigned char *src_p;
    unsigned int task_len;

    /* dst */
    unsigned char *dst = NULL, *dst_p;
    unsigned int dst_buffer_size;
    unsigned int dst_buffer_remain_size;
    unsigned char *new_buffer = NULL;

    if (src == NULL) return -ULZ77_ERR_NULL_PTR;

    *dst_out = NULL;
    *dst_out_len = 0;

    /* Create destination buffer */
    dst_buffer_size = MAX(src_len * 3, BUFFER_SIZE);
    dst_buffer_remain_size = dst_buffer_size;
    dst = (unsigned char *)malloc(sizeof(unsigned char) * (dst_buffer_size));
    if (dst == NULL)
    {
        ret = -ULZ77_ERR_MALLOC;
        goto done;
    }

    /* Create encoder */
    enc = ulz77_encoder_new();

    src_p = src;
    dst_p = dst;
    task_len = src_len;
    for (;;)
    {
        if (type == ULZ77_TYPE_COMPRESSION)
        {
            ret = ulz77_encoder_encode(enc, dst_p, dst_buffer_remain_size, src_p, task_len);
        }
        else if (type == ULZ77_TYPE_DECOMPRESSION)
        {
            ret = ulz77_encoder_decode(enc, dst_p, dst_buffer_remain_size, src_p, task_len);
        }
        else
        {
            ret = -ULZ77_ERR_UNKNOWN_OP;
            goto done;
        }
        if (ret == 0)
        {
            *dst_out = dst;
            *dst_out_len = enc->dst_total_len;
            dst = NULL;
            ret = 0;
            goto done;
        }
        else if (ret == -ULZ77_ERR_BUFFER_FULL)
        {
            /* extend buffer */
            dst_buffer_size <<= 1;
            new_buffer = (unsigned char *)malloc(sizeof(unsigned char) * dst_buffer_size);
            if (new_buffer == NULL)
            {
                ret = -ULZ77_ERR_MALLOC;
                goto done;
            }
            memcpy(new_buffer, dst, enc->dst_total_len);
            free(dst);dst = new_buffer;new_buffer = NULL;
            dst_buffer_remain_size = dst_buffer_size - enc->dst_total_len;
            task_len -= enc->src_len;
            src_p = ulz77_encoder_get_previous(enc);
            dst_p = dst + enc->dst_total_len;
        }
    }
done:
    if (enc != NULL) ulz77_encoder_destroy(enc);
    return ret;
}

/* Compress data */
int ulz77_compress_data(unsigned char **dst_out, size_t *dst_out_len, unsigned char *src, size_t src_len)
{
    return ulz77_encode_data(dst_out, dst_out_len, src, src_len, ULZ77_TYPE_COMPRESSION);
}

/* Decompress data */
int ulz77_decompress_data(unsigned char **dst_out, size_t *dst_out_len, unsigned char *src, size_t src_len)
{
    return ulz77_encode_data(dst_out, dst_out_len, src, src_len, ULZ77_TYPE_DECOMPRESSION);
}

/* Encode file */
int ulz77_encode_file(const char *filename_dst, const char *filename_src, int type)
{
    int ret = 0;
    FILE *fp_src = NULL, *fp_dst = NULL;

    /* src */
    unsigned char *src = NULL;
    size_t src_len;

    /* dst */
    unsigned char *dst = NULL;
    size_t dst_len;

    /* Open files */
    fp_src = fopen(filename_src, "rb");
    if (fp_src == NULL)
    {
        ret = -ULZ77_ERR_FILE_OPEN;
        goto done;
    }

    /* Get source file length */
    fseek(fp_src, 0, SEEK_END);
    src_len = ftell(fp_src);
    fseek(fp_src, 0, SEEK_SET);

    /* Allocate space for source */
    src = (unsigned char *)malloc(sizeof(unsigned char) * src_len);
    if (src == NULL)
    {
        ret = -ULZ77_ERR_MALLOC;
        goto done;
    }

    if (fread(src, src_len, 1, fp_src) < 1) 
    {
        ret = ULZ77_ERR_FILE_READ;
        goto done;
    }

    ret = ulz77_encode_data(&dst, &dst_len, src, src_len, type);
    if (ret == 0)
    {
        fp_dst = fopen(filename_dst, "wb+");
        if (fp_dst == NULL)
        {
            ret = -ULZ77_ERR_FILE_OPEN;
            goto done;
        }
        fwrite(dst, dst_len, 1, fp_dst);
    }

done:
    if (src != NULL) free(src);
    if (dst != NULL) free(dst);
    if (fp_src != NULL) fclose(fp_src);
    if (fp_dst != NULL) fclose(fp_dst);

    return ret;
}

/* Compress file */
int ulz77_compress_file(const char *filename_dst, const char *filename_src)
{
    return ulz77_encode_file(filename_dst, filename_src, ULZ77_TYPE_COMPRESSION);
}

/* Decompress file */
int ulz77_decompress_file(const char *filename_dst, const char *filename_src)
{
    return ulz77_encode_file(filename_dst, filename_src, ULZ77_TYPE_DECOMPRESSION);
}

enum 
{
    ULZ77_STREAM_WRITER_TYPE_NULL = 0,
    ULZ77_STREAM_WRITER_TYPE_FP = 1,
    ULZ77_STREAM_WRITER_TYPE_CB = 2,
};

enum 
{
    ULZ77_STREAM_READER_TYPE_NULL = 0,
    ULZ77_STREAM_READER_TYPE_FP = 1,
    ULZ77_STREAM_READER_TYPE_CB = 2,
};

/* Create a new stream */
struct ulz77_stream *ulz77_stream_new(void)
{
    struct ulz77_stream *new_stream = NULL;
    new_stream = (struct ulz77_stream *)malloc(sizeof(struct ulz77_stream));
    if (new_stream == NULL) return NULL;

    new_stream->writer_type = ULZ77_STREAM_WRITER_TYPE_NULL;
    new_stream->writer_fp = NULL;
    new_stream->writer_cb = NULL;

    new_stream->reader_type = ULZ77_STREAM_READER_TYPE_NULL;
    new_stream->reader_fp = NULL;
    new_stream->reader_cb = NULL;

    return new_stream;
}

/* Destroy a stream */
int ulz77_stream_destroy(struct ulz77_stream *stream)
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;
    free(stream);
    return 0;
}

/* Stream writer Null */
int ulz77_stream_set_writer_null(struct ulz77_stream *stream)
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;
    if (stream->writer_fp != NULL)
    {
        fclose(stream->writer_fp);
        stream->writer_fp = NULL;
    }
    stream->writer_cb = NULL;
    stream->writer_type = ULZ77_STREAM_WRITER_TYPE_NULL;
    return 0;
}

/* Stream writer File Pointer */
int ulz77_stream_set_writer_fp(struct ulz77_stream *stream, FILE *fp)
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;

    ulz77_stream_set_writer_null(stream);
    stream->writer_type = ULZ77_STREAM_WRITER_TYPE_FP;
    stream->writer_fp = fp;

    return 0;
}

/* Stream writer Callback */
int ulz77_stream_set_writer_callback(struct ulz77_stream *stream, int (*writer_cb)(unsigned char *data, size_t size))
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;

    ulz77_stream_set_writer_null(stream);
    stream->writer_type = ULZ77_STREAM_WRITER_TYPE_CB;
    stream->writer_cb = writer_cb;

    return 0;
}

/* Stream reader Null */
int ulz77_stream_set_reader_null(struct ulz77_stream *stream)
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;
    if (stream->reader_fp != NULL)
    {
        fclose(stream->reader_fp);
        stream->reader_fp = NULL;
    }
    stream->reader_cb = NULL;
    stream->reader_type = ULZ77_STREAM_WRITER_TYPE_NULL;
    return 0;
}

/* Stream reader File Pointer */
int ulz77_stream_set_reader_fp(struct ulz77_stream *stream, FILE *fp)
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;

    ulz77_stream_set_reader_null(stream);
    stream->reader_type = ULZ77_STREAM_WRITER_TYPE_FP;
    stream->reader_fp = fp;

    return 0;
}

/* Stream reader Callback */
int ulz77_stream_set_reader_callback(struct ulz77_stream *stream, int (*reader_cb)(unsigned char *data, size_t size))
{
    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;

    ulz77_stream_set_reader_null(stream);
    stream->reader_type = ULZ77_STREAM_WRITER_TYPE_CB;
    stream->reader_cb = reader_cb;

    return 0;
}

/* Push data into stream */
int ulz77_stream_push(struct ulz77_stream *stream, unsigned char *data, size_t size)
{
    int ret = 0;
    unsigned char *dst = NULL;
    size_t dst_len = 0;
    size_t written_len;

    if (stream == NULL) return -ULZ77_ERR_NULL_PTR;

    /* Compress data */
    ret = ulz77_encode_data(&dst, &dst_len, data, size, ULZ77_TYPE_COMPRESSION);
    if (ret != 0)
    {
        goto done;
    }

    /* Process the data */
    switch (stream->writer_type)
    {
        case ULZ77_STREAM_READER_TYPE_NULL:
            /* Do nothing */
            break;
        case ULZ77_STREAM_WRITER_TYPE_CB:
            /* Write size of compressed data */
            ret = (*stream->writer_cb)((unsigned char *)&dst_len, sizeof(uint32_t));
            if (ret != 0) goto done;
            /* Write compressed data */
            ret = (*stream->writer_cb)(dst, dst_len);
            if (ret != 0) goto done;
            break;
        case ULZ77_STREAM_WRITER_TYPE_FP:
            /* Write size of compressed data */
            written_len = fwrite((unsigned char *)&dst_len, sizeof(uint32_t), 1, stream->writer_fp);
            if (written_len < 1)
            { ret = ULZ77_ERR_FILE_WRITE; goto done; }
            /* Write compressed data */
            written_len = fwrite(dst, dst_len, 1, stream->writer_fp);
            if (written_len < 1)
            { ret = ULZ77_ERR_FILE_WRITE; goto done; }
            break;
        default:
            ret = -ULZ77_ERR_UNKNOWN_WRITER;
            break;
    }

done:
    if (dst != NULL) free(dst);
    return ret;
}

/* Pull data from stream */
int ulz77_stream_pull(struct ulz77_stream *stream, unsigned char **data, size_t *size)
{
    int ret = 0;
    uint32_t block_size;
    unsigned char *src = NULL;
    unsigned char *dst = NULL;
    size_t dst_len;

    switch (stream->reader_type)
    {
        case ULZ77_STREAM_READER_TYPE_NULL:
            ret = -ULZ77_ERR_INVALID_READER;
            goto fail;
            break;
        case ULZ77_STREAM_READER_TYPE_FP:
            /* Read block size */
            if (fread(&block_size, sizeof(uint32_t), 1, stream->reader_fp) < 1)
            {
                ret = -ULZ77_ERR_FILE_READ;
                goto fail;
            }
            /* Allocate space for block */
            src = (unsigned char *)malloc(sizeof(unsigned char) * block_size);
            if (src == NULL)
            {
                ret = -ULZ77_ERR_MALLOC;
                goto fail;
            }
            /* Read block */
            if (fread(src, block_size, 1, stream->reader_fp) < 1)
            {
                ret = -ULZ77_ERR_FILE_READ;
                goto fail;
            }
            break;
        case ULZ77_STREAM_READER_TYPE_CB:
            ret = -ULZ77_ERR_UNKNOWN_READER;
            goto fail;
            break;
        default:
            ret = -ULZ77_ERR_UNKNOWN_READER;
            goto fail;
            break;
    }

    ret = ulz77_encode_data(&dst, &dst_len, src, block_size, ULZ77_TYPE_DECOMPRESSION);
    if (ret != 0)
    {
        goto fail;
    }
    stream->reader_count = block_size + 4; /* 4 is the size of block size */
    stream->reader_total_count += block_size;
    *data = dst;
    *size = dst_len;
    ret = 0;
    goto done;
fail:
    if (dst != NULL) free(dst);
done:
    if (src != NULL) free(src);
    return ret;
}

/* Copy Error description */
int ulz77_error_description_cpy(char *buf, size_t buf_len, int err_no)
{
    static const char *err_msg[] =
    {
        "Unknown error",
        "Null Pointer",
        "Invalid argument",
        "Memory allocation failed",
        "File opening failed",
        "File reading failed",
        "Buffer full",
        "Undefined operation",
        "Unknown reader",
        "Invalid reader",
        "Unknown writter",
        "Invalid writter",
        "Narrow buffer size",
        "Undefined error",
    };

    if ((err_no >= 0)||((unsigned int)(-err_no)>=sizeof(err_msg)/sizeof(char*))) 
    { return 0; }

    strncpy(buf, err_msg[err_no], buf_len);

    return 0;
}

/* Print Error description */
int ulz77_error_description_print(int err_no)
{
    char buffer[4096];

    ulz77_error_description_cpy(buffer, 4096, err_no);
    fprintf(stderr, "Error : %s\n", buffer);

    return 0;
}

