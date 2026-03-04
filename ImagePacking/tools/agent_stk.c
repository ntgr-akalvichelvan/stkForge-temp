/*********************************************************************
*
* Copyright 2016-2018 Broadcom
*
**********************************************************************
*
* @filename  agent_stk.c
*
* @purpose   This is a standalone utility that inserts a set of files
*            into an existing .stk file. All executable files are
*            expected to have been pre-compiled using the proper
*            cross-compiler tool chain.
*
* @component tools
*
* @create    12/20/2015
*
* @end
*
*********************************************************************/
#define _XOPEN_SOURCE 500
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <ftw.h>
#include <netinet/in.h> /* htonl(), ntohl() */
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#include "stk.h"
#include "agent_stk.h"

/* Version of this utility.
** 
** History: 
**   1.0.0  (08-Jan-2016)
**     - Initial release.
**   1.1.0  (28-Jan-2016)
**     - Added support for FIT format u-boot image files.
**   1.2.0  (22-May-2018)
**     - Added -n option to remove old agent name.
**     - Changed new .stk file permissions from 0775 to 0664.
*/
static char *util_name        = "agent_stk";
static char *util_version     = "1.2.0";

/* Debug output control.
*/
static int show_dbg           = 0;

/* Current working directory (caller can set TOP environment variable to override).
*/
static char *env_top          = NULL;

/* Name of agent bundle deliverable file.
*/
static char *agt_bundle_fname = "cmapd_agent_pkg.tgz";

/* Directory where these utilities were built.
** 
** NOTE: A non-empty path string retains the trailing '/' to make it 
**       easier to combine with a command name. 
*/
static char tools_path[FILENAME_LEN_MAX] = { 0 };

/* Global variables.
*/
static char *cmd_output_ctrl = CMD_OUTPUT_NONE;
static char *p_cmd_output_noerr = CMD_OUTPUT_NOERR;
static char *p_dir_parts = DIR_PARTS;
static char *p_dir_target = DIR_TARGET;
static char *p_dir_tgz = DIR_TGZ;
static char *p_dir_tmp = DIR_TMP;
static char *p_dir_tools = DIR_TOOLS;
static char *p_file_tgz = TMPFILE_TGZ;
static char *p_file_uim = TMPFILE_UIM;
static char *p_file_uiminfo = TMPFILE_UIMINFO;
static char *p_file_vpd = VPDFILE;
static char *p_file_vpdtmp = TMPFILE_VPD;


/************************************************************
** Display help info.
************************************************************/
static void usage(void)
{
  printf("\nUsage: %s [-h | <options>] <stk_filename>\n" \
         "\t<stk_filename>     name of the STK file to update\n" \
         "\t-h                 display this help\n" \
         "\n\tAny of the following <options> may be specified:\n" \
         "\t-d                 show debugging output\n" \
         "\t-a <agent_path>    path containing agent files to insert (defaults to: $CWD/agent)\n" \
         "\t-n <old_agent>     remove old agent name from STK image (e.g. fake_agent)\n" \
         "\t-r <rel>           release number    may be numeric or alphanumeric\n"\
         "\t                                     (numeric: 0-63)\n"\
         "\t                                     (alphanumeric: a letter from A-Z or a-z, optionally followed by a single letter or digit 0-9)\n"\
         "\t-v <ver>           version number    (0-63)\n" \
         "\t-m <maint>         maintanence level (0-63)\n" \
         "\t-b <bld>           build number      (0-63)\n" \
         "\nThe original .stk file version (r.v.m.b) is used except as " \
         "replaced by any of the -r, -v, -m, -b option(s) specified here.\n" \
         " > Use -r 0 to create a version number based on date/time (month.day.hour.minute).\n" \
         " > Use -b 0 to omit build number part from .stk file name (option ignored when version number based on date/time is used).\n\n" \
         "The invocation directory is assumed to be the working directory unless the TOP environment "\
         "variable is set, for example:\n"\
         "\tTOP=`pwd` ./tools/%s ... \n\n"\
         "[ver %s]\n\n" \
         "", util_name, util_name, util_version);
};


/*****************************************************************************
** NAME: dbgprintf
**
** A printf() equivalent that only produces output when SHOW_DBG is defined as 1.
**
*****************************************************************************/
static void dbgprintf(char *fmt, ...)
{
  va_list ap;

  if (show_dbg != 0)
  {
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
}

/*****************************************************************************
** NAME: system_cmd
**
** Issue a shell command using the system() call.
**
*****************************************************************************/
static int system_cmd(char *fmt, ...)
{
  char buf[SYS_CMD_BUF_SIZE] = { 0 };
  va_list ap;
  int ret;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  dbgprintf("Executing system() command: %s\n", buf);

  ret = system(buf);
  if ((ret < 0) ||
      !WIFEXITED(ret) ||
      (WEXITSTATUS(ret) != 0))
  {
    return -1;
  }

  return 0;
}

/*****************************************************************************
** NAME: convert_tolower
**
** Convert a string in place to all lower-case characters.
**
*****************************************************************************/
static void convert_tolower(char *str)
{
  if (str != NULL)
  {
    while (*str != '\0')
    {
      *str = tolower(*str);
      str++;
    }
  }
}

/*****************************************************************************
** NAME: ftw_cb
**
** Callback function used with "file tree walk" operation invoked by local_rmdir().
**
*****************************************************************************/
static int ftw_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
  return remove(fpath);
}

/*****************************************************************************
** NAME: local_rmdir
**
** Remove a directory and everything underneath it.
**
*****************************************************************************/
static int local_rmdir(char *dir)
{
  int rc;
  struct stat fstat;

  if (stat(dir, &fstat) == 0)
  {
    rc = nftw(dir, ftw_cb, 64, FTW_DEPTH | FTW_PHYS);
    if (rc < 0)
    {
      printf("Error: Unable to remove directory %s\n", dir);
      return -1;
    }

    dbgprintf("Removed directory %s\n", dir);
  }
  return 0;
}

/*****************************************************************************
** NAME: local_mkdir
**
** Create a directory.
**
*****************************************************************************/
static int local_mkdir(char *dir)
{
  /* remove existing directory */
  if (local_rmdir(dir) < 0)
  {
    return -1;
  }

  if (mkdir(dir, 0755) < 0)
  {
    printf("Error: Unable to create directory %s\n", dir);
    return -1;
  }

  dbgprintf("Created directory %s\n", dir);
  return 0;
}

/*****************************************************************************
** NAME: get_file_line
**
** Read the designated file and extract the line that matches the specified substring.
** 
** NOTE: This function returns a ptr to an internal buffer and is non-reentrant.
** 
*****************************************************************************/
static char *get_file_line(char *filename, char *match)
{
  static char line[FGETS_LINE_MAX] = { 0 };
  FILE *fp = NULL;
  char *p_line = NULL;

  fp = fopen(filename, "r");
  if (fp == NULL)
  {
    printf("Error: Unable to open %s file for reading.\n", filename);
    return NULL;
  }

  /* Find the matching line using a substring compare.
  */
  while (fgets(line, sizeof(line), fp) != NULL)
  {
    if (strstr(line, match) != NULL)
    {
      p_line = line;
      break;
    }
  } /* endwhile */

  if (fclose(fp) != 0)
  {
    printf("Error closing %s file.\n", filename);
    return NULL;
  }

  return p_line;
}

