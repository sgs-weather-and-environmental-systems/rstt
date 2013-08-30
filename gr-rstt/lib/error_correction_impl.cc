/* -*- c++ -*- */
/* 
 * Copyright 2013 <+YOU OR YOUR COMPANY+>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
extern "C" {
#include <gnuradio/fec/rs.h>
}
#include "byte_status.h"
#include "error_correction_impl.h"

namespace gr {
  namespace rstt {

    static const int rs_symsize = 8;
    static const int rs_gfpoly = 0x11d;
    static const int rs_fcr = 0;
    static const int rs_prim = 1;
    static const int rs_nroost = 24;

    static const int RS_N = (1 << rs_symsize) - 1;
    static const int RS_M = RS_N - rs_nroost;
    static const int FRAME_LEN = 240;
    static const int FRAME_HDR_LEN = 6;
    static const int FRAME_RS_LEN = rs_nroost;
    static const int FRAME_DATA_LEN = FRAME_LEN - FRAME_HDR_LEN - FRAME_RS_LEN;

    error_correction::sptr
    error_correction::make(bool guess)
    {
      return gnuradio::get_initial_sptr
        (new error_correction_impl(guess));
    }

    error_correction_impl::error_correction_impl(bool guess)
      : gr::sync_block("error_correction",
              gr::io_signature::make(1, 1, sizeof(in_t)*240),
              gr::io_signature::make(1, 1, sizeof(out_t)*240)),
              guess(guess)
    {
        rs = init_rs_char(rs_symsize, rs_gfpoly, rs_fcr, rs_prim, rs_nroost);
        assert (d_rs != 0);
        syn_bytes[6] = 0x65;
        syn_bytes[7] = 0x10;
        syn_bytes[42] = 0x69;
        syn_bytes[43] = 0x0C;
        syn_bytes[70] = 0x67;
        syn_bytes[71] = 0x3D;
        syn_bytes[196] = 0x68;
        syn_bytes[197] = 0x05;
        syn_bytes[210] = 0xFF;
        syn_bytes[211] = 0x02;
        syn_bytes[212] = 0x02;
        syn_bytes[213] = 0x00;
        syn_bytes[214] = 0x02;
        syn_bytes[215] = 0x00;
    }

    error_correction_impl::~error_correction_impl()
    {
        free_rs_char(rs);
    }

    int
    error_correction_impl::work(int noutput_items,
			  gr_vector_const_void_star &input_items,
			  gr_vector_void_star &output_items)
    {
        const in_t *in = (const in_t *) input_items[0];
        out_t *out = (out_t *) output_items[0];

        for (int frame_num = 0; frame_num < noutput_items; ++frame_num) {
            if (!do_correction(in, out)) {
                memcpy(out, in, 240*sizeof(in_t));
            }
            in += 240;
            out += 240;
        }

        return noutput_items;
    }

    bool
    error_correction_impl::do_correction(const in_t *in, out_t *out) const
    {
        unsigned char rs_data[RS_N];
        int erasures[rs_nroost];
        int nerasures = 0;

        memset(rs_data, 0, sizeof(rs_data));
        for (int in_idx = FRAME_HDR_LEN; in_idx < FRAME_LEN; ++in_idx) {
            const int rs_idx = in_idx < FRAME_HDR_LEN + FRAME_DATA_LEN ?
                RS_M - 1 - (in_idx - FRAME_HDR_LEN) :
                RS_N - 1 - (in_idx - FRAME_HDR_LEN - FRAME_DATA_LEN);
            if (syn_bytes.count(in_idx) && guess) {
                rs_data[rs_idx] = syn_bytes.at(in_idx);
                continue;
            }
            const in_t ival = in[in_idx];
            if (ival & gr::rstt::STATUS_ERR_BYTE) {
                if (nerasures >= rs_nroost) {
                    return false;
                }
                erasures[nerasures] = rs_idx;
                ++nerasures;
                continue;
            }
            rs_data[rs_idx] = ival;
        }

        const int ncorr = decode_rs_char(rs, rs_data, erasures, nerasures);
        if (ncorr == -1) {
            nerasures = 0;
            for (int in_idx = FRAME_HDR_LEN; in_idx < FRAME_LEN - FRAME_RS_LEN; ++in_idx) {
                const in_t ival = in[in_idx];
                if (ival & (~0xff)) {
                    if (nerasures >= sizeof(erasures)) {
                        return false;
                    }
                    if (syn_bytes.count(in_idx)) {
                        continue;
                    }
                    erasures[nerasures] = in_idx;
                    ++nerasures;
                    continue;
                }
            }
            const int ncorr2 = decode_rs_char(rs, rs_data, erasures, nerasures);
            if (ncorr2 == -1) {
                return false;
            }
        }

        // copy back corrected data and create new (correct) header
        out[0] = '*';
        out[1] = '*';
        out[2] = '*';
        out[3] = '*';
        out[4] = '*';
        out[5] = 0x10;
        for (int out_idx = FRAME_HDR_LEN; out_idx < FRAME_LEN; ++out_idx) {
            const int rs_idx = out_idx < (FRAME_HDR_LEN + FRAME_DATA_LEN) ?
                RS_M - 1 - (out_idx - FRAME_HDR_LEN) :
                RS_N - 1 - (out_idx - FRAME_HDR_LEN - FRAME_DATA_LEN);
            out[out_idx] = rs_data[rs_idx];
        }

        return true;
    }

  } /* namespace rstt */
} /* namespace gr */

