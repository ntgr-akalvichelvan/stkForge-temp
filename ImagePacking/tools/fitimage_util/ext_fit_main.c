#include<stdio.h>
#include <stdint.h>
#include <linux/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fdt.h>
#include <libfdt.h>
#include <fdt_support.h>
#include <image.h>
/*#include <u-boot/md5.h>
#include <sha1.h>
*/

#ifdef BRCM_FIXUP
#include <libgen.h>
#endif

int debug = 0;

#ifdef BRCM_FIXUP
	static char *appname = NULL;
#endif

int get_image(const void *fit, char *image_name)
{
	int images_noffset;
#ifndef BRCM_FIXUP
	int confs_noffset;
#endif
	int noffset;
	int ndepth;
	int count = 0;
	const char *temp_image_name = NULL;
	const void *data;
#ifndef BRCM_FIXUP
	int size;
	int ofd = -1;
	struct stat sbuf;
	unsigned char *ptr;
#else
	size_t size;
	int ofd = -1;
#endif

	images_noffset = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_noffset < 0) {
		printf("Can't find images parent node '%s' (%s)\n",
			FIT_IMAGES_PATH, fdt_strerror(images_noffset));
		return -1;
	}
	/* Process its subnodes, print out component images details */
	for (ndepth = 0, count = 0,
		noffset = fdt_next_node(fit, images_noffset, &ndepth);
		(noffset >= 0) && (ndepth > 0);
		noffset = fdt_next_node(fit, noffset, &ndepth)) {

		if (ndepth == 1) {
			/*
			* Direct child node of the images parent node,
			* i.e. component image node.
			*/
			if (debug){
				printf(" Image %u (%s)\n", count++, fit_get_name(fit, noffset, NULL));
			}
			temp_image_name = fit_get_name(fit, noffset, NULL);
			if (0 == strcmp(temp_image_name, image_name)){
				if (fit_image_get_data(fit, noffset, &data, &size)){
					return -1;
				}
				/* write data to file */
				ofd = open (temp_image_name, O_CREAT|O_WRONLY);
				if (write(ofd, data, size) != size) {
					printf( "Can't write %s: %s\n",temp_image_name, strerror(errno));
					return -1;
				}
        printf("%s\n",temp_image_name);
				return 1; 
			}
			/*fit_image_print(fit, noffset, "  ");*/
		}
	}
	return 1;
}

int get_config_image(const void *fit, char *config_name, char *image_name)
{
#ifndef BRCM_FIXUP
   const void      *data;
   size_t   len;
   int      cfg_noffset;
   int      os_noffset;
   char*    prop_name;
#else
   int      cfg_noffset;
   char*    prop_name;
#endif

   cfg_noffset = fit_conf_get_node(fit, config_name);
   if (cfg_noffset < 0) {
      printf(" %s config not found.\n", config_name);
      return -1;
   }
   if (strcmp (image_name, FIT_KERNEL_PROP) == 0){
      prop_name = (char *)fdt_getprop(fit, cfg_noffset, FIT_KERNEL_PROP, NULL);
      if (prop_name != NULL){
         return get_image( fit, prop_name);
      }
   }
   else if (strcmp (image_name, FIT_RAMDISK_PROP) == 0){
      prop_name = (char *)fdt_getprop(fit, cfg_noffset, FIT_RAMDISK_PROP, NULL);
      if (prop_name != NULL){
         return get_image( fit, prop_name);
      }
   }
   else if (strcmp (image_name, FIT_FIRMWARE_PROP) == 0){
      prop_name = (char *)fdt_getprop(fit, cfg_noffset, FIT_FIRMWARE_PROP, NULL);
      if (prop_name != NULL){
         return get_image( fit, prop_name);
      }
   }
   else if (strcmp (image_name, FIT_BOOTCODE_PROP) == 0){
      prop_name = (char *)fdt_getprop(fit, cfg_noffset, FIT_BOOTCODE_PROP, NULL);
      if (prop_name != NULL){
         return get_image( fit, prop_name);
      }
   }
   else if (strcmp (image_name, FIT_SCRIPT_PROP) == 0){
      prop_name = (char *)fdt_getprop(fit, cfg_noffset, FIT_SCRIPT_PROP, NULL);
      if (prop_name != NULL){
         return get_image( fit, prop_name);
      }
   }
   else{
       return -1;
   }
}




