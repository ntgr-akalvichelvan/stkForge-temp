/*********************************************************************
*
* Copyright 2016-2018 Broadcom.
*
**********************************************************************
*
* @filename  mk_stk.c
*
* @purpose   This program creates a downloadable stack code image
*            from all OPR and TGZ files specified on the command line.
*
* @component cda
*
* @create    07/22/2003
*
* @author    atsigler 
*
* @end
*
*********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h> /* htonl(), ntohl() */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "stk.h"
/* #include "iplsec.h" */

/* STk file header.
*/
static stkFileHeader_t   stkFileHeader;

/* Information about OPR and TGZ iles in the
** STK file header.
*/
static stkOprFileInfo_t *stkOprFileInfo = 0;
static int           stkOprFileInfoSize = 0;

/* Total number of bytes in the STK file.
*/
static int  stkTotalSize = 0;

static char *stkFileName = 0;

/* Flag indicating that we are processing the first OPR or TGZ
** file.
*/
static int firstFile = 1;


static FILE *stk_fp = 0;


#define POLYNOMIAL 0xA097  /* Use 0x8005 for CRC16, 0x1021 for SDLC/HDLC */

static unsigned short CrcTable[256]; /* CRC lookup table */

#define READ_BUF_SIZE (1024 * 16)
static unsigned char readBuf [READ_BUF_SIZE];

#define VPD_FILE_NAME "fastpath.vpd"

/************************************************************************
** NAME: UpdateCRC
**
** This function updates existing CRC 
**
** INPUT:
**  DataByte - Received data byte
**  
** INPUT/OUTPUT:
**  crc - Current value of CRC.
************************************************************************/
static void stkUpdateCRC (unsigned char DataByte, unsigned short *crc)
{
  register unsigned long new_crc;

  new_crc = *crc;
  new_crc = (new_crc << 8) ^ (CrcTable[new_crc >> 8] ^ DataByte);
  *crc = (unsigned short) new_crc;
}


/************************************************************************
** NAME: GenCrcTable
**
** This function computes CRC look-up table.
************************************************************************/
static void stkGenCrcTable (void)
{
  unsigned short CrcCode;
  unsigned char DataByte;
  unsigned long  index, BitNo;
  unsigned long  CarryBit, DataBit;

  for ( index = 0; index <= 255; index++ )
  {
    CrcCode = 0;
    DataByte = (unsigned char)index;

    for ( BitNo = 1; BitNo <= 8; BitNo++ )
    {
      CarryBit = ((CrcCode & 0x8000) != 0);
      DataBit = ((DataByte & 0x80) != 0);
      CrcCode = (unsigned short)(CrcCode << 1);
      if ( CarryBit ^ DataBit )
      {
        CrcCode = (unsigned short)(CrcCode ^ POLYNOMIAL);
      }
      DataByte = (unsigned char)(DataByte << 1);
    } /* End For */
    CrcTable [index] = CrcCode;
  } /* End for */
}


/************************************************************
** Compute CRC of the file.
**
**
************************************************************/
static void stkCrcCompute (unsigned char * stk_file_name)
{
  int byte_count;
  int i;
  unsigned short computed_crc = 0;
  FILE * stk_file;
  unsigned char crc_net[2];
  int rc;

  stkGenCrcTable (); 

  printf("Computing CRC on the STK file: %s\n", stk_file_name);

  stk_file = fopen (stk_file_name, "rb+");
  if (stk_file == NULL)
  {
    printf("Can't open STK file: %s\n", stk_file_name);
    exit (-1);
  }

  /* Skip the first two bytes of the file. 
  */
  rc = fseek (stk_file, 2, SEEK_SET);
  if (rc < 0)
  {
    printf("Can't set position of STK file to 2.\n");
    exit (-1);
  }


  do
  {
    byte_count = fread (readBuf, 1, READ_BUF_SIZE, stk_file);
    if (byte_count > 0)
    {
      for (i = 0; i < byte_count; i++)
      {
        stkUpdateCRC (readBuf[i], &computed_crc);
      }
    }
  } while (byte_count > 0);


  *(unsigned short *) &crc_net[0] = ntohs (computed_crc);

  /* Reset file pointer to the begining of file.
  */
  rc = fseek (stk_file, 0, SEEK_SET);
  if (rc < 0)
  {
    printf("Can't set position of STK file to 0.\n");
    exit (-1);
  }

  /* Write the CRC into the file.
  */
  rc = fwrite (crc_net, sizeof (crc_net), 1, stk_file);
  if (rc != 1)
  {
    printf("Can't write CRC to STK file.\n");
    exit (-1);
  }

  rc = fclose (stk_file);
  if (rc < 0)
  {
    printf("Can't close STK file.\n");
    exit (-1);
  }

}