/*****************************************************************************
** NAME: parse_cmd_line
**
** Read parameters passed to the program.
**
*****************************************************************************/
int parse_cmd_line(int argc, char *argv[],
                   char *agt_path,
                   char *old_agt_name,
                   img_version_t *ver_cmd_parms,
                   char *stk_filename)
{
  int arg;
  char *p_arg;
  char *p_arg2;
  char *str;
  int val;
  int len;

  /* Look for -h to display help (ignore everything else).
  */
  for (arg = 1; arg < argc; arg++)
  {
    if (strcmp("-h", argv[arg]) == 0)
    {
      usage();
      exit(0);
      /* does not return */
    }
  }

  /* Copy tools path from command arg.
  ** 
  ** NOTE: If argv[0] begins with a "." (including "..") then tools path is 
  **       relative to CWD. Use env_top string to fully specify tools path
  **       to avoid problems with issuing commands from within subdirectories
  **       of CWD. If it begins with a "/" then it is a non-relative path,
  **       so copy the entire path. If neither of these, assume the command
  **       is accessed through the PATH environment variable, so use an
  **       empty string for the tools_path.
  */
  if (*argv[0] == '.')
  {
    snprintf(tools_path, sizeof(tools_path), "%s/%s", env_top, argv[0]);
  }
  else if (*argv[0] == '/')
  {
    snprintf(tools_path, sizeof(tools_path), "%s", argv[0]);
  }
  else
  {
    *tools_path = '\0';
  }
  if (*tools_path != '\0')
  {
    str = strrchr(tools_path, '/');
    if ((str == NULL) ||
        (str == tools_path))
    {
      printf("\nError: Invalid tools path specified in command \'%s\'.\n", argv[0]);
      return -1;
    }
    /* keep the trailing '/' */
    *(str + 1) = '\0';
  }

  /* The last arg must be the name of the .stk file.
  */
  p_arg = argv[argc - 1];
  str = strrchr(p_arg, '.');
  if ((argc < 2) ||
      (str == NULL) ||
      (strcmp(str, ".stk") != 0) ||
      (strlen(p_arg) <= strlen(".stk")))
  {
    printf("\nError: The .stk file name must be specified as the last command argument.\n");
    return -1;
  }

  /* Output the .stk file arg and check for optional args.
  */
  strcpy(stk_filename, p_arg);
  argc--;

  /* start with first real arg */
  for (arg = 1; arg < argc; arg++)
  {
    p_arg = argv[arg];
    p_arg2 = argv[arg + 1];

    if (strcmp("-a", p_arg) == 0)
    {
      strcpy(agt_path, p_arg2);
      arg++;
    }

    if (strcmp("-n", p_arg) == 0)
    {
      strcpy(old_agt_name, p_arg2);
      arg++;
    }

    else if (strcmp("-d", p_arg) == 0)
    {
      show_dbg = 1;
      cmd_output_ctrl = CMD_OUTPUT_ALLOW;
    }

    else if (strcmp("-r", p_arg) == 0)
    {
      if (isdigit(*p_arg2))
      {
        val = atoi(p_arg2);
        /* check all but first char (was already checked above) */
        len = strlen(p_arg2);
        while (--len > 0)
        {
          if (!isdigit(p_arg2[len]))
          {
            val = -1;
            break;
          }
        }
        /* check for an integer from 0-63 */
        if ((val < VER_PART_MIN) || (val > VER_PART_MAX))
        {
          printf("Error: %s numeric value must be from %d-%d.\n", p_arg, VER_PART_MIN, VER_PART_MAX);
          return -1;
        }
      }
      else if (isalpha(*p_arg2))
      {
        /* must be a string, only 1 or 2 alpha chars allowed */
        len = strlen(p_arg2);
        if ((len == 0) || (len > VER_PART_ALPHA_LEN_MAX))
        {
          printf("Error: -r alpha string too %s.\n", (len == 0) ? "short" : "long");
          return -1;
        }

        /* check all but first char (was already checked above) */
        while (--len > 0)
        {
          if (!isalpha(p_arg2[len]) &&
              !isdigit(p_arg2[len]))
          {
            printf("Error: -r alpha string value invalid.\n");
            return -1;
          }
        }
      }
      else
      {
        printf("Error: -r value must be %d-%d or a letter.\n", VER_PART_MIN, VER_PART_MAX);
        return -1;
      }

      strcpy(ver_cmd_parms->rel_str, p_arg2);
      arg++;
    }

    else if (strstr("-v -m -b", p_arg) != NULL)
    {
      if (!isdigit(*p_arg2))
      {
        printf("Error: %s value must be a number from %d-%d.\n", p_arg, VER_PART_MIN, VER_PART_MAX);
        return -1;
      }
      /* check for an integer from 0-63 */
      val = atoi(p_arg2);
      if ((val < VER_PART_MIN) || (val > VER_PART_MAX))
      {
        printf("Error: %s value must be %d-%d.\n", p_arg, VER_PART_MIN, VER_PART_MAX);
        return -1;
      }
      /* set the appropriate output value */
      if (strcmp("-v", p_arg) == 0)
      {
        ver_cmd_parms->ver = val;
      }
      else if (strcmp("-m", p_arg) == 0)
      {
        ver_cmd_parms->maint = val;
      }
      else if (strcmp("-b", p_arg) == 0)
      {
        ver_cmd_parms->bld = val;
      }
      else
      {
        printf("Error: Option %s is invalid.\n", p_arg);
        return -1;
      }
      arg++;
    }

    else
    {
      printf("Error: %s is an unrecognized parameter.\n", p_arg);
      return -1;
    }

  } /* endfor */

  dbgprintf("\nGot argc=%d:\n", argc);
  for (arg = 0; arg < argc; arg++)
  {
    dbgprintf("  [%d]: %s\n", arg, argv[arg]);
  }
  dbgprintf("\n");

  return 0;
}

/*****************************************************************************
** NAME: read_stk_hdr
**
** Output contents of .stk file header.
** 
** NOTE: All header fields are in NETWORK byte order.
**
*****************************************************************************/
int read_stk_hdrs(char *stk_filename, stkFileHeader_t *stkhdr, stkOprFileInfo_t *imghdr)
{
  FILE *fp = NULL;
  int rc;
  int  i;
  int  num_components;
  stkOprFileInfo_t stk_opr_file_info;

  fp = fopen(stk_filename, "rb");
  if (fp == NULL)
  {
    printf("Error: Unable to open %s file.\n", stk_filename);
    return -1;
  }

  /* fread returns number of items read (not bytes read) */
  if (fread(stkhdr, sizeof(*stkhdr), 1, fp) != 1)
  {
    printf("Error: Unable to read %s file header.\n", stk_filename);
    return -1;
  }

  /* 
  * NOTE: The ntohl() and ntohs() functions may actually be macros that are
  *       #defined away as nothing.  Using an explicit typecast for each
  *       of these so that the value is always of the expected type to
  *       match the format string.
  */

  dbgprintf("CRC: 0x%x\n",
            (unsigned short)ntohs(stkhdr->crc));
  dbgprintf("Tag1: 0x%x\n",
            (unsigned short)ntohs(stkhdr->tag1));
  dbgprintf("Tag2: 0x%x\n",
            (unsigned int)ntohl(stkhdr->tag2));

  num_components = ntohl(stkhdr->num_components);
  dbgprintf("Num Components: %d\n", num_components);

  dbgprintf("File Size: %u\n",
            (unsigned int)ntohl (stkhdr->file_size));
  dbgprintf("Rel: %d, Ver: %d, Maint Level: %d, Build Num: %d\n",
            stkhdr->rel,
            stkhdr->ver,
            stkhdr->maint_level,
            stkhdr->build_num);
  dbgprintf("STK Header Size: %u\n",
            (unsigned int)ntohl(stkhdr->stk_header_size));

  i = 0;
  dbgprintf("\nComponent %d:\n", i+1);
  rc = fread(&stk_opr_file_info, sizeof(stk_opr_file_info), 1, fp);
  if (rc != 1)
  {
    printf("Error: Unable to read %s image file header.\n", stk_filename);
    return -1;
  }

  dbgprintf("Offset: %u\n",
            (unsigned int)ntohl (stk_opr_file_info.offset));
  dbgprintf("Target Device: 0x%x\n",
            (unsigned int)ntohl (stk_opr_file_info.target_device));

  switch (ntohl(stk_opr_file_info.os))
  {
    case STK_OS_VXWORKS:
      dbgprintf("OS: VxWorks\n");
      break;
    case STK_OS_LINUX:
      dbgprintf("OS: Linux\n");
      break;
    default:
      dbgprintf("OS: %u : Unknown.\n",
                (unsigned int)ntohl(stk_opr_file_info.os));
      break;
  }

  dbgprintf("STK Image Flags: 0x%08x\n\n",
            (unsigned int)ntohl(stk_opr_file_info.image_flags));

  memcpy(imghdr, &stk_opr_file_info, sizeof(*imghdr));

  rc = fclose(fp);
  if (rc < 0)
  {
    printf("Error: Unable to close %s file.\n", stk_filename);
    return -1;
  }

  return 0;
}

/*****************************************************************************
** NAME: extract_uim_file
**
** Extract the operational code uimage file (.uim) from the STK file.
** 
*****************************************************************************/
int extract_uim_file(char *stk_filename, unsigned int img_offset, unsigned int *uim_length)
{
  int rc = 0;
  FILE *fin = NULL;
  FILE *fout = NULL;
  char buf[FREAD_CHUNK] = { 0 };
  size_t totBytes = 0;
  size_t ret;
  size_t chunk;

  ret = 1;          /* init to enter while loop below */

  fin = fopen(stk_filename, "rb");
  fout = fopen(p_file_uim, "wb");

  if (fin == NULL)
  {
    printf("Error: Unable to open %s file for reading.\n", stk_filename);
    rc = -1;
  }
  if (fout == NULL)
  {
    printf("Error: Unable to open %s file for writing.\n", p_file_uim);
    rc = -1;
  }

  /* Start at the specified offset of the input file. 
  */
  if (fseek(fin, img_offset, SEEK_SET) < 0)
  {
    printf("Error: Cannot access offset %u in input file %s.\n", img_offset, stk_filename);
    rc = -1;
  }

  while ((rc == 0) && (ret > 0))
  {
    ret = fread(buf, 1, FREAD_CHUNK, fin);

    if (ret < FREAD_CHUNK)
    {
      /* either got partial read or error -- figure out which */
      if (ferror(fin) || !feof(fin))
      {
        printf("Error reading %s file.\n", stk_filename);
        rc = -1;
        break;
      }

      /* check for EOF with no more bytes read */
      if (ret == 0)
      {
        break;
      }
    }

    /* Append bytes read to the UIM file.
    */
    chunk = ret;
    totBytes += chunk;
    ret = fwrite(buf, 1, chunk, fout);
    if (ret != chunk)
    {
      printf("Error writing %d bytes to %s file.\n", (int)chunk, p_file_uim);
      totBytes -= chunk;
      rc = -1;
      break;
    }

    /* check for last partial write */
    if (ret < FREAD_CHUNK)
    {
      break;
    }

  } /* endwhile */

  if (fout != NULL)
  {
    if (fclose(fout) != 0)
    {
      printf("Error closing %s file.\n", p_file_uim);
      rc = -1;
    }
  }
  if (fin != NULL)
  {
    if (fclose(fin) != 0)
    {
      printf("Error closing %s file.\n", stk_filename);
      rc = -1;
    }
  }

  if (uim_length != NULL)
  {
    *uim_length = (unsigned int)totBytes;
  }

  return rc;
}

