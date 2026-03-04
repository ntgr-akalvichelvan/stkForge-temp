/*
 * Copyright 2016-2018 Broadcom
 *
 * This program is the proprietary software of Broadcom and/or
 * its licensors, and may only be used, duplicated, modified or distributed
 * pursuant to the terms and conditions of a separate, written license
 * agreement executed between you and Broadcom (an "Authorized License").
 * Except as set forth in an Authorized License, Broadcom grants no license
 * (express or implied), right to use, or waiver of any kind with respect to
 * the Software, and Broadcom expressly reserves all rights in and to the
 * Software and all intellectual property rights therein.  IF YOU HAVE NO
 * AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE IN ANY
 * WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE ALL USE OF
 * THE SOFTWARE.
 *
 * Except as expressly set forth in the Authorized License,
 *
 * 1. This program, including its structure, sequence and organization,
 * constitutes the valuable trade secrets of Broadcom, and you shall use
 * all reasonable efforts to protect the confidentiality thereof, and to
 * use this information only in connection with your use of Broadcom
 * integrated circuit products.
 *
 * 2. TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED
 * "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES,
 * REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR
 * OTHERWISE, WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
 * DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY,
 * NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES,
 * ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE RISK ARISING
 * OUT OF USE OR PERFORMANCE OF THE SOFTWARE.
 *
 * 3. TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL
 * BROADCOM OR ITS LICENSORS BE LIABLE FOR (i) CONSEQUENTIAL, INCIDENTAL,
 * SPECIAL, INDIRECT, OR EXEMPLARY DAMAGES WHATSOEVER ARISING OUT OF OR
 * IN ANY WAY RELATING TO YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN
 * IF BROADCOM HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR (ii)
 * ANY AMOUNT IN EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE ITSELF
 * OR U.S. $1, WHICHEVER IS GREATER. THESE LIMITATIONS SHALL APPLY
 * NOTWITHSTANDING ANY FAILURE OF ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *
 * @filename    extimage.c
 *
 * @purpose
 *
 * @component
 *
 * @comments    none
 *
 * @create
 *
 * @author
 *
 * @end
 *
 **************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#include "image.h"
#include "stk.h"
#include <netinet/in.h> /* htonl(), ntohl() -- must be positioned after image.h */
#ifndef PAGE_SIZE
#define PAGE_SHIFT 12
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#endif


/* --------------------------------------------------------------------- */

#define MAX_IMAGES 24
int extract=0;
int display=0;

stkFileHeader_t stkHdr;
stkOprFileInfo_t oprHdr;

image_header_t imageHdr;
unsigned int imageSizes[MAX_IMAGES];
unsigned int firmwareSize = 0;
int imageCount = 0;


int open_input(char * file) {
  int fd;
  int cnt = 0;
  int read_rc;
  unsigned int size;

  fd=open(file,O_RDONLY);
  if(fd==-1) {
    printf("error: cannot open input file %s\n",file);
    exit(2);
  };

  do
  {
    /* Read the image header */
    read_rc = read(fd, &stkHdr, sizeof(stkFileHeader_t));
  } while ((0 > read_rc) && (EINTR == errno));

  if (read_rc < 0) {
    close(fd);
    perror("Error on read");
    exit(1);
  }

  if ((ntohs(stkHdr.tag1) == STK_TAG1) &&
     (ntohl(stkHdr.tag2) == STK_TAG2))
  {
    do
    {
      read_rc = read(fd, &oprHdr, sizeof(stkOprFileInfo_t));
    } while ((0 > read_rc) && (EINTR == errno));

    if (read_rc < 0) {
      close(fd);
      perror("Error on read");
      exit(1);
    }
  } else {
    printf("warning: invalid STK header, assuming uimage\n");
    lseek(fd, 0, 0);
  }

  firmwareSize = (unsigned int)ntohl(stkHdr.file_size);

  do
  {
    /* Read the image header */
    read_rc = read(fd, &imageHdr, sizeof(image_header_t));
  } while ((0 > read_rc) && (EINTR == errno));

  if (read_rc < 0) {
    close(fd);
    perror("Error on read");
    exit(1);
  }

  if (ntohl(imageHdr.ih_magic) != IH_MAGIC) {
    close(fd);
    printf("error: invalid uimage header\n");
    exit(2);
  }

  do
  {
    read_rc = read(fd, &size, sizeof(unsigned int));
  } while ((0 > read_rc) && (EINTR == errno));

  if (read_rc < 0) {
    close(fd);
    perror("Error reading size from header ");
    exit(1);
  }

  while ((size != 0) && (cnt < MAX_IMAGES)) {
    /* Ensure image sizes are even */
    imageSizes[cnt] = ntohl(size);
    cnt++;

    do
    {
      read_rc = read(fd, &size, sizeof(unsigned int));
    } while ((0 > read_rc) && (EINTR == errno));

    if (read_rc < 0) {
      close(fd);
      perror("Error on read");
      exit(1);
    }
  }
  imageCount = cnt;
  if (cnt == MAX_IMAGES) {
    printf("warning: maximum number of supported images reached\n"); 
  }

  return(fd);
}