/************************************************************
** Show contents of the STK file header.
**
** This function opens the STk file, parses the header
** and displays information about the STk file.
**
************************************************************/
static void stkFileShow (char * stk_file_name)
{
  FILE * stk_file;
  stkFileHeader_t   stk_file_header;
  stkOprFileInfo_t  stk_opr_file_info;
  int  i;
  int  num_components;
  int rc;

  printf("Opening STK file: %s\n", stk_file_name);

  stk_file = fopen (stk_file_name, "rb");
  if (stk_file == NULL)
  {
    printf("Can't open STK file: %s\n", stk_file_name);
    exit (-1);
  }

  rc = fread (&stk_file_header, sizeof (stk_file_header), 1, stk_file);
  if (rc != 1)
  {
    printf("Can't read STK file header.\n");
    exit (-1);
  }

  /* 
  * NOTE: The ntohl() and ntohs() functions may actually be macros that are
  *       #defined away as nothing.  Using an explicit typecast for each
  *       of these so that the value is always of the expected type to
  *       match the format string.
  */

  printf("CRC: 0x%x\n",
         (unsigned short)ntohs(stk_file_header.crc));
  printf("Tag1: 0x%x\n",
         (unsigned short)ntohs(stk_file_header.tag1));
  printf("Tag2: 0x%x\n",
         (unsigned int)ntohl(stk_file_header.tag2));

  num_components = ntohl (stk_file_header.num_components);
  printf("Num Components: %u\n",
         (unsigned int)ntohl(stk_file_header.num_components));

  printf("File Size: %u\n",
         (unsigned int)ntohl (stk_file_header.file_size));
  printf("Rel: %d, Ver: %d, Maint Level: %d, Build Num: %d\n",
         stk_file_header.rel,
         stk_file_header.ver,
         stk_file_header.maint_level,
         stk_file_header.build_num);
  printf("STK Header Size: %u\n",
         (unsigned int)ntohl(stk_file_header.stk_header_size));


  for (i = 0; i < num_components; i++)
  {
    printf("\nComponent %d:\n", i+1);
    rc = fread (&stk_opr_file_info, sizeof (stk_opr_file_info), 1, stk_file);
    if (rc != 1)
    {
      printf("Can't read file info.\n");
      exit (-1);
    }

    printf("Offset: %u\n",
           (unsigned int)ntohl (stk_opr_file_info.offset));
    printf("Target Device: 0x%x\n",
           (unsigned int)ntohl (stk_opr_file_info.target_device));

    switch (ntohl(stk_opr_file_info.os))
    {
    case STK_OS_VXWORKS:
      printf("OS: VxWorks\n");
      break;
    case STK_OS_LINUX:
      printf("OS: Linux\n");
      break;
    case STK_OS_ECOS:
      printf("OS: eCos\n");
      break;
    default:
      printf("OS: %u : Unknown.\n",
             (unsigned int)ntohl(stk_opr_file_info.os));
    }

    printf("STK Image Flags: 0x%08x\n",
           (unsigned int)ntohl(stk_opr_file_info.image_flags));
  }

  rc = fclose (stk_file);
  if (rc < 0)
  {
    printf("Can't close STK file: %s.\n", stk_file_name);
    exit (-1);
  }
}