/*****************************************************************************
** NAME: parse_mkimage_traditional
**
** Parse a uimage status text file based on traditional format.
** 
*****************************************************************************/
int parse_mkimage_traditional(FILE *fp, char *uiminfo_filename, mkimage_parms_t *mkimage_parms)
{
  const char *delim     = ":";
  char *p_image_name    = "Image Name";
  char *p_image_type    = "Image Type";
  char *p_load_address  = "Load Address";
  char *p_entry_point   = "Entry Point";
  char *p_image         = "Image ";             /* NOTE: use strstr() for this */
  int firmware_part     = -1;
  uim_part_info_t *uim_part;
  char line[FGETS_LINE_MAX];
  char valstr[FGETS_LINE_MAX];
  int val;
  unsigned int uval;
  char *tok;

  memset(line, 0, sizeof(line));

  if (fp == NULL)
  {
    return -1;
  }

  /* Allow the line "leader" strings to appear in any order. Although the order
  ** is probably invariant, doing it this way allows for some additional lines 
  ** to be added/inserted by 'mkimage' without affecting this algorithm.
  */

  /* Whenever a line match occurs, its "leader" string ptr is set to
  ** NULL to avoid doing further comparisons on it. 
  **  
  ** Recall, line buffer contains trailing '\n' if one was read from the file. 
  */
  while (fgets(line, sizeof(line), fp) != NULL)
  {
    if ((tok = strtok(line, delim)) == NULL)
    {
      continue;
    }

    /* Image Name: extract IMG_NAME and CPU.
    */
    if ((p_image_name != NULL) &&
        (strcmp(p_image_name, tok) == 0))
    {
      tok += (strlen(tok) + 1);
      if (sscanf(tok, " System for %s", valstr) == 1)
      {
        convert_tolower(valstr);
        snprintf(mkimage_parms->img_name, sizeof(mkimage_parms->img_name) - 1, "System for %s", valstr);
        strncpy(mkimage_parms->cpu, valstr, sizeof(mkimage_parms->cpu) - 1);
      }
      p_image_name = NULL;
      continue;
    }

    /* Image Type: extract ARCH and CMPR.
    */
    if ((p_image_type != NULL) &&
        (strcmp(p_image_type, tok) == 0))
    {
      tok += (strlen(tok) + 1);
      if (sscanf(tok, " %s ", valstr) == 1)
      {
        convert_tolower(valstr);
        strncpy(mkimage_parms->arch, valstr, sizeof(mkimage_parms->arch) - 1);
        if (strncmp(mkimage_parms->arch, "aarch64", sizeof(mkimage_parms->arch)) == 0)
        {
          printf("\nSetting ARCH to arm64\n");
          strncpy(mkimage_parms->arch, "arm64", sizeof(mkimage_parms->arch));
        }
      }
      if ((tok = strtok(NULL, "(")) != NULL)
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, " %s ", valstr) == 1)
        {
          convert_tolower(valstr);
          strncpy(mkimage_parms->cmpr, valstr, sizeof(mkimage_parms->cmpr) - 1);
        }
      }
      p_image_type = NULL;
      continue;
    }


    /* Load Address: extract START.
    */
    if ((p_load_address != NULL) &&
        (strcmp(p_load_address, tok) == 0))
    {
      tok += (strlen(tok) + 1);
      if (sscanf(tok, "%x", &uval) == 1)
      {
        mkimage_parms->start = uval;
      }
      p_load_address = NULL;
      continue;
    }

    /* Entry Point: extract ENTRY.
    */
    if ((p_entry_point != NULL) &&
        (strcmp(p_entry_point, tok) == 0))
    {
      tok += (strlen(tok) + 1);
      if (sscanf(tok, "%x", &uval) == 1)
      {
        mkimage_parms->entry = uval;
      }
      p_entry_point = NULL;
      continue;
    }

    /* Image %d: extract IMG_SZ.
    */
    if ((p_image != NULL) &&
        (strstr(tok, p_image) != NULL))
    {
      if (sscanf(tok, " %*s %d", &val) == 1)
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, "%u", &uval) == 1)
        {
          if (val < IMG_PARTS_MAX)
          {
            uim_part = &mkimage_parms->parts[val];
            snprintf(uim_part->part_name, sizeof(uim_part->part_name) - 1, "part%d", val);
            uim_part->part_size = uval;
            mkimage_parms->num_parts++;

            /* assume that the largest uimage part size is the code image (.tgz) file */
            if (uval > mkimage_parms->code_img_size)
            {
              firmware_part = val;
              mkimage_parms->code_img_size = uval;
            }
          }
        }
      }
      /* do not set p_image to NULL here (use to check multiple parts) */
      continue;
    }

  } /* endwhile */

  /* Compare size of image parts 3 and 4 and choose the larger
  ** as the location of the actual code image file.
  */
  if (firmware_part < 0)
  {
    printf("Error: Unable to determine code image part in %s file.\n", uiminfo_filename);
    return -1;
  }
  mkimage_parms->code_img_part = (unsigned int)firmware_part;
    
  /* Display the mkimage parms obtained.
  */
  dbgprintf("Obtained mkimage parms from %s file:\n", uiminfo_filename);
  dbgprintf("  IMG_NAME=\"%s\"\n", mkimage_parms->img_name);
  dbgprintf("  CPU=%s\n", mkimage_parms->cpu);
  dbgprintf("  ARCH=%s\n", mkimage_parms->arch);
  dbgprintf("  CMPR=%s\n", mkimage_parms->cmpr);
  dbgprintf("  START=%8.8x (hex)\n", mkimage_parms->start);
  dbgprintf("  ENTRY=%8.8x (hex)\n", mkimage_parms->entry);
  for (val = 0; val < IMG_PARTS_MAX; val++)
  {
    uim_part = &mkimage_parms->parts[val];
    if (uim_part->part_size > 0)
    {
      dbgprintf("  [%d] %s IMG_SZ=%u\n", val, uim_part->part_name, uim_part->part_size);
    }
  }
  printf("Image part %u contains TGZ file data (%u Bytes).\n",
         mkimage_parms->code_img_part, mkimage_parms->code_img_size);

  return 0;
}

/*****************************************************************************
** NAME: parse_mkimage_fit
**
** Parse a uimage status text file based on FIT format.
** 
*****************************************************************************/
int parse_mkimage_fit(FILE *fp, char *uiminfo_filename, mkimage_parms_t *mkimage_parms)
{
  const char *delim1    = ":";
  const char *delim2    = "()";
  char *p_descr         = "Description";
  char *p_data_size     = "Data Size";
  char *p_compress      = "Compression";
  char *p_arch          = "Architecture";
  char *p_load_address  = "Load Address";
  char *p_entry_point   = "Entry Point";
  char *p_firmware_name = "switchdrvr";         /* NOTE: use strstr() for this */
  int part_idx          = 0;
  int firmware_part     = -1;
  uim_part_info_t *uim_part;
  char line[FGETS_LINE_MAX];
  char valstr[FGETS_LINE_MAX];
  int val;
  unsigned int uval;
  char *tok;

  memset(line, 0, sizeof(line));
  uim_part = mkimage_parms->parts;

  if (fp == NULL)
  {
    return -1;
  }

  /* Process the info file in a predictable pattern, looking for
  ** an "Image N" entry to set the parts array index and the 
  ** obtain the image name, followed by a "Data Size" entry 
  ** that gives the size of that image in bytes. This code 
  ** assumes that general pattern is repeated throughout the 
  ** info file for the FIT image format. 
  **  
  ** Also watch for the "switchdrvr" entry in the "Description" field.  
  */

  /* Recall, line buffer contains trailing '\n' if one was read from the file.
  */
  while (fgets(line, sizeof(line), fp) != NULL)
  {

    /* Image N (name): extract part index and file name.
    */
    if (sscanf(line, " Image %d %s", &part_idx, valstr) == 2)
    {
      if ((tok = strtok(valstr, delim2)) != NULL)
      {
        /* change uim_part ptr per updated index value */
        uim_part = &mkimage_parms->parts[part_idx];
        snprintf(uim_part->part_name, sizeof(uim_part->part_name) - 1, "%s", tok);
        mkimage_parms->num_parts++;
      }
      continue;
    }

    if ((tok = strtok(line, delim1)) == NULL)
    {
      continue;
    }

    /* Data Size: extract size.
    */
    if (strstr(tok, p_data_size) != NULL)
    {
      tok += (strlen(tok) + 1);
      if (sscanf(tok, "%d Bytes", &uval) == 1)
      {
        uim_part->part_size = uval;
        if (part_idx == firmware_part)
        {
          mkimage_parms->code_img_size = uval;
        }
      }
      continue;
    }

    /* Description: watch for 'switchdrvr' and note its index value.
    */
    if (strstr(tok, p_descr) != NULL)
    {
      tok += (strlen(tok) + 1);
      if (strstr(tok, p_firmware_name) != NULL)
      {
        firmware_part = part_idx;
        snprintf(mkimage_parms->img_name, sizeof(mkimage_parms->img_name) - 1, "%s", p_firmware_name);
        printf("Found \'%s\' firmware in uimage part %d.\n", p_firmware_name, firmware_part);
      }
      continue;
    }

    /* The following attributes are only of interest for the firmware image part.
    */
    if (part_idx == firmware_part)
    {
      /* Compression: extract CMPR.
      */
      if ((p_compress != NULL) &&
          (strstr(tok, p_compress) != NULL))
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, " %s ", valstr) == 1)
        {
          convert_tolower(valstr);
          strncpy(mkimage_parms->cmpr, valstr, sizeof(mkimage_parms->cmpr) - 1);
        }
        p_compress = NULL;
        continue;
      }

      /* Architecture: extract ARCH.
      */
      if ((p_arch != NULL) &&
          (strstr(tok, p_arch) != NULL))
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, " %s ", valstr) == 1)
        {
          convert_tolower(valstr);
          strncpy(mkimage_parms->arch, valstr, sizeof(mkimage_parms->arch) - 1);
        }
        p_arch = NULL;
        continue;
      }

      if (strncmp(mkimage_parms->arch, "AArch64", sizeof(mkimage_parms->arch)) == 0)
      {
        printf("\nGot AArch64, replacing with arm\n");
        strncpy(mkimage_parms->arch, "arm", sizeof(mkimage_parms->arch));
      }

      /* Load Address: extract START.
      */
      if ((p_load_address != NULL) &&
          (strstr(tok, p_load_address) != NULL))
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, "%x", &uval) == 1)
        {
          mkimage_parms->start = uval;
        }
        p_load_address = NULL;
        continue;
      }

      /* Entry Point: extract ENTRY.
      */
      if ((p_entry_point != NULL) &&
          (strstr(tok, p_entry_point) != NULL))
      {
        tok += (strlen(tok) + 1);
        if (sscanf(tok, "%x", &uval) == 1)
        {
          mkimage_parms->entry = uval;
        }
        p_entry_point = NULL;
        continue;
      }
    } /* endif within firmware_part */

  } /* endwhile */

  /* Check if firmware image part was found.
  */
  if (firmware_part < 0)
  {
    printf("Error: Unable to locate firmware part in %s file.\n", uiminfo_filename);
    return -1;
  }
  mkimage_parms->code_img_part = (unsigned int)firmware_part;
    
  /* Display the mkimage parms obtained.
  */
  dbgprintf("Obtained mkimage parms from %s file:\n", uiminfo_filename);
  dbgprintf("  IMG_NAME=\"%s\"\n", mkimage_parms->img_name);
  dbgprintf("  CPU=%s\n", "N/A");
  dbgprintf("  ARCH=%s\n", mkimage_parms->arch);
  dbgprintf("  CMPR=%s\n", mkimage_parms->cmpr);
  dbgprintf("  START=%8.8x (hex)\n", mkimage_parms->start);
  dbgprintf("  ENTRY=%8.8x (hex)\n", mkimage_parms->entry);
  for (val = 0; val < IMG_PARTS_MAX; val++)
  {
    uim_part = &mkimage_parms->parts[val];
    if (uim_part->part_size > 0)
    {
      dbgprintf("  [%d] %s IMG_SZ=%u\n", val, uim_part->part_name, uim_part->part_size);
    }
  }
  printf("FIT image part %u contains TGZ file data (%u Bytes).\n",
         mkimage_parms->code_img_part, mkimage_parms->code_img_size);

  return 0;
}

