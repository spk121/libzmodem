#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "zmodem.h"

static
bool tick_cb(const char *filename, size_t bytes_received)
{
  return true;
}

void complete_cb(const char *filename, int result, size_t size, time_t date)
{
  if (result == RZSZ_NO_ERROR)
    fprintf(stderr, "'%s': success\n", filename);
  else
    fprintf(stderr, "'%s': failure\n", filename);
}
int
main(int argc, char *argv[])
{
  int c;
  bool bps_flag = false;;
  uint64_t bps = 0u;
  int n_filenames = 0;
  const char **filenames = NULL;

  while ((c = getopt(argc, argv, "b:") != -1))
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

  struct stat st;
  int ret;
  filenames = (const char **) malloc (argc * sizeof(char *));
  memset(filenames, 0, argc * sizeof(char *));
  for (int i = optind; i < argc; i++)
    {
      // These should be filenames of regular files
      ret = stat(argv[i], &st);
      if (ret == -1)
	fprintf(stderr, "'%s' does not exist.\n", argv[i]);
      else
	{
	  if (S_ISDIR(st.st_mode))
	    fprintf(stderr, "'%s' is a directory.\n", argv[i]);
	  else if (!S_ISREG(st.st_mode))
	    fprintf(stderr, "'%s' is not a regular file.\n", argv[i]);
	  else
	    {
	      filenames[n_filenames] = strdup(argv[i]);
	      n_filenames ++;
	    }
	}
    }

  if (n_filenames == 0)
    {
      fprintf(stderr, "No files to send.\n");
      free (filenames);
      return 0;
    }
  size_t bytes = zmodem_send(n_filenames, filenames,
			     tick_cb,
			     complete_cb,
			     bps_flag ? bps : 0,
			     RZSZ_FLAGS_NONE);
  fprintf(stderr, "Sent %zu bytes.\n", bytes);
  for (int i = 0; i < argc; i ++)
    free (filenames[i]);
  free (filenames);
  return 0;
}