int open_output(char * file) {
    int fd;

  if (strcmp(file, "-") == 0) {
    return 1; /* stdout */
  }

  fd=open(file,O_CREAT | O_RDWR, S_IRWXU);
  if(fd==-1) {
    printf("error: cannot open output file %s\n",file);
    exit(2);
  };
  return(fd);
}

#define READ_BUFFER_SIZE 4096
int copy_image(inFd, outFd, inSize)
{
   int offset = 0;
   char buffer[READ_BUFFER_SIZE];
   int bytesToRead, readSize = READ_BUFFER_SIZE;
   int cur_pos;
   int read_rc, write_rc;

   bytesToRead = inSize ? inSize : firmwareSize; 
   offset = 0;
   cur_pos = lseek(inFd, offset, SEEK_SET);

   cur_pos++;
   cur_pos--;

   /* Extract image */
   do {
     if (readSize > bytesToRead) {
       readSize = bytesToRead;
     }

     do
     {
       read_rc = read(inFd, buffer, readSize);
     } while ((0 > read_rc) && (EINTR == errno));

     if (read_rc < 0) {
       perror("Error on read");
       exit(1);
     }
     if (read_rc > 0) {
       int writeBytes;
       writeBytes = read_rc;
       do {
            do 
            {
              write_rc = write(outFd, buffer, writeBytes);
            } while((0 > write_rc) && (EINTR == errno)); /*Interuppted call. Retrying. */

            if (write_rc < 0) {
              perror("Error on write");
              exit(1);
         }
         writeBytes -= write_rc;
       } while(write_rc >= 0 && writeBytes > 0);
     }
     bytesToRead -= read_rc;
   } while((bytesToRead > 0) && (read_rc >= 0));
   close(inFd);
   close(outFd);
   return(0);
}

int extract_image(reqImageNum, inFd, outFd, inSize)
{
   int offset = 0;
   int count;
   int bytesToRead;
   int cur_pos;
   void *src_addr, *copy_src;
   int pages, write_rc;
   struct stat st;

   /* Compute image start address */
   for (count = 0; count < reqImageNum; count++) {
      /* Compensate for observed bug in mkimage with odd sized file */
      /* The output file is padded with a byte thus aligning it on  */
      /* an even boundary, but the size does not reflect the the    */
      /* padded byte.                                               */
      offset+= (imageSizes[count] + 3) & 0xfffffffc;
   }

   cur_pos = lseek(inFd, offset, 1);
   bytesToRead =  imageSizes[reqImageNum];
   if (inSize == 0) {
     if (fstat(inFd, &st) < 0) {
       perror("Can't figure out how big the file is");
       exit(1);
     }
     inSize = st.st_size;
   }

   if ((offset > inSize) || ((cur_pos + bytesToRead) > inSize)) {
     fprintf(stderr, "Corrupt data: Image claims to extend past end of file\n");
     exit(1);
   }

   /* Extract image */
   pages = ((bytesToRead + cur_pos) / PAGE_SIZE) + 1;
   src_addr = mmap(NULL, pages*PAGE_SIZE, PROT_READ, MAP_SHARED, inFd, 0);
   if (src_addr == MAP_FAILED) {
     perror("Could not map memory for copy");
     exit(1);
   }
   copy_src = (void *)(((char *)src_addr) + cur_pos);
   
   do 
   {
     do
     {
       write_rc = write(outFd, copy_src, bytesToRead);
     } while ((0 > write_rc) && (EINTR == errno));

     if (write_rc < 0) 
     {
       perror("Error on write");
       exit(1);
     }

     bytesToRead -= write_rc;
     copy_src = (void *)(((char *)copy_src) + write_rc);     
   } while((bytesToRead > 0) && (write_rc >= 0));
   munmap(src_addr, pages*PAGE_SIZE);
   return(0);
}