/*****************************************************************************
** NAME: get_mkimage_parms
**
** Parse the uimage status text file that was previously created and
** obtain the various parameters that were used with the 'mkimage'
** command when the .uim file was built.
** 
** NOTE: This function relies on the format of the information
**       produced by the 'mkimage -l' command output. While this
**       should not change very often, it could impact the success
**       of this utility if it does change.
** 
*****************************************************************************/
int get_mkimage_parms(char *uiminfo_filename, mkimage_parms_t *mkimage_parms)
{
  int rc = -1;
  const char *delim = ":";
  char *p_fit_descr = "FIT description";
  char *p_msg_fmt = "%s uimage format detected.\n";
  FILE *fp = NULL;
  char line[FGETS_LINE_MAX];
  char *tok;

  memset(line, 0, sizeof(line));

  /* Allow the line "leader" strings to appear in any order. Although the order
  ** is probably invariant, doing it this way allows for some additional lines 
  ** to be added/inserted by 'mkimage' without affecting this algorithm.
  */

  fp = fopen(uiminfo_filename, "r");
  if (fp == NULL)
  {
    printf("Error: Unable to open %s file for reading.\n", uiminfo_filename);
    return -1;
  }

  /* Preview first line of status text file to determine whether to
  ** process it as FIT format or traditional format.
  */
  if ((fgets(line, sizeof(line), fp) == NULL) ||
      ((tok = strtok(line, delim)) == NULL))
  {
    printf("Error: reading first line of file %s.\n", uiminfo_filename);
    (void)fclose(fp);
    return -1;
  }

  /* Reset file ptr back to beginning to allow parsing functions
  ** to start at the top.
  */
  if (fseek(fp, 0, SEEK_SET) < 0)
  {
    printf("Error: resetting pointer to top of file %s.\n", uiminfo_filename);
    (void)fclose(fp);
    return -1;
  }

  /* FIT Description: indicates FIT file format.
  */
  if (strcmp(p_fit_descr, tok) == 0)
  {
    mkimage_parms->is_fit_format = 1;
    printf(p_msg_fmt, "FIT");
    rc = parse_mkimage_fit(fp, uiminfo_filename, mkimage_parms);
  }
  else
  {
    printf(p_msg_fmt, "Traditional");
    rc = parse_mkimage_traditional(fp, uiminfo_filename, mkimage_parms);
  }

  if (fclose(fp) != 0)
  {
    printf("Error closing %s file.\n", uiminfo_filename);
    return -1;
  }

  return rc;
}

/*****************************************************************************
** NAME: get_vpd_version
**
** Read the fastpath.vpd file and extract the r, v, m, and b version part values.
** 
*****************************************************************************/
int get_vpd_version(char *vpd_filename, img_version_t *vpd_parms)
{
  char *delim = ",";
  int ret = 0;
  char *line;
  img_version_t tmp;
  char *tok;

  memset(&tmp, 0, sizeof(tmp));

  /* Find the "Rel" line since that contains the version part fields.
  */
  line = get_file_line(vpd_filename, "Rel ");
  if (line == NULL)
  {
    printf("Error: File %s does not contain proper version information.\n", vpd_filename);
    return -1;
  }

  tok = strtok(line, delim);
  ret += sscanf(tok, " Rel %s", tmp.rel_str);
  tok = strtok(NULL, delim);
  ret += sscanf(tok, " Ver %d", &tmp.ver);
  tok = strtok(NULL, delim);
  ret += sscanf(tok, " Maint Lev %d", &tmp.maint);
  tok = strtok(NULL, delim);
  ret += sscanf(tok, " Bld No %d", &tmp.bld);
  if (ret != 4)
  {
    printf("Error: Unable to read VPD version from %s file (ret=%d).\n", vpd_filename, ret);
    return -1;
  }

  *vpd_parms = tmp;

  /* Display the VPD version.
  */
  printf("Old VPD version: Rel %s, Ver %d, Maint Lev %d, Bld No %d\n",
         vpd_parms->rel_str, vpd_parms->ver, vpd_parms->maint, vpd_parms->bld);

  return 0;
}

/*****************************************************************************
** NAME: get_new_vpd_image_name
**
** Read the fastpath.vpd file and extract the prefix part of the image file
** name (i.e. with the r, v, m, and b suffix parts removed). Using the
** specified version info, construct a new image file name.
** 
*****************************************************************************/
int get_new_vpd_image_name(char *vpd_filename, img_version_t *vpd_ver, img_version_t *new_ver,
                           char *buf, size_t bufsize)
{
  char *opercode_fmt = " Operational Code Image File Name - %s";
  vpd_parse_fmt_t vpd_fmt[] =
  {
    { "r%sv%d", "r%sv%dm%d", "%s%sb%d" },
    { "%s.%d",  "%s.%d.%d",  "%s%s.%d" }
  };
  int parse_fmt_max = sizeof(vpd_fmt) / sizeof(vpd_fmt[0]);
  vpd_parse_fmt_t *parse_fmt;
  char img_filename[FILENAME_LEN_MAX] = { 0 };
  char verstr[VER_STR_SIZE];
  int i;
  char *line;
  char *sfx;

  if ((vpd_filename == NULL) ||
      (vpd_ver == NULL) ||
      (new_ver == NULL) ||
      (buf == NULL))
  {
    printf("Internal Error: %s invalid parameter\n", __FUNCTION__);
    return -1;
  }

  memset(&verstr, 0, sizeof(verstr));

  /* Find the line that contains the image file name.
  */
  line = get_file_line(vpd_filename, " Operational Code Image ");
  if (line == NULL)
  {
    printf("Error: File %s does not contain image file name.\n", vpd_filename);
    return -1;
  }

  if (sscanf(line, opercode_fmt, img_filename) != 1)
  {
    printf("Error: File %s image file name not found.\n", vpd_filename);
    return -1;
  }

  /* Create a version string containing the r, v parts of the VPD version
  ** to use as a search key to find the suffix portion of the image file name. 
  ** If found, overwrite the beginning of the suffix with a null string 
  ** terminator to isolate the image name prefix.
  **  
  ** NOTE: There are two common version formats used for the .stk file name, 
  **       engineering builds "rxvxmx[bx].stk" vs. customer builds "x.x.x[.x].stk".
  */
  sfx = NULL;
  parse_fmt = NULL;
  for (i = 0; i < parse_fmt_max; i++)
  {
    parse_fmt = &vpd_fmt[i];
    snprintf(verstr, sizeof(verstr), parse_fmt->old_fmt, vpd_ver->rel_str, vpd_ver->ver);
    sfx = strstr(img_filename, verstr);
    if (sfx != NULL)
    {
      break;
    }
  } /* endfor */
  if ((sfx == NULL) || (parse_fmt == NULL))
  {
    printf("Error: File %s image file name format invalid: %s.\n", vpd_filename, img_filename);
    return -1;
  }
  *sfx = '\0';

  /* Construct a new image file name from the prefix string and the
  ** new version info provided by the caller. 
  **  
  ** NOTE: If the new version bld value is 0, omit the 'b' part from
  **       the image name. 
  */
  snprintf(verstr, sizeof(verstr), parse_fmt->new_fmt, new_ver->rel_str, new_ver->ver, new_ver->maint);
  if (new_ver->bld != 0)
  {
    snprintf(buf, bufsize, parse_fmt->new_fmt_b, img_filename, verstr, new_ver->bld);
  }
  else
  {
    snprintf(buf, bufsize, "%s%s", img_filename, verstr);
  }
  *(buf + bufsize - 1) = '\0';

  return 0;
}