/************************************************************
** Append OPR or TGZ file to the STk file.
**
**
************************************************************/
static void stkFileAppend (char *opr_file_name)
{
  FILE * opr_file;
  int rc;
  char buf[1];

  opr_file = fopen (opr_file_name, "rb");
  if (opr_file == NULL)
  {
    printf("Can't open %s file.\n", opr_file_name);
    exit (-1);
  }

  do
  {
    rc = fread (buf, 1, 1, opr_file);

    if (rc == 0)
    {
      break; /* Reached end of file. Exit loop. */
    }

    if (rc != 1)
    {
      printf("Can't read data from %s\n", opr_file_name);
      exit (-1);
    }
    else
    {
      /* Append byte to the STK file.
      */
      rc = fwrite (buf, 1, 1, stk_fp);
      if (rc != 1)
      {
        printf("Can't write to the STK file.\n");
        exit (-1);
      }
    }

  } while (rc == 1);

  rc = fclose (opr_file);
  if (rc < 0)
  {
    printf("Can't close %s file.\n", opr_file_name);
    exit (-1);
  }
}


/************************************************************
** Write STK file header to disk.
**
************************************************************/
static void stkFileHeaderWrite (void)
{
  int rc;


  stkFileHeader.file_size = htonl (stkTotalSize);

  /* Create a new STK file.
  */
  stk_fp = fopen (stkFileName, "wb"); 
  if (stk_fp == NULL)
  {
    printf("Error, can't open STK file: %s\n", stkFileName);
    exit (-1);
  }


  /* Write the STK header into the file.
  */
  rc = fwrite (&stkFileHeader, sizeof (stkFileHeader), 1, stk_fp);
  if (rc != 1)
  {
    printf("Can't create STK file header.\n");
    exit (-1);
  }


  /* Write file descriptors.
  */
  rc = fwrite (stkOprFileInfo, stkOprFileInfoSize, 1, stk_fp);
  if (rc != 1)
  {
    printf("Can't write file descriptors to the STK file.\n");
    exit (-1);
  }


}

/************************************************************
** Read code version and target device from the
** Linux TGZ file.
**
************************************************************/
static void stkLinuxFileInfoGet (char *uimg_file_name,
                                 unsigned int *rel, 
                                 unsigned int *ver, 
                                 unsigned int *maint_level, 
                                 unsigned int *build_num,
                                 unsigned long *target_device,
                                 unsigned long *image_flags)
{
  int rc;
  unsigned char line[255];
  FILE *fp = NULL;
  unsigned char line_start[80];
  int found_version = 0, found_device = 0;
  int comma_len;
  char rel_str[5] = {0};
  unsigned int unitID;
  unsigned int imageFlags;

  fp = fopen ("../target/fastpath.vpd", "r"); 

  if (fp == NULL)
  {
    printf("Couldn't find %s file.\n", VPD_FILE_NAME);
    exit (-1);
  }

  /* Read the linux vpd file for version information
  */
  while (1)
  {
    if (fgets(line, sizeof(line) - 1, fp ) == NULL)
    {
      /* End of file reached
      */
      break;
    }
    else
    {
      printf("line - %s", line);
      sscanf(line, "%s", (char*)&line_start);

      /* Look for version information
      */
      if (!strncmp((char*)line_start,"Rel", strlen("Rel")))
      {
        rc = sscanf(line, "%s %s Ver %d, Maint Lev %d, Bld No %d",
                    (char*)&line_start,
                    rel_str,
                    ver,
                    maint_level,
                    build_num);

        if (rc == 5)
        {
          found_version++;
        }
        /* Found the required version information
        */
        comma_len = strlen(rel_str);
        rel_str[comma_len-1] = 0;

        if (isalpha(rel_str[0]) != 0)
        {
          *rel = rel_str[0];
        }
        else
        {
          *rel = atoi(rel_str);
        }
      }
      /* Look for target device information
      */
      else if (!strncmp((char*)line_start,"Device", strlen("Device")))
      {
        sscanf(line, "%s %d", (char*)&line_start, &unitID);

        *target_device = ntohl(unitID);
        found_device++;
      }
      /* Look for ImageFlags information
      */
      else if (!strncmp((char*)line_start,"ImageFlags", strlen("ImageFlags")))
      {
        sscanf(line, "%s %d", (char*)&line_start, &imageFlags);

        *image_flags = ntohl(imageFlags);
      }
    }
  }
  fclose(fp);

  /* Make sure the version and target device was found
  */
  if ((!found_version) || (!found_device))
  {
    printf("Couldn't find required information in %s\n", VPD_FILE_NAME);

    exit (-1);
  }

}

