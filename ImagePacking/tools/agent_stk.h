/*********************************************************************
*
* (C) Copyright Broadcom Corporation 2007-2017
*
**********************************************************************
*
* @filename  agent_stk.h
*
* @purpose   Header file definitions for agent_stk host utility.
*
* @component tools
*
* @create    01/28/2016
*
* @end
*
*********************************************************************/
#ifndef _INCLUDE_AGENT_STK_H_
#define _INCLUDE_AGENT_STK_H_

#define ENVIRON_TOP                     "TOP"
#define CMD_OUTPUT_ALLOW                ""
#define CMD_OUTPUT_NOERR                "2>/dev/null"
#define CMD_OUTPUT_NONE                 ">/dev/null 2>&1"
#define FILENAME_LEN_MAX                256
#define FREAD_CHUNK                     256
#define FGETS_LINE_MAX                  256
#define IMG_PARTS_MAX                   16             /* oversized to support FIT format images */
#define IMG_PART_NAME_MAX               64
#define IMG_ATTR_SIZE_SMALL             32
#define IMG_ATTR_SIZE_LARGE             128
#define IMG_OPTS_SIZE_MAX               128
#define SYS_CMD_BUF_SIZE                512
#define TIMESTAMP_BUF_SIZE              32
#define VER_PART_ALPHA_LEN_MAX          2
#define VER_PART_MIN                    0
#define VER_PART_MAX                    63
#define VER_PART_UNINIT                 -999
#define VER_STR_SIZE                    64

/* Subdirectory names.
*/
#define DIR_AGENT                      "./agent"
#define DIR_PARTS                      "./parts"
#define DIR_TARGET                     "./target"
#define DIR_TGZ                        "./tgz"
#define DIR_TMP                        "./tmp1"
#define DIR_TOOLS                      "./tools"

/* Base file names.
*/
#define FILE_TGZ                        "fastpath.tgz"
#define FILE_UIM                        "fastpath.uim"
#define FILE_UIMINFO                    "uim_info.txt"
#define FILE_VPD                        "fastpath.vpd"
#define FILE_VPDTMP                     "fastpath.vpd.tmp"

/* Temporary file names.
*/
#define TMPFILE_TGZ                     DIR_TMP "/" FILE_TGZ
#define TMPFILE_UIM                     DIR_TMP "/" FILE_UIM
#define TMPFILE_UIMINFO                 DIR_TMP "/" FILE_UIMINFO
#define TMPFILE_VPD                     DIR_TMP "/" FILE_VPDTMP
#define VPDFILE                         DIR_TGZ "/" FILE_VPD 

/* Utility command names.
*/
#define CMD_EXTIMAGE                    "extimage"
#define CMD_FITIMAGE                    "extfitimage"
#define CMD_MKIMAGE                     "mkimage"
#define CMD_MKSTK                       "mk_stk"

/* Input parameters specifying new version information to use
** when naming updated STK file.
*/
typedef struct
{
  char              rel_str[VER_PART_ALPHA_LEN_MAX + 1];
  int               ver;
  int               maint;
  int               bld;
} img_version_t;

/* Various format strings used for parsing VPD version information.
*/
typedef struct
{
  char              *old_fmt;
  char              *new_fmt;
  char              *new_fmt_b;
} vpd_parse_fmt_t;

/* U-boot image part information.
*/
typedef struct
{
  char              part_name[IMG_PART_NAME_MAX];     /* uimage part name reference (e.g. "part1", "firmware@1") */
  unsigned int      part_size;                        /* uimage part size (in bytes) */
  char              fit_filename[FILENAME_LEN_MAX];   /* FIT only: name of image file (sans path prefix) */
} uim_part_info_t;

/* Parameters used by mkimage utility to create an uimage file.
*/
typedef struct
{
  /* these fields are used directly for the 'mkimage' cmd */
  char              img_name[IMG_ATTR_SIZE_LARGE];    /* image name (e.g. "System for gto") */
  char              cpu[IMG_ATTR_SIZE_SMALL];         /* CPU name (e.g. gto, iproc) */
  char              arch[IMG_ATTR_SIZE_SMALL];        /* CPU architecture (e.g. arm, ppc, powerpc) */
  char              cmpr[IMG_ATTR_SIZE_SMALL];        /* compression type used (e.g. gzip, lzma) */
  unsigned int      start;                            /* image load address */
  unsigned int      entry;                            /* image entry point */
  uim_part_info_t   parts[IMG_PARTS_MAX];             /* uimage parts information */

  /* additional uimage info of interest */
  unsigned int      is_fit_format;                    /* uimage file is FIT format (rather than traditional) */
  unsigned int      uim_length;                       /* total number of bytes in uimage file */
  unsigned int      num_parts;                        /* number of parts comprising uimage file */
  unsigned int      code_img_part;                    /* part number containing actual code image file */
  unsigned int      code_img_size;                    /* uimage part code file size (in bytes) */

} mkimage_parms_t;

#endif /* _INCLUDE_AGENT_STK_H_ */