/*****************************************************************************
** NAME: write_vpd_file
** 
** Read the fastpath.vpd file line by line, rewriting those that contain 
** version or timestamp information. 
**  
** NOTE: This function relies on the fixed-format entries in fastpath.vpd.
**       Only some of the lines are modified, the others remain unchanged.
** 
*****************************************************************************/
int write_vpd_file(char *new_filename, char *verstr, char *timestamp)
{
  int rc = 0;
  char *p_spacer  = "         ";
  char *p_oper    = " Operational ";
  char *p_rel     = " Rel ";
  char *p_time    = " Timestamp ";
  char *vpd_filename = p_file_vpd;
  char *tmp_filename = p_file_vpdtmp;
  FILE *fin = NULL;
  FILE *fout = NULL;
  char *delim = "-";
  char line[FGETS_LINE_MAX];
  char valstr[FGETS_LINE_MAX];
  char md5_filename[FILENAME_LEN_MAX] = { 0 };
  char *tok;

  memset(line, 0, sizeof(line));

  fin = fopen(vpd_filename, "r");
  fout = fopen(tmp_filename, "w");

  if (fin == NULL)
  {
    printf("Error: Unable to open %s file for reading.\n", vpd_filename);
    rc = -1;
  }
  if (fout == NULL)
  {
    printf("Error: Unable to open %s file for writing.\n", tmp_filename);
    rc = -1;
  }

  /* Read each line and for any that do not match one of the "leader" strings,
  ** write it out unmodified. Use the input parms to replace the matching 
  ** lines with modified content. 
  ** 
  ** Whenever a line match occurs, its "leader" string ptr is set to
  ** NULL to avoid doing further comparisons on it. 
  **  
  ** Recall, line buffer contains trailing '\n' if one was read from the file. 
  */
  while ((rc == 0) && fgets(line, sizeof(line), fin) != NULL)
  {

    /* Operational Code Image File Name: rewrite new image file name.
    */
    if ((p_oper != NULL) &&
        (strstr(line, p_oper) != NULL))
    {
      tok = strtok(line, delim);
      if (tok != NULL)
      {
        snprintf(valstr, sizeof(valstr), "%s - %s\n", tok, new_filename);
        snprintf(line, sizeof(line), "%s", valstr);
      }
      p_oper = NULL;
    }

    /* Rel x: rewrite new version string.
    */
    else if ((p_rel != NULL) &&
             (strstr(line, p_rel) != NULL))
    {
      snprintf(line, sizeof(line), "%s%s\n", p_spacer, verstr);
      p_rel = NULL;
    }

    /* Operational Code Image File Name: rewrite new image file name.
    */
    else if ((p_time != NULL) &&
             (strstr(line, p_time) != NULL))
    {
      tok = strtok(line, delim);
      if (tok != NULL)
      {
        snprintf(valstr, sizeof(valstr), "%s - %s\n", tok, timestamp);
        snprintf(line, sizeof(line), "%s", valstr);
      }
      p_time = NULL;
    }

    else
    {
      /* use the line unmodified */
    }

    /* Write the line back out (whether modified or not).
    */
    if (fputs(line, fout) < 0)
    {
      printf("Error writing line to %s file: %s.\n", tmp_filename, line);
      rc = -1;
    }

  } /* endwhile */

  if (fout != NULL)
  {
    if (fclose(fout) != 0)
    {
      printf("Error closing %s file.\n", p_file_vpdtmp);
      rc = -1;
    }
  }
  if (fin != NULL)
  {
    if (fclose(fin) != 0)
    {
      printf("Error closing %s file.\n", p_file_vpd);
      rc = -1;
    }
  }

  /* If successful thus far, replace the original VPD file
  ** in the TGZ files directory with the one just created 
  ** and update its companion md5sum file. 
  */
  if ((rc == 0) &&
      (access(tmp_filename, F_OK) == 0))
  {
    snprintf(md5_filename, sizeof(md5_filename), "%s.md5sum", vpd_filename);
    if ((system_cmd("cp -f %s %s %s ; ",
                    tmp_filename, vpd_filename, p_cmd_output_noerr) < 0) ||
        (system_cmd("md5sum %s >%s %s ; ",
                     vpd_filename, md5_filename, p_cmd_output_noerr) < 0))
    {
      printf("Error: %s file copy command failed.\n", vpd_filename);
      rc = -1;
    }
  }

  return rc;
}

/*****************************************************************************
** NAME: get_uimage_attrs
**
** Extract the operational code uimage file (.uim) from the STK file.
** 
*****************************************************************************/
int get_uimage_attrs(char *stk_filename, unsigned int img_offset,
                     mkimage_parms_t *mkimage_parms)
{
  /* Assume first image in .stk file is the .uim image and extract it.
  */
  printf("\nExtracting uimage file from %s starting at offset %d.\n", stk_filename, img_offset);
  if (extract_uim_file(stk_filename, img_offset, &mkimage_parms->uim_length) < 0)
  {
    return -1;
  }
  printf("Successfully extracted %s file (%u bytes).\n", FILE_UIM, mkimage_parms->uim_length);

  /* Use 'mkimage -l' utility to reproduce uimage creation parameters
  ** and output results to a text file for subsequent parsing.
  */
  if (system_cmd("mkimage -l %s >%s %s ;",
                 p_file_uim, p_file_uiminfo, p_cmd_output_noerr) < 0)
  {
    printf("Error: \'mkimage -l\' command failed.\n");
    return -1;
  }

  if (access(p_file_uiminfo, F_OK) != 0)
  {
    printf("Error: Unable to extract uimage status info from %s.\n", p_file_uiminfo);
    return -1;
  }
  printf("Successfully extracted uimage status info from %s.\n", FILE_UIM);

  /* Parse each line of the uimage status file to obtain essential
  ** data used as 'mkimage' input parameters when creating the rebuilt 
  ** image file later on. 
  */
  if (get_mkimage_parms(p_file_uiminfo, mkimage_parms) != 0)
  {
    return -1;
  }

  return 0;
}

/*****************************************************************************
** NAME: extract_tgz_files
**
** Extract fastpath.tgz from the STK file and split apart all the files
** it contains.
** 
*****************************************************************************/
int extract_tgz_files(char *stk_filename, mkimage_parms_t *mkimage_parms)
{
  unsigned int firmware_part;
  char firmware_file[FILENAME_LEN_MAX];
  char *fname;

  firmware_part = mkimage_parms->code_img_part;

  if (mkimage_parms->is_fit_format == 0)
  {
    /* ./tools/extimage -i */
    if (system_cmd("%s%s -i %s -o %s -n %u %s ;",
                   tools_path, CMD_EXTIMAGE, stk_filename, p_file_tgz, firmware_part, p_cmd_output_noerr) < 0)
    {
      printf("Error: TGZ extract \'%s\' command failed.\n", CMD_EXTIMAGE);
      return -1;
    }
  }
  else
  {
    if (access(p_file_uim, F_OK) != 0)
    {
      printf("Error: Required %s file does not exist.\n", p_file_uim);
      return -1;
    }
    fname = mkimage_parms->parts[firmware_part].part_name;
    snprintf(firmware_file, sizeof(firmware_file), "%s/%s", p_dir_tmp, fname);

    /* ./tools/extfitimage g */
    if (system_cmd("cd %s ; %s/%s %s g %s %s && chmod u+r %s ; cd - %s ;",
                   p_dir_tmp, tools_path, CMD_FITIMAGE, FILE_UIM, fname, CMD_OUTPUT_NONE, fname, CMD_OUTPUT_NONE) < 0)
    {
      printf("Error: TGZ extract \'%s\' command failed.\n", CMD_FITIMAGE);
      return -1;
    }
    if (system_cmd("cp -f %s %s ;", 
                   firmware_file, p_file_tgz) < 0)
    {
      printf("Error: File %s rename failed.\n", firmware_file);
      return -1;
    }
  }

  if (system_cmd("tar -C %s -xzf %s %s ;",
                 p_dir_tgz, p_file_tgz, p_cmd_output_noerr) < 0)
  {
    printf("Error: TGZ extract \'tar\' command failed.\n");
    return -1;
  }

  if (unlink(p_file_tgz) < 0)
  {
    printf("Error: Cannot delete %s file.\n", p_file_tgz);
    return -1;
  }

  printf("Successfully extracted %s files.\n\n", FILE_TGZ);

  return 0;
}

