#include <sys/select.h>
#include <sys/times.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include <linux/types.h>
#include <linux/random.h>
#include <sys/ioctl.h>

#include "bitbuffer.h"
#include "debias.h"
#include "asoundrunner.h"

static char *version_number = "0.2";
int daemonise = 0;
int verbose = 0;
int minwatermark = 256;
int maxwatermark = 4096-256;
int depositsize = 64;
int buffersize = 64 * 16;

BitBuffer incoming_bits;
BitBuffer buffered_bits;

int randomfd;

struct injector {
  int ent_count;
  int size;
  union {
    int ints[128];
    BitField bitfield[512];
  } value;
} random_injector;

int
bits_in_pool(void)
{
  int ret;
  ioctl(randomfd, RNDGETENTCNT, &ret);
  return ret;
}

void
do_mixin_bits(void)
{
  int i;
  int bits_before = bits_in_pool();
  if (verbose > 3)
    printf("Injecting %d bits of entropy into the kernel\n", depositsize * 8);
  /*
    with the correct soundcard setting, "depositsize * 8" should work correctly
    however, maybe it would be more secure to change "8" to a smaller value like 6 or 7, see also
    https://stackoverflow.com/questions/17118705/using-rndaddentropy-to-add-entropy-to-dev-random
  */
  random_injector.ent_count = depositsize * 8;
  random_injector.size = depositsize;
  for (i = 0; i < depositsize; ++i) {
    bitbuffer_extract_bits(buffered_bits, random_injector.value.bitfield + i, 8);
  }
  if (ioctl(randomfd, RNDADDENTROPY, &random_injector) == -1) {
    perror("ioctl");
  } else {
    printf("Writing %d bits of entropy to file\n", depositsize * 8);
    FILE *f;
    f = fopen("rsound.out", "a+");
    fwrite(random_injector.value.bitfield, 1, random_injector.size, f);
    fclose(f);
  }
  if (verbose > 3)
    printf("Kernel now at %d bits of entropy\n", bits_in_pool());
  if (bits_before == bits_in_pool())
    printf("Did it fail?!?!\n");
}

void
main_loop()
{
  fd_set readfds;
  fd_set writefds;
  fd_set errfds;
  int ret;
  int adding = 0;
  
  FD_ZERO(&writefds);
  
  while (1) {
    if (restart_arecord == 1) {
      int pid;
      if (verbose > 1)
        printf("Need to restart arecord.\n");
      pid = start_arecord(incoming_bits);
      if (pid == -1)
        return;
      if (verbose > 1)
        printf("Started arecord with pid %d\n", pid);
      FD_ZERO(&readfds);
      FD_ZERO(&errfds);
      if (restart_arecord) {
        printf("Arecord already died.\n");
        return;
      }
    }
    FD_SET(arecord_read_fd, &readfds);
    FD_SET(arecord_read_fd, &errfds);
    ret = select(arecord_read_fd + 1, &readfds, &writefds, &errfds, NULL);
    if (ret == -1) {
      perror("select");
      return;
    }
    if (FD_ISSET(arecord_read_fd, &readfds))
      asound_do_read();
    if (FD_ISSET(arecord_read_fd, &errfds)) {
      printf("Error on arecord fd, gotta restart.\n");
      restart_arecord = 1;
    }
    ret = transfer_bits_and_debias(incoming_bits, buffered_bits);
    if (verbose > 3 && ret > 0)
      printf("Added %d bits to cache. Now at %d/%d bits in it\n", ret, bitbuffer_available_bits(buffered_bits), buffersize * 8);
    if (bits_in_pool() <= minwatermark && adding == 0) {
      if (verbose > 2)
        printf("Transition to inserting entropy. Kernel pool at %d\n", bits_in_pool());
      adding = 1;
    }
    if (bits_in_pool() >= maxwatermark && adding == 1) {
      if (verbose > 2)
        printf("Transition to waiting. Kernel pool at %d\n", bits_in_pool());
      adding = 0;
    }
    if (adding == 1) {
      if (bitbuffer_available_bits(buffered_bits) >= (depositsize * 8)) {
        do_mixin_bits();
        if (verbose > 3)
          printf("Kernel entropy pool now sits at %d bits\n", bits_in_pool());
      }
    }
  }
}

