/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The only file that sees LibreDWG's internals: decodes a DWG held in
 * memory and writes it as DXF, which the DXF reader then imports. Plain C,
 * because LibreDWG's headers are C. */
#include "dwg_shim.h"

#include "dwg.h"
#include "bits.h"
#include "decode.h"
#include "out_dxf.h"

#include <stdlib.h>
#include <string.h>

int cad4all_dwg_to_dxf(const unsigned char* data, size_t size, FILE* out)
{
    Dwg_Data dwg;
    Bit_Chain in;
    Bit_Chain dxf;
    int error;

    memset(&dwg, 0, sizeof dwg);
    memset(&in, 0, sizeof in);
    memset(&dxf, 0, sizeof dxf);
    /* LibreDWG reads past the end looking for strings: keep a zero there. */
    in.chain = (unsigned char*)calloc(1, size + 1);
    if (!in.chain)
        return DWG_ERR_OUTOFMEM;
    memcpy(in.chain, data, size);
    in.size = size;

    error = dwg_decode(&in, &dwg);
    free(in.chain);
    if (error >= DWG_ERR_CRITICAL) {
        dwg_free(&dwg);
        return error;
    }
    dxf.version = dwg.header.version;
    dxf.from_version = dwg.header.from_version;
    dxf.fh = out;
    error = dwg_write_dxf(&dxf, &dwg);
    dwg_free(&dwg);
    return error >= DWG_ERR_CRITICAL ? error : 0;
}