/*****************************************************************************
** NAME: update_vpd
**
** Read existing fastpath.vpd and construct new STK filename based on the
** version info and any version part input parameter(s) specified. Rewrite
** the VPD file with latest info, including an updated time stamp.
** 
*****************************************************************************/
int update_vpd(img_version_t *ver_cmd_parms, time_t *curtime, char *new_filename, size_t new_filename_size)
{
  char *vpd_filename = p_file_vpd;
  char *timestamp_fmt = "%a %b %d %H:%M:%S %Z %Y";
  char *ver_fmt = "Rel %s, Ver %d, Maint Lev %d, Bld No %d";
  struct tm *tm = NULL;
  char timestamp[TIMESTAMP_BUF_SIZE];
  char verstr[VER_STR_SIZE];
  img_version_t vpd_ver;
  img_version_t new_ver;

  memset(timestamp, 0, sizeof(timestamp));
  memset(verstr, 0, sizeof(verstr));

  if (get_vpd_version(vpd_filename, &vpd_ver) < 0)
  {
    return -1;
  }

  /* Use the version parts read unless a replacement input parameter
  ** value was specified.
  */
  new_ver = vpd_ver;
  if (strlen(ver_cmd_parms->rel_str) > 0)
  {
    strncpy(new_ver.rel_str, ver_cmd_parms->rel_str, sizeof(new_ver.rel_str));
  }
  if (ver_cmd_parms->ver != VER_PART_UNINIT)
  {
    new_ver.ver = ver_cmd_parms->ver;
  }
  if (ver_cmd_parms->maint != VER_PART_UNINIT)
  {
    new_ver.maint = ver_cmd_parms->maint;
  }
  if (ver_cmd_parms->bld != VER_PART_UNINIT)
  {
    new_ver.bld = ver_cmd_parms->bld;
  }

  /* Create timestamp string based on current time.
  */
  *curtime = time(NULL);
  if (*curtime != -1)
  {
    tm = localtime(curtime);
  }
  if ((tm == NULL) ||
      (strftime(timestamp, sizeof(timestamp), timestamp_fmt, tm) == 0))
  {
    printf("Error: Cannot read local time. Please avoid using \'-r 0\' as an input parameter.\n");
    return -1;
  }

  /* If rel_str "0" is specified, overwrite version parts based on current time.
  ** 
  ** NOTE: Allowing rel_str "00" to be used as-is. 
  */
  if (strcmp(new_ver.rel_str, "0") == 0)
  {
    snprintf(new_ver.rel_str, sizeof(new_ver.rel_str), "%d", tm->tm_mon + 1);
    new_ver.ver = tm->tm_mday;
    new_ver.maint = tm->tm_hour;
    new_ver.bld = tm->tm_min;
  }

  /* Create VPD file version string using new version info. Even if the bld part
  ** is omitted from the image name, it must still be included here (as 0).
  */
  snprintf(verstr, sizeof(verstr), ver_fmt, new_ver.rel_str, new_ver.ver, new_ver.maint, new_ver.bld);

  if (get_new_vpd_image_name(vpd_filename, &vpd_ver, &new_ver, new_filename, new_filename_size) < 0)
  {
    return -1;
  }

  dbgprintf("New VPD Version: %s\n", verstr);
  dbgprintf("New VPD Timestamp: %s\n", timestamp);
  printf("New Filename: %s\n", new_filename);

  if (write_vpd_file(new_filename, verstr, timestamp) < 0)
  {
    return -1;
  }

  return 0;
}

/*****************************************************************************
** NAME: remove_old_agent
**
** Removes files from the TGZ subdirectory that are associated with an old agent name.
** 
** NOTE: Assumes the default agent file bundle naming convention was used.
** 
** NOTE: As an added precaution, e.g. if old and new agent names are the same,
**       this function must be called before copy_agent_files().
** 
*****************************************************************************/
int remove_old_agent(char *old_agt_name, char *tgz_dir)
{
  /* Only proceed with removing old agent files if a name
  ** was provided as an input parameter.
  */
  if (strlen(old_agt_name) > 0)
  {
    printf("\nRemoving old agent files for %s.\n", old_agt_name);

    /* Unconditionally remove the following files that are
    ** associated with a typical agent bundle corresponding
    ** to the old agent name: 
    **   <name>.pre*
    **   <name>.tgz
    **   rc.agent.<name>*
    */
    system_cmd("rm -f %s/%s.pre* %s ;", tgz_dir, old_agt_name, p_cmd_output_noerr);
    system_cmd("rm -f %s/%s.tgz %s ;", tgz_dir, old_agt_name, p_cmd_output_noerr);
    system_cmd("rm -f %s/rc.agent.%s* %s ;", tgz_dir, old_agt_name, p_cmd_output_noerr);
  }

  return 0;
}

/*****************************************************************************
** NAME: copy_agent_files
**
** Copies each of the files in the designated agent directory to the TGZ files
** subdirectory.
** 
** NOTE: This does not look into subdirectories of the agent directory. Any
**       agent file hierarchy must be handled during agent script initialization
**       by expanding a tar file, etc.
** 
*****************************************************************************/
int copy_agent_files(char *agt_path, size_t agt_path_size, char *tgz_dir)
{
  int rc = 0;
  int is_first = 1;
  char filename[FILENAME_LEN_MAX] = { 0 };
  DIR *fdir = NULL;
  struct dirent *dp;
  struct stat fstat;

  printf("\n");

  /* If no agent path was provided as an input parm, use the ./agent
  ** location as a default.
  */
  if (strlen(agt_path) == 0)
  {
    snprintf(agt_path, agt_path_size, "%s", DIR_AGENT);
    printf("Using default agent path %s.\n", agt_path);
  }

  /* Walk the agent subdirectory, copying each regular file one
  ** at a time, printing the name along the way.
  */
  fdir = opendir(agt_path);
  if (fdir == NULL)
  {
    printf("Error: Agent directory \'%s\' not accessible or does not exist.\n", agt_path);
    return -1;
  }

  while ((dp = readdir(fdir)) != NULL)
  {
    /* Skip self (.) and parent (..) references.
    ** Also skip agent bundle tar file in case it 
    ** was not deleted from the agent/ work area. 
    */
    if ((strcmp(dp->d_name, ".") == 0) ||
        (strcmp(dp->d_name, "..") == 0) ||
        (strcmp(dp->d_name, agt_bundle_fname) == 0))
    {
      continue;
    }

    snprintf(filename, sizeof(filename), "%s/%s", agt_path, dp->d_name);

    if (is_first != 0)
    {
      is_first = 0;
      printf("Copying agent files from %s:\n", agt_path);
    }

    /* only copy regular files */
    if ((stat(filename, &fstat) < 0) ||
        !S_ISREG(fstat.st_mode))
    {
      dbgprintf("  %s (ignored)\n", dp->d_name);
      continue;
    }

    if (system_cmd("cp %s %s %s ;",
                   filename, tgz_dir, p_cmd_output_noerr) < 0)
    {
      printf("Error: copy command failed for agent file %s.\n", dp->d_name);
      rc = -1;
      break;
    }
    printf("  %s\n", dp->d_name);

  } /* endwhile */

  if (is_first != 0)
  {
    printf("No agent files found in %s.\n", agt_path);
  }

  printf("\n");

  if (fdir != NULL)
  {
    if (closedir(fdir) < 0)
    {
      printf("Error closing %s directory.\n", agt_path);
      rc = -1;
    }
  }

  return rc;
}

/*****************************************************************************
** NAME: find_its_filename
**
** Searches the specified directory for the first occurrence of an
** image tree source file named *.its.
** 
** NOTE: This does not look into any subdirectories and stops when it finds
**       the first .its file (there should be at most one).
** 
*****************************************************************************/
int find_its_filename(char *srch_dir, char *filename, size_t filename_size)
{
  int rc = -1;
  char *suffix = ".its";
  DIR *fdir = NULL;
  struct dirent *dp;
  char *str;

  /* Walk the agent subdirectory, copying each regular file one
  ** at a time, printing the name along the way.
  */
  fdir = opendir(srch_dir);
  if (fdir == NULL)
  {
    printf("Error: Directory \'%s\' not accessible or does not exist.\n", srch_dir);
    return -1;
  }

  while ((dp = readdir(fdir)) != NULL)
  {
    /* look specifically for a filename ending in ".its" */
    if (((str = strstr(dp->d_name, suffix)) != NULL) &&
        (strcmp(str, suffix) == 0))
    {
      /* found it -- copy file name to caller's output buffer */
      strncpy(filename, dp->d_name, filename_size);
      *(filename + filename_size - 1) = '\0';
      dbgprintf("Found %s file for FIT format uimage definition.\n", filename);
      rc = 0;
      break;
    }

  } /* endwhile */

  if (fdir != NULL)
  {
    if (closedir(fdir) < 0)
    {
      printf("Error closing %s directory.\n", srch_dir);
      rc = -1;
    }
  }

  return rc;
}

