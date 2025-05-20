/* -*- linux-c -*-
 * Symbolic Lookup Functions
 * Copyright (C) 2005-2023 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#if defined(_STP_SYM_C_) && defined(STP_NEED_LINE_DATA)

// At this point the compiler needs to know the context struct.  The context
// struct is emitted by s.up->emit_common_header () within translate.cxx
// The _stp_filename_lookup_5 is declared in runtime/sym.c , but at that point
// the context struct isn't known yet.  Here, context struct is known already,
// so here comes the _stp_filename_lookup_5 function body.

static void _stp_filename_lookup_5(struct _stp_module *mod, char ** filename,
                                   uint8_t *dirsecp, uint8_t *enddirsecp,
                                   unsigned int length,
                                   unsigned fileidx, int user, int compat_task,
                                   struct context *c)
{
  // Pointer to the .debug_line section
  // pointing at just after standard_opcode_lengths
  // which is the last header item common to DWARF v4 and v5.
  uint8_t *debug_line_p = dirsecp;
  // Pointer to the beginning of .debug_line_str section
  uint8_t *debug_line_str_p = mod->debug_line_str;
  uint8_t *endstrsecp = mod->debug_line_str + mod->debug_line_str_len;

  uint8_t directory_entry_format_count = 0, file_name_entry_format_count = 0,
          directories_count = 0, file_names_count = 0;

  static char fullpath [MAXSTRINGLEN];

  // Reusable loop iterators.  As the producer records show, the rhel8 kbuild
  // system uses -std=gnu90 not allowing initial loop declarations, while the
  // rhel9 build system allows that.  We need to stay compatible though..
  // https://lwn.net/Articles/885941/
  int i = 0, j = 0;

  // We rely on struct context for c->dw_data.
  // That needs to be known at this point.
  if (c == NULL)
    {
      _stp_error("BUG: Unknown context in _stp_filename_lookup_5()\n");
      return;
    }

  // Initialize the *filename
  *filename = "unknown";

  // Next comes directory_entry_format_count
  if (debug_line_str_p + 1 > enddirsecp)
    return;
  directory_entry_format_count = *debug_line_p++;
  if (directory_entry_format_count > STP_MAX_DW_SOURCES)
    return;

  // Next comes directory_entry_format
  for (i = 0; i < directory_entry_format_count; i++)
    {
      c->dw_data.dir_enc[i].desc = read_pointer ((const uint8_t **) &debug_line_p,
                                                 enddirsecp, DW_EH_PE_leb128, user, compat_task);
      c->dw_data.dir_enc[i].form = read_pointer ((const uint8_t **) &debug_line_p,
                                                 enddirsecp, DW_EH_PE_leb128, user, compat_task);
    }

  // Next comes directories_count
  directories_count = read_pointer ((const uint8_t **) &debug_line_p,
                                    enddirsecp, DW_EH_PE_leb128, user, compat_task);
  if (directories_count > STP_MAX_DW_SOURCES)
    return;

  // Next come directories
  // See elfutil's print_form_data() in readelf.c for an analogy of what happens below
  for (i=0; i < directories_count; i++)
      for (j=0; j < directory_entry_format_count; j++)
          switch (c->dw_data.dir_enc[j].form)
            {
              case DW_FORM_line_strp:
                c->dw_data.src_dir[i].offset = (uint32_t) read_pointer ((const uint8_t **) &debug_line_p,
                                                                       enddirsecp, DW_EH_PE_data4, user, compat_task);
                if ((uint8_t *) mod->debug_line_str + c->dw_data.src_dir[i].offset > endstrsecp)
                  return;
                c->dw_data.src_dir[i].name = mod->debug_line_str + c->dw_data.src_dir[i].offset;
                break;
              default:
                _stp_error("BUG: Unknown form %d encountered while parsing source dir\n",
                           c->dw_data.dir_enc[j].form);
                return;
            }

  // Next comes file_name_entry_format_count
  if (debug_line_p + 1 > enddirsecp)
    return;
  file_name_entry_format_count = *debug_line_p++;
  if (file_name_entry_format_count > STP_MAX_DW_SOURCES)
    return;


  // Next comes file_name_entry_format
  for (i = 0; i < file_name_entry_format_count; i++)
    {
      c->dw_data.file_enc[i].desc = read_pointer ((const uint8_t **) &debug_line_p,
                                                  enddirsecp, DW_EH_PE_leb128, user,
                                                  compat_task);
      c->dw_data.file_enc[i].form = read_pointer ((const uint8_t **) &debug_line_p,
                                                  enddirsecp, DW_EH_PE_leb128, user,
                                                  compat_task);
    }

  // Next comes the file_names_count
  // See elfutil's print_form_data() in readelf.c for an analogy of what happens below
  file_names_count = read_pointer ((const uint8_t **) &debug_line_p, enddirsecp,
                                   DW_EH_PE_leb128, user, compat_task);
  if (file_names_count > STP_MAX_DW_SOURCES)
    return;

  // Next come the files
  for (i=0; i < file_names_count; i++)
      for (j=0; j < file_name_entry_format_count; j++)
          switch (c->dw_data.file_enc[j].form)
            {
              case DW_FORM_line_strp:
                c->dw_data.src_file[i].offset = (uint32_t) read_pointer ((const uint8_t **) &debug_line_p,
                                                                        enddirsecp, DW_EH_PE_data4, user,
                                                                        compat_task);
                if ((uint8_t *) mod->debug_line_str + c->dw_data.src_file[i].offset > endstrsecp)
                  return;
                c->dw_data.src_file[i].name = mod->debug_line_str + c->dw_data.src_file[i].offset;
                break;
              case DW_FORM_data16:
                // This is how clang encodes the md5sum, skip it
                if (debug_line_p + 16 > enddirsecp)
                  return;
                debug_line_p += 16;
                break;
              case DW_FORM_udata:
                c->dw_data.src_file[i].dirindex = (uint8_t) read_pointer ((const uint8_t **) &debug_line_p,
                                                                          enddirsecp, DW_EH_PE_leb128, user,
                                                                          compat_task);
                break;
              default:
                _stp_error("BUG: Unknown form %d encountered while parsing source file\n",
                           c->dw_data.file_enc[j].form);
                return;
            }

  // Put it together
  // - requested file index is fileidx
  //   (based on the line number program)
  // - find directory respective to this file
  // - and attach slash and the file name itself
  strlcpy(fullpath, c->dw_data.src_dir[c->dw_data.src_file[fileidx].dirindex].name, MAXSTRINGLEN);
  strlcat(fullpath, "/", MAXSTRINGLEN);
  strlcat(fullpath, c->dw_data.src_file[fileidx].name, MAXSTRINGLEN);
  *filename = fullpath;
}

#endif