/*
 * print sizes as "xxx kB", "xxx.y kB", "xxx MB" or "xxx.y MB" as needed;
 * allow for optional trailing string (like "\n")
 */
void print_size (ulong size, const char *s)
{
   ulong m, n;
   ulong d = 1 << 20;      /* 1 MB */
   char  c = 'M';

   if (size < d) {         /* print in kB */
      c = 'k';
      d = 1 << 10;
   }

   n = size / d;

   m = (10 * (size - (n * d)) + (d / 2) ) / d;

   if (m >= 10) {
      m -= 10;
      n += 1;
   }

   printf ("%2ld", n);
   if (m) {
      printf (".%ld", m);
   }
   printf (" %cB%s", c, s);
}

static void
print_type (image_header_t *hdr)
{
   char *os, *arch, *type, *comp;

   switch (hdr->ih_os) {
   case IH_OS_INVALID:  os = "Invalid OS";      break;
   case IH_OS_NETBSD:   os = "NetBSD";       break;
   case IH_OS_LINUX: os = "Linux";        break;
   case IH_OS_VXWORKS:  os = "VxWorks";         break;
   case IH_OS_QNX:      os = "QNX";       break;
   case IH_OS_U_BOOT:   os = "U-Boot";       break;
   case IH_OS_RTEMS: os = "RTEMS";        break;
#ifdef CONFIG_ARTOS
   case IH_OS_ARTOS: os = "ARTOS";        break;
#endif
#ifdef CONFIG_LYNXKDI
   case IH_OS_LYNXOS:   os = "LynxOS";       break;
#endif
   default:    os = "Unknown OS";      break;
   }

   switch (hdr->ih_arch) {
   case IH_CPU_INVALID: arch = "Invalid CPU";      break;
   case IH_CPU_ALPHA:   arch = "Alpha";         break;
   case IH_CPU_ARM:  arch = "ARM";        break;
   case IH_CPU_I386: arch = "Intel x86";     break;
   case IH_CPU_IA64: arch = "IA64";       break;
   case IH_CPU_MIPS: arch = "MIPS";       break;
   case IH_CPU_MIPS64:  arch = "MIPS 64 Bit";      break;
   case IH_CPU_PPC:  arch = "PowerPC";    break;
   case IH_CPU_S390: arch = "IBM S390";      break;
   case IH_CPU_SH:      arch = "SuperH";     break;
   case IH_CPU_SPARC:   arch = "SPARC";         break;
   case IH_CPU_SPARC64: arch = "SPARC 64 Bit";     break;
   case IH_CPU_M68K: arch = "M68K";          break;
   case IH_CPU_MICROBLAZE: arch = "Microblaze";       break;
   default:    arch = "Unknown Architecture";   break;
   }

   switch (hdr->ih_type) {
   case IH_TYPE_INVALID:   type = "Invalid Image";    break;
   case IH_TYPE_STANDALONE:type = "Standalone Program";  break;
   case IH_TYPE_KERNEL: type = "Kernel Image";     break;
   case IH_TYPE_RAMDISK:   type = "RAMDisk Image";    break;
   case IH_TYPE_MULTI:  type = "Multi-File Image"; break;
   case IH_TYPE_FIRMWARE:  type = "Firmware";      break;
   case IH_TYPE_SCRIPT: type = "Script";     break;
   default:    type = "Unknown Image";    break;
   }

   switch (hdr->ih_comp) {
   case IH_COMP_NONE:   comp = "uncompressed";     break;
   case IH_COMP_GZIP:   comp = "gzip compressed";  break;
   case IH_COMP_BZIP2:  comp = "bzip2 compressed"; break;
   default:    comp = "unknown compression"; break;
   }

   printf ("%s %s %s (%s)", arch, os, type, comp);
}