/*****************************************************************************
** NAME: modify_its_file
**
** Modified the specified image tree source file (*.its) in preparation
** for recreating a FIT format u-boot image.
** 
** NOTE: The path name used in each a 'data =' entry is changed to a
**       "./" relative path for use with this utility.
** 
*****************************************************************************/
int modify_its_file(char *its_file, char *src_dir, char *dst_dir, mkimage_parms_t *mkimage_parms)
{
  int rc = 0;
  int part_idx = 0;
  char *delim = "=";
  char *quote = "\"";
  char *match = "data ";
  char src[FILENAME_LEN_MAX];
  char dst[FILENAME_LEN_MAX];
  FILE *fp_src = NULL;
  FILE *fp_dst = NULL;
  char line[FGETS_LINE_MAX];
  char buf[FGETS_LINE_MAX];
  char *tok;
  char *tok1;
  char *fname;
  uim_part_info_t *uim_part;
  int i;

  /* The its_file obtained from the contents of fastpath.tgz is
  ** copied line-by-line to the dst_dir with each 'data =' entry 
  ** modified accordingly by changing its file path to a local 
  ** reference "./". This modified .its file is the one used 
  ** with mkimage to rebuild the u-boot image file from all 
  ** the individual image parts. 
  */

  snprintf(src, sizeof(src), "%s/%s", src_dir, its_file);
  snprintf(dst, sizeof(dst), "%s/%s", dst_dir, its_file);

  /* open both input and output files */
  fp_src = fopen(src, "r");
  if (fp_src == NULL)
  {
    printf("Error: Unable to open %s file for reading.\n", src);
    return -1;
  }

  fp_dst = fopen(dst, "w");
  if (fp_dst == NULL)
  {
    (void)fclose(fp_src);
    printf("Error: Unable to open %s file for writing.\n", dst);
    return -1;
  }

  /* Copy each line from src to dst, modifying the 'data =' line
  ** whenever it is encountered.
  */
  while (fgets(line, sizeof(line), fp_src) != NULL)
  {
    /* copy line for use with strtok */
    snprintf(buf, sizeof(buf), "%s", line);

    if (((tok1 = strtok(buf, delim)) != NULL) &&
        (strstr(tok1, match) != NULL))
    {
      /* Found a 'data =' line. Isolate the file name string
      ** that is enclosed in double quotes.
      */
      if (((tok = strtok(NULL, quote)) == NULL) ||
          ((tok = strtok(NULL, quote)) == NULL))
      {
        printf("Error: Malformed 'data =' line: %s.\n", line);
        rc = -1;
        break;
      }
      fname = basename(tok);

      /* Replace the original string in the line[] buffer with a
      ** modified one using relative path name for each image file.
      */
      uim_part = &mkimage_parms->parts[part_idx];
      strncpy(uim_part->fit_filename, fname, sizeof(uim_part->fit_filename) - 1);
      snprintf(line, sizeof(line), "%s= /incbin/(\"./%s\");\n", tok1, fname);
      part_idx++;
    }

    if (fputs(line, fp_dst) < 0)
    {
      printf("Error: Unable to write line to file %s: \'%s\'", dst, line);
      rc = -1;
      break;
    }

  } /* endwhile */

  dbgprintf("Updated uimage part info:\n");
  for (i = 0; i < IMG_PARTS_MAX; i++)
  {
    uim_part = &mkimage_parms->parts[i];
    if (uim_part->part_size > 0)
    {
      dbgprintf("  [%d] %-15s IMG_SZ = %-10u FIT_FNAME = %s\n", i, uim_part->part_name, uim_part->part_size,
                uim_part->fit_filename);
    }
  }

  /* Clean up.
  */
  if ((fp_src != NULL) &&
      (fclose(fp_src) != 0))
  {
    printf("Error: Cannot close source file %s.\n", src);
    rc = -1;
  }
  if ((fp_dst != NULL) &&
      (fclose(fp_dst) != 0))
  {
    printf("Error: Cannot close destination file %s.\n", dst);
    rc = -1;
  }

  return rc;
}

/*****************************************************************************
** NAME: extract_uim_parts_traditional
** 
** Extract each individual image part from a traditional-formatted u-boot image file.
** 
*****************************************************************************/
int extract_uim_parts_traditional(char *stk_filename, char *new_filename,
                                  mkimage_parms_t *mkimage_parms,
                                  char *opts, size_t opts_size)
{
  unsigned int i;
  unsigned int num_parts;
  unsigned int code_img_part;
  char partlist[FILENAME_LEN_MAX] = { 0 };
  char buf[FILENAME_LEN_MAX] = { 0 };

  num_parts = mkimage_parms->num_parts;
  code_img_part = mkimage_parms->code_img_part;

  /* Extract all uimage parts from original .stk file.
  */
  for (i = 0; i < num_parts; i++)
  {
    snprintf(buf, sizeof(buf), "%s/part%u", p_dir_parts, i);
    if (system_cmd("%s" CMD_EXTIMAGE " -i %s -o %s -n %u ;",
                   tools_path, stk_filename, buf, i) < 0)
    {
      printf("Error: \'%s\' command failed for part %u.\n", i, CMD_EXTIMAGE);
      return -1;
    }
    snprintf(buf, sizeof(buf), "%spart%u", (i > 0) ? ":" : "", i);
    strcat(partlist, buf);

  } /* endfor */

  /* Replace code image partN file with updated fastpath.tgz.
  */
  snprintf(buf, sizeof(buf), "%s/part%u", p_dir_parts, code_img_part);
  if (system_cmd("cp -f %s %s %s ; ",
                 p_file_tgz, buf, p_cmd_output_noerr) < 0)
  {
    printf("Error: Unable to replace %s file with updated %s.\n", buf, p_file_tgz);
    return -1;
  }

  /* Output format-specific mkimage command parameter options string.
  */
  snprintf(opts, opts_size, "-A %s -O linux -T multi -C %s -a %x -e %x -n \"%s\" -d %s",
           mkimage_parms->arch, mkimage_parms->cmpr, mkimage_parms->start,
           mkimage_parms->entry, mkimage_parms->img_name, partlist);

  return 0;
}

/*****************************************************************************
** NAME: extract_uim_parts_fit
** 
** Extract each individual image part from a FIT-formatted u-boot image file.
** 
*****************************************************************************/
int extract_uim_parts_fit(char *stk_filename, char *new_filename,
                          mkimage_parms_t *mkimage_parms,
                          char *opts, size_t opts_size)
{
  unsigned int i;
  unsigned int num_parts;
  unsigned int code_img_part;
  char its_file[FILENAME_LEN_MAX] = { 0 };
  char firmware_file[FILENAME_LEN_MAX] = { 0 };
  char buf[FILENAME_LEN_MAX] = { 0 };
  uim_part_info_t *uim_part;

  if (find_its_filename(p_dir_tgz, its_file, sizeof(its_file)) < 0)
  {
    printf("Error: Cannot locate required .its file in %s directory.\n", p_dir_tgz);
    return -1;
  }

  if (modify_its_file(its_file, p_dir_tgz, p_dir_parts, mkimage_parms) < 0)
  {
    printf("Error: Unable to modify %s file contents.\n", its_file);
    return -1;
  }

  num_parts = mkimage_parms->num_parts;
  code_img_part = mkimage_parms->code_img_part;

  /* Extract all uimage parts from original .stk file
  ** using the part names that were saved in the 
  ** image part info struct. Rename each to its 
  ** fit_filename that was also saved.
  */
  for (i = 0; i < num_parts; i++)
  {
    uim_part = &mkimage_parms->parts[i];
    snprintf(firmware_file, sizeof(firmware_file), "%s", uim_part->part_name);

    /* ./tools/extfitimage g */
    if (system_cmd("cd %s ; ../%s/" CMD_FITIMAGE " %s g %s %s && chmod u+r %s ; cd - %s ;",
                   p_dir_tmp, p_dir_tools, FILE_UIM, firmware_file, CMD_OUTPUT_NONE, firmware_file, CMD_OUTPUT_NONE) < 0)
    {
      printf("Error: TGZ extract \'%s\' command failed.\n", CMD_FITIMAGE);
      return -1;
    }

    snprintf(firmware_file, sizeof(firmware_file), "%s/%s", p_dir_tmp, uim_part->part_name);
    snprintf(buf, sizeof(buf), "%s/%s", p_dir_parts, uim_part->fit_filename);
    if (system_cmd("mv -f %s %s ;", 
                   firmware_file, buf) < 0)
    {
      printf("Error: Unable to move %s/%s file to %s.\n", firmware_file, buf);
      return -1;
    }

  } /* endfor */

  /* Replace code image part file with updated fastpath.tgz.
  */
  uim_part = &mkimage_parms->parts[code_img_part];
  snprintf(buf, sizeof(buf), "%s/%s", p_dir_parts, uim_part->fit_filename);
  if (system_cmd("cp -f %s %s %s ; ",
                 p_file_tgz, buf, p_cmd_output_noerr) < 0)
  {
    printf("Error: Unable to replace %s file with updated %s.\n", buf, p_file_tgz);
    return -1;
  }

  /* Output format-specific mkimage command parameter options string.
  */
  snprintf(opts, opts_size, "-f %s", its_file);

  return 0;
}