/************************************************************
** Read code version and target device from the
** VxWorks OPR file.
**
************************************************************/
static void stkVxWorksFileInfoGet (FILE * opr_fp,
                                   unsigned char *rel, 
                                   unsigned char *ver, 
                                   unsigned char *maint_level, 
                                   unsigned char *build_num,
                                   unsigned long *target_device)
{
  int rc;

  /* Place file pointer to the target device tag in the OPR header.
  */
  rc = fseek (opr_fp, 4, SEEK_SET);
  if (rc != 0)
  {
    printf("Error searching for target device field.\n");
    exit (-1);
  }


  /* Read the target device from file.
  */
  rc = fread (target_device, 4, 1, opr_fp);
  if (rc != 1)
  {
    printf("Error reading target device field.\n");
    exit (-1);
  }

  /* Place file pointer to the version information in the OPR header.
  */
  rc = fseek (opr_fp, 784, SEEK_SET);
  if (rc != 0)
  {
    printf("Error searching for version field.\n");
    exit (-1);
  }

  /* Read the version from file.
  */
  rc = fread (rel, 1, 1, opr_fp);
  if (rc != 1)
  {
    printf("Error reading release field.\n");
    exit (-1);
  }
  rc = fread (ver, 1, 1, opr_fp);
  if (rc != 1)
  {
    printf("Error reading version field.\n");
    exit (-1);
  }
  rc = fread (maint_level, 1, 1, opr_fp);
  if (rc != 1)
  {
    printf("Error reading maintenance level field.\n");
    exit (-1);
  }
  rc = fread (build_num, 1, 1, opr_fp);
  if (rc != 1)
  {
    printf("Error reading build number field.\n");
    exit (-1);
  }

}


