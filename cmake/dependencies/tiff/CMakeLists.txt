# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 4.4)
project(DocEnhanceTiff LANGUAGES C CXX)
if(NOT DOCENHANCE_TIFF_CHARGED_JPEG OR NOT DOCENHANCE_TIFF_COMPLETE_DEFLATE OR
   NOT jpeg OR NOT zlib OR libdeflate)
  message(FATAL_ERROR "TIFF requires the reviewed charged JPEG adapter and locked codecs")
endif()
file(SHA256 "${DE_UPSTREAM_SOURCE}/libtiff/tif_jpeg.c" digest)
if(NOT digest STREQUAL DOCENHANCE_TIFF_JPEG_SHA256)
  message(FATAL_ERROR "Review TIFF JPEG allocation against the locked source")
endif()
set(owned "${CMAKE_CURRENT_BINARY_DIR}/owned-source")
file(COPY "${DE_UPSTREAM_SOURCE}/" DESTINATION "${owned}")
file(READ "${owned}/libtiff/tif_jpeg.c" source)
set(before "    return CALLVJPEG(sp, jpeg_create_decompress(&sp->cinfo.d));")
set(after "    if (!CALLVJPEG(sp, jpeg_create_decompress(&sp->cinfo.d)))\n        return 0;\n    DocEnhanceTiffJpegControl *memory = (DocEnhanceTiffJpegControl *)\n        TIFFGetClientInfo(sp->tif, DOCENHANCE_TIFF_JPEG_CONTROL);\n    if (!memory || !memory->install)\n    {\n        TIFFErrorExtR(sp->tif, \"JPEG\", \"Charged JPEG memory installer is absent\");\n        return 0;\n    }\n    memory->install(memory->context, &sp->cinfo.d);\n    return 1;")
string(FIND "${source}" "${before}" found)
if(found LESS 0)
  message(FATAL_ERROR "The reviewed TIFF JPEG bootstrap is absent")
endif()
string(REPLACE "${before}" "${after}" source "${source}")
set(before "    if (!TIFFjpeg_start_decompress(sp))")
set(after "    DocEnhanceTiffJpegControl *control = (DocEnhanceTiffJpegControl *)\n        TIFFGetClientInfo(tif, DOCENHANCE_TIFF_JPEG_CONTROL);\n    if (!control || !control->prepare ||\n        !control->prepare(control->context, &sp->cinfo.d))\n        return 0;\n    if (!TIFFjpeg_start_decompress(sp))")
string(FIND "${source}" "${before}" found)
if(found LESS 0)
  message(FATAL_ERROR "The reviewed TIFF JPEG admission boundary is absent")
endif()
string(REPLACE "${before}" "${after}" source "${source}")
# Keep the already-installed shared progress control. The native environment-controlled
# monitor duplicates its policy and overwrites cancellation immediately before decoding.
set(begin "/* Avoid the risk of denial-of-service on crafted JPEGs")
set(end "/*\n * Interface routines.")
string(FIND "${source}" "${begin}" first)
string(FIND "${source}" "${end}" last)
if(first LESS 0 OR last LESS first)
  message(FATAL_ERROR "The reviewed TIFF JPEG progress routine is absent")
endif()
string(SUBSTRING "${source}" 0 ${first} prefix)
string(SUBSTRING "${source}" ${last} -1 suffix)
set(source "${prefix}${suffix}")
set(begin "    const char *sz_max_allowed_scan_number;")
set(end "    return CALLVJPEG(sp, jpeg_start_decompress(&sp->cinfo.d));")
string(FIND "${source}" "${begin}" first)
string(FIND "${source}" "${end}" last)
if(first LESS 0 OR last LESS first)
  message(FATAL_ERROR "The reviewed TIFF JPEG progress override is absent")
endif()
string(SUBSTRING "${source}" 0 ${first} prefix)
string(SUBSTRING "${source}" ${last} -1 suffix)
set(source "${prefix}${suffix}")
string(REPLACE "    int max_allowed_scan_number;\n" "" source "${source}")
string(REPLACE "    struct jpeg_progress_mgr progress;\n" "" source "${source}")
file(CONFIGURE OUTPUT "${owned}/libtiff/tif_jpeg.c"
  CONTENT "#include \"docenhance_tiff.h\"\n${source}" @ONLY NEWLINE_STYLE LF)
# This private reader decodes complete striles; a full output buffer does not prove zlib EOF.
file(SHA256 "${DE_UPSTREAM_SOURCE}/libtiff/tif_zip.c" digest)
if(NOT digest STREQUAL DOCENHANCE_TIFF_ZIP_SHA256)
  message(FATAL_ERROR "Review TIFF Deflate completion against the locked source")
endif()
file(READ "${owned}/libtiff/tif_zip.c" source)
set(before "    do\n    {\n        int state;\n        uInt avail_in_before =")
set(after "    int state = Z_OK;\n    do\n    {\n        uInt avail_in_before =")
string(FIND "${source}" "${before}" found)
if(found LESS 0)
  message(FATAL_ERROR "The reviewed TIFF Deflate decode loop is absent")
endif()
string(REPLACE "${before}" "${after}" source "${source}")
set(before "    } while (occ > 0);\n    if (occ != 0)")
set(after [=[    } while (occ > 0);
    /* DocEnhance: complete bounded striles require the checksum and exact stream extent. */
    if (occ == 0 && state != Z_STREAM_END)
    {
        uint8_t extra;
        Bytef *saved_out = sp->stream.next_out;
        sp->stream.next_out = &extra;
        sp->stream.avail_out = 1;
        state = inflate(&sp->stream, Z_FINISH);
        sp->stream.next_out = saved_out;
        if (state != Z_STREAM_END || sp->stream.avail_out != 1)
        {
            TIFFErrorExtR(tif, module, "Incomplete or oversized Deflate strile");
            sp->read_error = 1;
            return 0;
        }
    }
    if (occ == 0 && sp->stream.avail_in != 0)
    {
        TIFFErrorExtR(tif, module, "Trailing Deflate strile bytes");
        sp->read_error = 1;
        return 0;
    }
    tif->tif_rawcc = sp->stream.avail_in;
    if (occ != 0)]=])
string(FIND "${source}" "${before}" found)
if(found LESS 0)
  message(FATAL_ERROR "The reviewed TIFF Deflate completion boundary is absent")
endif()
string(REPLACE "${before}" "${after}" source "${source}")
file(CONFIGURE OUTPUT "${owned}/libtiff/tif_zip.c" CONTENT "${source}" @ONLY NEWLINE_STYLE LF)
configure_file(docenhance_tiff.h "${owned}/libtiff/docenhance_tiff.h" COPYONLY)
install(FILES docenhance_tiff.h DESTINATION include)
add_subdirectory("${owned}" upstream)
