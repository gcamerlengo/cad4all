/* SPDX-License-Identifier: GPL-3.0-or-later */
/* C interface to LibreDWG, used by dwgreader.cpp. */
#ifndef CAD4ALL_DWG_SHIM_H
#define CAD4ALL_DWG_SHIM_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes the DWG in `data` and writes it to `out` as ASCII DXF.
 * Returns 0 on success, otherwise a LibreDWG error code. */
int cad4all_dwg_to_dxf(const unsigned char* data, size_t size, FILE* out);

/* Encodes the DXF 2000 in `data` as a DWG 2000. On success returns 0 and
 * `out` holds `outSize` bytes, to release with cad4all_dwg_free. */
int cad4all_dxf_to_dwg(const unsigned char* data, size_t size, unsigned char** out, size_t* outSize);
void cad4all_dwg_free(void* p);

#ifdef __cplusplus
}
#endif

#endif
