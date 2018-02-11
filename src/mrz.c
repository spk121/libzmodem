#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "zmodem.h"

static bool
approver_cb(const char *filename, size_t size, time_t date)
{
  fprintf(stderr, "Sender requests to send %s: %zu bytes\n", filename, size);
  return true;
}

static
bool tick_cb(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left)
{
  static long last_sec_left = 0;
  if (last_sec_left != sec_left && sec_left != 0) {
    fprintf(stderr, "%s: Bytes Received:%7ld/%7ld   BPS:%-8ld ETA %02d:%02d\n",
	    fname, bytes_sent, bytes_total,
	    last_bps, min_left, sec_left);
    last_sec_left = sec_left;
  }
  usleep(10000);
  return true;
}


void complete_cb(const char *filename, int result, size_t size, time_t date)
{
  if (result == RZSZ_NO_ERROR)
    fprintf(stderr, "'%s': received\n", filename);
  else
    fprintf(stderr, "'%s': failed to receive\n", filename);
}

int
main(int argc, char *argv[])
{
  int c;
  bool bps_flag = false;
  uint64_t bps = 0u;

  while ((c = getopt(argc, argv, "b:q")) != -1)
    switch(c)
      {
      case 'b':
	bps = strtoul(optarg, NULL, 10);
	if (bps > 0)
	  bps_flag = true;
	break;
      case '?':
	if (optopt == 'b')
	  fprintf(stderr, "Option -b requires an integer argument.\n");
	else if (isprint (optopt))
	  fprintf(stderr, "Unknown option '-%c'.\n", optopt);
	else
	  fprintf (stderr, "Unknown option '\\x%x'.\n", optopt);
	return 1;
      default:
	abort ();
      }
  size_t bytes = zmodem_receive(NULL, /* use current directory */
				approver_cb, /* receive everything */
				tick_cb,
				complete_cb,
				bps_flag ? bps : 0,
				RZSZ_FLAGS_NONE);
  fprintf(stderr, "Received %zu bytes.\n", bytes);
  return 0;
}