void
print_image_hdr (image_header_t *hdr)
{
#if 0
   time_t timestamp = (time_t)ntohl(hdr->ih_time);
   struct rtc_time tm;
#endif

   printf ("\n   Image Name:   %.*s\n", IH_NMLEN, hdr->ih_name);
#if 0
   to_tm (timestamp, &tm);
   printf ("   Created:      %4d-%02d-%02d  %2d:%02d:%02d UTC\n",
      tm.tm_year, tm.tm_mon, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif
   printf ("   Image Type:   "); print_type(hdr);
   printf ("\n   Data Size:    %u Bytes = ", (unsigned int)ntohl(hdr->ih_size));
   print_size (ntohl(hdr->ih_size), "\n");
   printf ("   Load Address: %08x\n"
      "   Entry Point:  %08x\n",
       (unsigned int)ntohl(hdr->ih_load), (unsigned int)ntohl(hdr->ih_ep));

   if (hdr->ih_type == IH_TYPE_MULTI) {
      int i;
      printf ("   Contents:\n");
    for (i = 0; i < imageCount; i++) {
         printf ("     Image %d: %8u Bytes = ", i, imageSizes[i]);
         print_size (imageSizes[i], "\n");
      }
   }
   printf ("\n");
}


/* --------------------------------------------------------------------- */

void usage(void) {
  printf("\nusage: extimage [<options>]*\n\n"
    "Where <options> can be:\n\n"
    "  -h                 display this text\n"
    "  -i <name>          input file name\n"
    "  -o <name>          output file name ('-' or omit for stdout)\n"
    "  -n <number>        image to extract\n"
    "  -d                 display contains of input file\n"
    "  -s                 size of input file\n"
   "  -a <name>          get the start address from the given file\n"
   "  -e <name>          get the execute address from the given file\n"
   "  -c <name>          extract full image from the given file\n\n");
  exit(0);
};

int main(int argc, char* argv[]) {
  
  int arg=1;
  int inFd = 0;
  int outFd = 0;
  int reqImageNum = 0;
  unsigned int saddr = 0;
  unsigned int eaddr = 0;
  unsigned int inSize = 0;

  if(argc==1) {
    usage();
    exit(0);
  };

  if (strcmp("-a",argv[arg]) == 0)
  {
    arg++;  
    inFd = open_input(argv[arg]);
    saddr = 1;
  }
  else if (strcmp("-e",argv[arg]) == 0)
  {
    arg++;  
    inFd = open_input(argv[arg]);
    eaddr = 1; 
  }
  else 
  { 
    while(arg<argc) 
    {
      if (strcmp("-h",argv[arg])==0) 
      {
        usage();
      }
       
      if (strcmp("-i",argv[arg])==0) 
      {
        arg++;
        inFd = open_input(argv[arg]);
      }
       
      if (strcmp("-o",argv[arg])==0) 
      {
        arg++;
        outFd = open_output(argv[arg]);
      }

      if(strcmp("-n",argv[arg])==0)
      {
        arg++;
        reqImageNum = atoi(argv[arg]);
        extract = 1;
      }
      
      if (strcmp("-s",argv[arg])==0)
      {
        arg++;
        inSize = (unsigned int)strtol(argv[arg], (char **)NULL, 16);
      }

      if(strcmp("-d",argv[arg])==0) 
      {
        display = 1;
      }

      if(strcmp("-c",argv[arg])==0) 
      {
        extract = 1;
        reqImageNum = -1;
      }
      arg++;
    }
  }

  if (!outFd) {
    outFd = 1; /* stdout */
  }
  if(extract) {
    if (inFd && outFd) {
       if (reqImageNum == -1) {
         copy_image(inFd, outFd, inSize);
       } else if (reqImageNum <= imageCount) {
         extract_image(reqImageNum, inFd, outFd, inSize);
       } else {
         printf("error: image file only contains %d images\n", imageCount);
       }
     } else {
       printf("error: extimage requires input and output files\n");
     }
  }

  if(display) {
    if (inFd) {
      print_image_hdr (&imageHdr);
    } else {
       printf("error: extimage no input file specified\n");
    }
  }
  
  if (saddr)
  {
    printf("%x",ntohl(imageHdr.ih_load));
  }
  if (eaddr)
  {
    printf("%x",ntohl(imageHdr.ih_ep));
  }
  return 0;
};