#ifndef BRCM_FIXUP
void print_usage(void)
{
	printf(" fit_image <image name> g xxxxxx         :Get the image by name \n");
	printf(" fit_image <image name> g config xxxxx   :Get all the images in xxxxx config \n");
	printf(" fit_image <image name> p              :print the .its \n");
	printf(" fit_image <image name> p config xxxx  :print the xxxx config \n");
}
#else
void print_usage(void)
{
	char *name = "fit_image";

	if (appname != NULL)
	{
		name = basename(appname);
	}

	printf(" %s <image name> g xxxxxx         :Get the image by name \n", name);
	printf(" %s <image name> g config xxxxx   :Get all the images in xxxxx config \n", name);
	printf(" %s <image name> p                :print the .its \n", name);
	printf(" %s <image name> p config xxxx    :print the xxxx config \n", name);
}
#endif

int main(int argc, char **argv)
{
	int ifd = -1;
	struct stat sbuf;
	unsigned char *ptr;
	unsigned char *itb;
	unsigned char *command;
	unsigned char *config;
	unsigned char *image;
#if 0  /* BRCM_FIXUP */
	int  rv;
	unsigned char *temp;
#endif

#ifdef BRCM_FIXUP
    appname = argv[0];
#endif

	if (argc < 3)	{
		print_usage();
		return -1;
	}
	/* input file name */
	itb = argv[1];
	command = argv[2];

	/* open the input file and mmap it for reading */
	ifd = open (itb, O_RDONLY);
	if (ifd < 0) {
		printf ("Can't open the file %s :%s\n",itb, strerror(errno));
		return -1;
	}
	if (fstat(ifd, &sbuf) < 0){
		printf ( "Can't stat %s\n", strerror(errno));
		(void) close (ifd);
		return -1;
	}

	ptr = mmap(0, sbuf.st_size, PROT_READ, MAP_FILE|MAP_SHARED, ifd, 0);
	if (ptr == MAP_FAILED){
		printf ("Can't read  %s\n", strerror(errno));
		(void) close (ifd);
		return -1;
	}
  
	/* Check the command and perform the operation */
	if (command[0] == 'g'){
		if (argc == 4){
			image = argv[3]; 
			/* Get the image by name */
			get_image( ptr, image);
		}
		else if (argc == 5){
			config = argv[3];
			image = argv[4];
      if ( get_config_image(ptr, config, image) <= 0){
         printf(" Unable to get the image\n");
         return -1;
      }
 
		}
		else {
			print_usage();
			close (ifd);
			(void) munmap((void *)ptr, sbuf.st_size);
			return -1;
		}
	}
	else if (command[0] == 'p'){
		if (argc == 3){
			fit_print_contents((const void *)ptr);
		}
	}
	else{
		close(ifd);
		(void) munmap((void *)ptr, sbuf.st_size);
		print_usage();
		return -1;
	}

	close (ifd);
	munmap((void *)ptr, sbuf.st_size);
	return 0;

#if 0
  ifd = open ("multi.itb", O_RDONLY);
  
  if (ifd < 0) {
    printf ("Can't open %s\n",strerror(errno));
    return 0;
  }


  if (fstat(ifd, &sbuf) < 0) {
    printf ( "Can't stat %s\n", strerror(errno));
    (void) close (ifd);
  }

  ptr = mmap(0, sbuf.st_size, PROT_READ, MAP_FILE|MAP_SHARED, ifd, 0);
  if (ptr == MAP_FAILED) {
    printf ("Can't read  %s\n", strerror(errno));
    (void) close (ifd);
    return 0;
  }


  printf("hello fit\n");
  rv = fdt_check_header((const void *)ptr );
  printf(" return val %d\n",rv);
  fit_print_contents((const void *)ptr);

  get_image((const void *)ptr,"kernel@0");





  (void) close (ifd);
  (void) munmap((void *)ptr, sbuf.st_size);
  return 0;
#endif

}