/*****************************************************************************
** NAME: create_stk_file
**
** Create the new .stk file from the updated set of fastpath.tgz files.
** 
*****************************************************************************/
int create_stk_file(char *stk_filename, char *new_filename, mkimage_parms_t *mkimage_parms)
{
  int rc = -1;
  char opts[IMG_OPTS_SIZE_MAX] = { 0 };
  char buf[FILENAME_LEN_MAX] = { 0 };
  char uimfile[FILENAME_LEN_MAX] = { 0 };
  struct stat fstat;

  memset(opts, 0, sizeof(opts));

  /* Build new .tgz file.
  */
  (void)unlink(p_file_tgz);
  if ((system_cmd("cd %s ; tar -czf %s/%s ./* %s ; cd - %s ;",
                  p_dir_tgz, env_top, p_file_tgz, p_cmd_output_noerr, CMD_OUTPUT_NONE) < 0) ||
      (stat(p_file_tgz, &fstat) < 0))
  {
    printf("Error: TGZ file creation \'tar\' command failed.\n");
    return -1;
  }
  printf("Created new %s file (%ld Bytes).\n", FILE_TGZ, (long)fstat.st_size);

  if (mkimage_parms->is_fit_format == 0)
  {
    rc = extract_uim_parts_traditional(stk_filename, new_filename, mkimage_parms, opts, sizeof(opts));
  }
  else
  {
    rc = extract_uim_parts_fit(stk_filename, new_filename, mkimage_parms, opts, sizeof(opts));
  }

  if (rc < 0)
  {
    printf("Error: %s file creation failed.\n", stk_filename);
    return -1;
  }

  /* Build new uimage file from all image part files.
  */
  snprintf(uimfile, sizeof(uimfile), "%s.uim", new_filename);
  snprintf(buf, sizeof(buf), "%s/%s", p_dir_tmp, uimfile);
  (void)unlink(buf);
  if (system_cmd("cd %s ; " CMD_MKIMAGE " %s ../%s %s ; cd - %s ;",
                 p_dir_parts, opts, buf, cmd_output_ctrl, CMD_OUTPUT_NONE) < 0)
  {
    printf("Error: Unable to create new %s file.\n", buf);
    return -1;
  }

  /* Create new .stk file.
  ** 
  ** NOTE: The mk_stk utility expects to find a fastpath.vpd file 
  **       in a ./target subdirectory, so copy the VPD file there
  **       first.
  */
  if (system_cmd("cp -f %s %s ;",
                 p_file_vpd, p_dir_target) < 0)
  {
    printf("Error: Unable to copy %s file to %s.\n", p_file_vpd, p_dir_target);
    return -1;
  }
  if (system_cmd("cd %s ; %s" CMD_MKSTK " %s %s ; cd - %s ;",
                 p_dir_tmp, tools_path, uimfile, cmd_output_ctrl, CMD_OUTPUT_NONE) < 0)
  {
    printf("Error: Unable to create new %s file.\n", buf);
    return -1;
  }

  /* Move the new .stk file to the $TOP directory. If it has the same
  ** name as the original .stk file, rename the original to .stk.orig, 
  ** but only do this once (i.e. don't overwrite .stk.orig with a later 
  ** version of the new .stk file if this utility is run multiple times). 
  */
  snprintf(buf, sizeof(buf), "%s.stk", new_filename);
  if (strcmp(stk_filename, buf) == 0)
  {
    snprintf(buf, sizeof(buf), "%s.orig", stk_filename);
    if (access(buf, F_OK) != 0)
    {
      if (rename(stk_filename, buf) < 0)
      {
        printf("Error: Unable to rename %s file to %s.\n", stk_filename, buf);
        return -1;
      }
    }
    snprintf(buf, sizeof(buf), "%s.stk", new_filename);
  }
  if (system_cmd("mv %s/%s %s && chmod 0664 %s ;",
                 p_dir_tmp, buf, buf, buf) < 0)
  {
    printf("Error: Unable to copy %s file to %s.\n", stk_filename, buf);
    return -1;
  }

  printf("\nSuccessfully created %s.\n\n", buf);

  return 0;
}

/************************************************************ 
** Main Application
************************************************************/
int main (int argc, char *argv[])
{
  int ret = -1;
  char *noval = "<none>";
  char cwd[FILENAME_LEN_MAX] = { 0 };
  char agt_path[FILENAME_LEN_MAX] = { 0 };
  char old_agt_name[FILENAME_LEN_MAX] = { 0 };
  char stk_filename[FILENAME_LEN_MAX] = { 0 };
  char new_filename[FILENAME_LEN_MAX] = { 0 };
  stkFileHeader_t stkhdr;
  stkOprFileInfo_t imghdr;
  mkimage_parms_t mkimage_parms;
  img_version_t ver_cmd_parms;
  uint32_t tmp32;
  unsigned long img_offset;
  time_t curtime;

  memset(&stkhdr, 0, sizeof(stkhdr));
  memset(&imghdr, 0, sizeof(imghdr));
  memset(&mkimage_parms, 0, sizeof(mkimage_parms));

  /* init version command parm fields to their "uninit" values */
  memset(ver_cmd_parms.rel_str, 0, sizeof(ver_cmd_parms.rel_str));
  ver_cmd_parms.ver = VER_PART_UNINIT;
  ver_cmd_parms.maint = VER_PART_UNINIT;
  ver_cmd_parms.bld = VER_PART_UNINIT;

  /* The current working directory is assumed, unless the TOP
  ** environment variable is specified.
  */
  env_top = getenv(ENVIRON_TOP);
  if ((env_top == NULL) &&
      (getcwd(cwd, sizeof(cwd)) != NULL))
  {
    env_top = cwd;
  }
  if (env_top == NULL)
  {
    printf("\nPlease set the %s variable in your environment to the top-level working directory.\n" \
           "This is usually the location of the .stk file to be modified.\n" \
           "Example:  export %s=`pwd`\t<-- note the backticks\n", ENVIRON_TOP, ENVIRON_TOP);
    printf("\nYou can also specify %s on the command line:\n" \
           "%s=`pwd` %s\n\n", ENVIRON_TOP, ENVIRON_TOP, argv[0]);
    exit(ret);
  }

  if (parse_cmd_line(argc, argv, agt_path, old_agt_name, &ver_cmd_parms, stk_filename) < 0)
  {
    printf("\nFor help, try: %s -h\n\n", argv[0]);
    exit(ret);
  }
  dbgprintf("Working directory: %s\n", env_top);

  /* DEBUG: Show interpreted input args.
  */
  dbgprintf("\nRead the following args:\n");
  dbgprintf("  tools_path: %s\n", tools_path);
  dbgprintf("  agt_path: %s\n", (strlen(agt_path) == 0) ? noval : agt_path);
  dbgprintf("  old_agt_name: %s\n", (strlen(old_agt_name) == 0) ? noval : old_agt_name);
  dbgprintf("  stk_filename: %s\n", (strlen(stk_filename) == 0) ? noval : stk_filename);
  dbgprintf("  rel: \"%s\"\n", (strlen(ver_cmd_parms.rel_str) == 0) ? noval : ver_cmd_parms.rel_str);
  dbgprintf("  ver: %d\n", ver_cmd_parms.ver);
  dbgprintf("  maint: %d\n", ver_cmd_parms.maint);
  dbgprintf("  bld: %d\n\n", ver_cmd_parms.bld);

  /* One-pass loop for error exit control.
  */
  do
  {
    /* Create working directories for storing temporary files.
    */
    if ((local_mkdir(p_dir_tmp) < 0) ||
        (local_mkdir(p_dir_tgz) < 0) ||
        (local_mkdir(p_dir_parts) < 0) ||
        (local_mkdir(p_dir_target) < 0))
    {
      printf("\nUnable to create additional working subdirectories.\n");
      break;
    }

    if (read_stk_hdrs(stk_filename, &stkhdr, &imghdr) < 0)
    {
      break;
    }

    tmp32 = ntohl(imghdr.offset);
    img_offset = (unsigned long)tmp32;

    /* Get uimage creation attributes from .stk file.
    */
    if (get_uimage_attrs(stk_filename, img_offset, &mkimage_parms) < 0)
    {
      break;
    }

    /* Extract the .tgz file from the .stk file at the previously
    ** determined part index and split it apart.
    */
    if (extract_tgz_files(stk_filename, &mkimage_parms) < 0)
    {
      break;
    }

    /* Update the fastpath.vpd file using any of the -r, -v, -m, -b
    ** input parameters that were specified. Use the new STK filename 
    ** generated by this function when creating the modified .stk file. 
    */
    if (update_vpd(&ver_cmd_parms, &curtime, new_filename, sizeof(new_filename)) < 0)
    {
      break;
    }

    /* Remove old agent files from the TGZ directory (if requested).
    */
    if (remove_old_agent(old_agt_name, p_dir_tgz) < 0)
    {
      break;
    }

    /* Copy the agent files into the TGZ directory.
    */
    if (copy_agent_files(agt_path, sizeof(agt_path), p_dir_tgz) < 0)
    {
      break;
    }

    /* Create the new .stk file.
    */
    if (create_stk_file(stk_filename, new_filename, &mkimage_parms) < 0)
    {
      break;
    }

    /* All went well.
    */
    ret = 0;

  } while (0); 

  /* Common clean up.
  ** 
  ** NOTE: Leave the working directories in place when running with 
  **       the -d flag, since this information complements the debug
  **       print messages and is helpful for problem analysis.
  */
  if (show_dbg == 0)
  {
    (void)local_rmdir(p_dir_tmp);
    (void)local_rmdir(p_dir_tgz);
    (void)local_rmdir(p_dir_parts);
    (void)local_rmdir(p_dir_target);
  }
  else
  {
    printf("(Working subdirectories retained for debugging purposes.)\n\n");
  }

  exit(ret);
}

