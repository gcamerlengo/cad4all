/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The only file that sees LibreDWG's internals: decodes a DWG held in
 * memory and writes it as DXF, which the DXF reader then imports, and the
 * other way round, encodes a DXF 2000 held in memory as a DWG 2000. Plain C,
 * because LibreDWG's headers are C. */
#include "dwg_shim.h"

#include "dwg.h"
#include "bits.h"
#include "decode.h"
#include "encode.h"
#include "out_dxf.h"
/* After out_dxf.h, as LibreDWG requires. */
#include "in_dxf.h"

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


/* LibreDWG 0.14 reads MTEXT group 40 (text height) into the box width and
 * 41 (box width) into the box height: move them back where they belong. */
static void fix_mtext_sizes(Dwg_Data* dwg)
{
    BITCODE_BL i;
    for (i = 0; i < dwg->num_objects; ++i) {
        Dwg_Object* obj = &dwg->object[i];
        Dwg_Entity_MTEXT* m;
        if (obj->fixedtype != DWG_TYPE_MTEXT || !obj->tio.entity)
            continue;
        m = obj->tio.entity->tio.MTEXT;
        if (m && m->text_height == 0.0 && m->rect_width > 0.0) {
            m->text_height = m->rect_width;
            m->rect_width = m->rect_height;
            m->rect_height = 0.0;
        }
    }
}

int cad4all_dxf_to_dwg(const unsigned char* data, size_t size, unsigned char** out, size_t* outSize)
{
    Dwg_Data dwg;
    Bit_Chain in;
    Bit_Chain dwgOut;
    int error;

    *out = NULL;
    *outSize = 0;
    memset(&dwg, 0, sizeof dwg);
    memset(&in, 0, sizeof in);
    memset(&dwgOut, 0, sizeof dwgOut);
    /* As dxf_read_file: the text ends with a line break and a zero. */
    in.chain = (unsigned char*)calloc(1, size + 2);
    if (!in.chain)
        return DWG_ERR_OUTOFMEM;
    memcpy(in.chain, data, size);
    in.size = size;
    if (size == 0 || in.chain[size - 1] != '\n') {
        in.chain[size] = '\n';
        in.size++;
    }
    dwg.opts = DWG_OPTS_INDXF;
    dwg.header.version = R_2000;
    in.version = R_2000;
    in.opts = dwg.opts;

    error = dwg_read_dxf(&in, &dwg);
    free(in.chain);
    dwg.opts |= DWG_OPTS_INDXF;
    if (error >= DWG_ERR_CRITICAL) {
        dwg_free(&dwg);
        return error;
    }
    fix_mtext_sizes(&dwg);
    /* LibreDWG encodes R2000 only, from a DXF of that version. */
    dwg.header.version = R_2000;
    if (dwg.header.from_version == R_INVALID)
        dwg.header.from_version = R_2000;
    dwgOut.opts = dwg.opts;
    dwgOut.version = R_2000;
    dwgOut.from_version = R_2000;
    dwgOut.codepage = dwg.header.codepage;
    error = dwg_encode(&dwg, &dwgOut);
    dwg_free(&dwg);
    if (error >= DWG_ERR_CRITICAL || !dwgOut.chain) {
        free(dwgOut.chain);
        return error ? error : DWG_ERR_CRITICAL;
    }
    *out = dwgOut.chain;
    *outSize = dwgOut.size;
    return 0;
}

void cad4all_dwg_free(void* p)
{
    free(p);
}
