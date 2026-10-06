// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#ifndef DOCENHANCE_TIFF_H
#define DOCENHANCE_TIFF_H
/* Private locked-source ABI: per-handle memory installation before headers and policy admission
 * before decoding. */
typedef struct {
    void (*install)(void* context, void* decoder);
    int (*prepare)(void* context, void* decoder);
    void* context;
} DocEnhanceTiffJpegControl;
#define DOCENHANCE_TIFF_JPEG_CONTROL "docenhance.jpeg.control"
#endif