void
usage(const char* prog, FILE *output)
{
  fprintf(output, "%s: Usage:\n"\
          "\n"\
          "Argument: h - display this help message\n"\
          "          V - display version information.\n",
          "          D - Daemonize\n"\
          "          v - Increase verbosity. Can be used more than once.\n"\
          "          m - specify minimum number of bits of entropy in the pool.\n"\
          "          M - specify max number of bits in the pool.\n"\
          "          b - specify number of bytes of randomness to buffer for use.\n"\
          "          d - specify number of bytes to deposit into the pool each time.\n",
          prog);
}

void
version(const char* prog, FILE *output)
{
  fprintf(output, "%s: %s\n"\
                  "Copyright 2007 Daniel Silverstone\n",
          prog, version_number);
}

int
main(int argc, char **argv)
{
  int opt;

  while ((opt = getopt(argc, argv, ":hDvVm:M:b:d:")) != -1) {
    switch (opt) {
    case 'h':
      usage(argv[0], stdout);
      return 0;
    case 'V':
      version(argv[0], stdout);
      return 0;
    case 'D':
      daemonise = 1;
      break;
    case 'v':
      verbose += 1;
      break;
    case 'm':
      minwatermark = atoi(optarg);
      break;
    case 'M':
      maxwatermark = atoi(optarg);
      break;
    case 'b':
      buffersize = atoi(optarg);
      break;
    case 'd':
      depositsize = atoi(optarg);
      break;
    default:
      usage(argv[0], stderr);
      return 1;
    }
  }
  
  /* Validate options now */
  if (minwatermark < 64) {
    fprintf(stderr, "Minimum watermark is below 64 bits. This is silly.\n");
    return 2;
  }
  if (maxwatermark > 4096) {
    fprintf(stderr, "Maxmimum watermark is above 4096. This is not possible.\n");
    return 2;
  }
  if (buffersize < depositsize || buffersize > (1024*1024)) {
    fprintf(stderr, "Buffer size smaller than deposit size or greater than one megabyte.\n");
    return 2;
  }
  if (depositsize < 4 || depositsize > 512 || (depositsize & 3) != 0) {
    fprintf(stderr, "Deposit size must be a multiple of four, between 4 and 512 inclusive.\n");
    return 2;
  }
  
  if (verbose > 0) {
    printf("Random sound daemon. Copyright 2007 Daniel Silverstone.\n\n");
    printf("Will keep random pool between %d and %d bits of entropy.\n", minwatermark, maxwatermark);
    printf("Will retain a buffer of %d bytes, making entropy deposits of %d bytes at a time.\n", buffersize, depositsize);
    if (daemonise == 1) {
      printf("Will daemonise.\n");
    }
  }
  
  if (verbose > 1) {
    printf("Allocating a %d bit buffer for the incoming bits.\n",
           buffersize * 8 * 4);
  }
  incoming_bits = bitbuffer_new(buffersize * 8 * 4);
  buffered_bits = bitbuffer_new(buffersize * 8);
  if (incoming_bits == NULL || buffered_bits == NULL) {
    fprintf(stderr, "Unable to allocate buffers.\n");
    return 3;
  }
  
  randomfd = open("/dev/random", O_RDWR);
  
  if (randomfd == -1) {
    perror("Opening /dev/random\n");
    return 3;
  }
  
  if (daemonise == 1) {
    printf("Daemonising\n");
    fflush(stdout);
    int fd = fork();
    if (fd == -1 ) {
      perror("forking daemon.\n");
      return 4;
    }
    if (fd != 0) return 0;
    setpgrp();
    setsid();
  }
  
  main_loop();
  
  return -1;
}