/************************************************************
** Add information from the OPR or TGZ file to the STK file 
** header.
**
** We assume that the OPR file has a well defined format 
** that is the same on all platforms. 
**
************************************************************/
static void stkFileHeaderCreate (char * file_name, int file_index, 
                                 int  os, int extn_len)
{
  FILE * opr_fp;
  int rc;
  unsigned long target_device;
  unsigned long image_flags = 0;
  unsigned char rel, ver, maint_level, build_num;
  unsigned int tgz_rel, tgz_ver, tgz_maint_level, tgz_build_num;
  int    stk_file_name_size;
  int    file_size;
  struct stat stat_buf;

  /* 
  * NOTE: The ntohl() and ntohs() functions may actually be macros that are
  *       #defined away as nothing.  Using an explicit typecast for each
  *       of these so that the value is always of the expected type to
  *       match the format string.
  */

  switch (os)
  {
  case STK_OS_VXWORKS:

    opr_fp = fopen (file_name, "rb"); 

    if (opr_fp == NULL)
    {
      printf("Couldn't open %s\n", file_name);
      exit (-1);
    }

    stkVxWorksFileInfoGet (opr_fp,
                           &rel, 
                           &ver, 
                           &maint_level, 
                           &build_num,
                           &target_device);

    /* Determine the size of the OPR file.
    */
    rc = fseek (opr_fp, 0, SEEK_END);
    if (rc != 0)
    {
      printf("Error seeking end of file.\n");
      exit (-1);
    }

    file_size = ftell (opr_fp);
    if (file_size < 0)
    {
      printf("Error getting end of file position.\n");
      exit (-1);
    }

    rc = fclose (opr_fp);
    if (rc != 0)
    {
      printf("Error closing file.\n");
      exit (-1);
    }

    break;
  case STK_OS_LINUX:
  case STK_OS_ECOS:

    /* Make sure the TGZ file exists
    */
    rc = stat(file_name, &stat_buf);

    if ((rc != 0) || (stat_buf.st_size == 0))
    {
      printf("Error opening TGZ file: %s\n", file_name);
      exit (-1);
    }
    file_size = stat_buf.st_size;

    stkLinuxFileInfoGet (file_name,
                         &tgz_rel, 
                         &tgz_ver, 
                         &tgz_maint_level, 
                         &tgz_build_num,
                         &target_device,
                         &image_flags);
    break;
  default:
    printf("Unsupported OS\n");
    exit (-1);

  }


  printf("OS: ");
  switch (os)
  {
  case STK_OS_VXWORKS:
    printf("VxWorks\n");
    printf("Target Device: 0x%x\n", (unsigned int)ntohl(target_device));
    printf("Rel: %d, Ver: %d, Maint Level: %d, Build Num: %d\n",
           rel, 
           ver,
           maint_level,
           build_num);
    printf("File Size: %d\n", file_size);
    break;
  case STK_OS_LINUX:
    printf("Linux\n");
    printf("Target Device: %#x\n", (unsigned int)ntohl(target_device));
    printf("Rel: %d, Ver: %d, Maint Level: %d, Build Num: %d\n",
           tgz_rel,
           tgz_ver,
           tgz_maint_level,
           tgz_build_num);
    printf("File Size: %d\n", file_size);

    rel = (unsigned char)tgz_rel;
    ver = (unsigned char)tgz_ver;
    maint_level = (unsigned char)tgz_maint_level;
    build_num = (unsigned char)tgz_build_num;

    break;
  case STK_OS_ECOS:
    printf("eCos\n");
    printf("Target Device: %#x\n", (unsigned int)ntohl(target_device));
    printf("Rel: %d, Ver: %d, Maint Level: %d, Build Num: %d\n", 
           tgz_rel, 
           tgz_ver, 
           tgz_maint_level, 
           tgz_build_num);
    printf("File Size: %d\n", file_size);

    rel = (unsigned char)tgz_rel;
    ver = (unsigned char)tgz_ver;
    maint_level = (unsigned char)tgz_maint_level;
    build_num = (unsigned char)tgz_build_num;

    break;
  default:
    printf("Unsupported OS\n");
    exit (-1);
  }


  /* If we have not created an STK file then initialize the STK file 
  ** header and generate the STK file name.
  */
  if (firstFile == 1)
  {
    firstFile = 0;

    /* The file name for STK file is the same as the first 
    ** file name on the command line except the file has an
    ** stk extension.
    */
    stk_file_name_size = strlen (file_name) + 1;
    stkFileName = malloc (stk_file_name_size);
    memset (stkFileName, 0, stk_file_name_size);
    memcpy (stkFileName, file_name, stk_file_name_size - extn_len - 1);
    strcpy (&stkFileName[stk_file_name_size - extn_len - 1], ".stk");


    stkFileHeader.rel = rel;
    stkFileHeader.ver = ver;
    stkFileHeader.maint_level = maint_level;
    stkFileHeader.build_num = build_num;

  }

  /* All OPR and TGZ files included in the same STK file must have
  ** the same code version. The first OPR file sets the version 
  ** number for the rest of the STK members. 
  */
  if ((stkFileHeader.rel != rel) ||
      (stkFileHeader.ver != ver) ||
      (stkFileHeader.maint_level != maint_level) ||
      (stkFileHeader.build_num != build_num))
  {
    printf("Detected version mismatch in file: %s\n", file_name);
    printf("Expected version: %d.%d.%d.%d\n",
           stkFileHeader.rel,
           stkFileHeader.ver,
           stkFileHeader.maint_level,
           stkFileHeader.build_num);
    printf("Detected version: %d.%d.%d.%d\n",
           rel,
           ver,
           maint_level,
           build_num);
    exit (-1);
  }

  /* Create file descriptor for this OPR/TGZ file in the STK file header.
  */
  stkOprFileInfo[file_index].offset = htonl(stkTotalSize);
  stkOprFileInfo[file_index].os = htonl(os);
  stkOprFileInfo[file_index].target_device = target_device;
  stkOprFileInfo[file_index].image_flags   = image_flags;

  /* Increment total STK file size by the number of bytes in the OPR or TGZ file.
  */
  stkTotalSize += file_size;

  printf("\n");
}


/************************************************************
************************************************************/
int main (int argc, char *argv[])
{
  int i;
  int file_index;
  int file_name_len;
  char * opr_file_name;
  int rc;

  if (argc < 2)
  {
    printf("You must specify at least one OPR or TGZ file on the command line.\n");
    exit (-1);
  }

  if (argc > (STK_MAX_IMAGES+1))
  {
    printf("A maximum of %d images are allowed in one .stk file.\n", STK_MAX_IMAGES);
    exit(-1);
  }

  memset (&stkFileHeader, 0, sizeof (stkFileHeader));
  stkFileHeader.tag1 = htons (STK_TAG1);
  stkFileHeader.tag2 = htonl (STK_TAG2);
  stkFileHeader.num_components = htonl (argc - 1);


  /* Allocate space for the OPR file descriptors.
  */
  stkOprFileInfoSize = sizeof (stkOprFileInfo_t) * (argc - 1);
  stkOprFileInfo = malloc (stkOprFileInfoSize);
  memset (stkOprFileInfo, 0, stkOprFileInfoSize);

  /* The initial size of the STK file is equal to the
  ** STK file header size.
  */
  stkTotalSize  = sizeof (stkFileHeader) + stkOprFileInfoSize;

  stkFileHeader.stk_header_size = htonl (stkTotalSize);


  /* Examine the file header for all OPR files. Build the STK file header.
  */ 
  file_index = 0;
  for (i = 1; i < argc; i++)
  {
    opr_file_name = argv[i];
    printf ("Checking file: %s\n", opr_file_name);

    /* Determine whether the file is VxWorks or Linux. We can use the 
    ** file extention (opr vs. tgz) to make this determination.
    */
    file_name_len = strlen (opr_file_name);
    if (memcmp (".opr", &opr_file_name[file_name_len - 4], 4) == 0)
    {
      stkFileHeaderCreate (opr_file_name, file_index, STK_OS_VXWORKS, 4);
      file_index++;
    }
    else if ((memcmp (".7z", &opr_file_name[file_name_len - 3], 3) == 0))
    {
      stkFileHeaderCreate (opr_file_name, file_index, STK_OS_ECOS, 3);
      file_index++;
    }
    else if ((memcmp (".tar.gz", &opr_file_name[file_name_len - 7], 7) == 0))
    {
      stkFileHeaderCreate (opr_file_name, file_index, STK_OS_LINUX, 7);
      file_index++;
    }
    else if ((memcmp (".gz", &opr_file_name[file_name_len - 3], 3) == 0))
    {
      stkFileHeaderCreate (opr_file_name, file_index, STK_OS_LINUX, 3);
      file_index++;
    }
    else
    {
      if ((memcmp (".uim", &opr_file_name[file_name_len - 4], 4) == 0) ||
          (memcmp (".tgz", &opr_file_name[file_name_len - 4], 4) == 0))
      {
        stkFileHeaderCreate (opr_file_name, file_index, STK_OS_LINUX, 4);
        file_index++;
      }
    }
  } /* for */

  /* Write the STK file header to disk.
  */
  stkFileHeaderWrite ();

  /* Combine all OPR and TGZ files into one STK file.
  */
  for (i = 1; i < argc; i++)
  {
    opr_file_name = argv[i];
    stkFileAppend (opr_file_name);
  }

  /* Set file pointer to beginning of file plus 2 bytes.
  */
  rc = fseek (stk_fp, 2, SEEK_SET);
  if (rc < 0)
  {
    printf("Can't set position of STK file to 2.\n");
    exit (-1);
  }


  rc = fclose (stk_fp);
  if (rc < 0)
  {
    printf("Can't close STK file.\n");
    exit (-1);
  }

  free (stkOprFileInfo);

  /* Compute CRC of the STK file.
  */
  stkCrcCompute (stkFileName);

  /* Print the content of the STK file header.
  */
  stkFileShow (stkFileName);
}
