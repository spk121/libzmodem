#ifndef LIBZMODEM_ZMODEM_H
#define LIBZMODEM_ZMODEM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes */
#define RZSZ_NO_ERROR (0)
#define RZSZ_ERROR (1)

/* Flags */
#define RZSZ_FLAGS_NONE (0x0000)

/* This runs a zmodem receiver.

   DIRECTORY is the root directory to which files will be downloaded,
   if those files are specified with a relative path.

   DIRECTORY may be NULL.  If DIRECTORY is NULL, the current working
   directory is used.

   APPROVER is a callback function. It is called when a file transfer
   request is received from the sender.  It must return 'true' to
   approve a download of the file. It is up to APPROVER to decide if
   filenames are appropriate, if absolute paths are allowed, if files
   are too large or small, and if existing files may be overwritten.

   APPROVER may be NULL.  If APPROVER is NULL, all files with relative
   pathnames will be approved -- even if they overwrite existing files
   -- and all files with absolute pathnames will be rejected.

   TICK is a callback function.  It is called after each packet is
   received from the sender.  If it returns 'true', the zmodem fetch
   will continue.  If it returns 'false', the zmodem fetch will
   terminate.  It should return quickly.

   If TICK is NULL, the transfer will continue until completion, unless
   the MIN_BPS check fails.

   COMPLETE is a callback function.  It is called when a file download
   has completed, either successfully or unsuccessfully. It should
   return quickly.  The RESULT parameter will return a result code
   indicating if the transfer was successful.

   COMPLETE may be NULL, meaning that completion data is ignored.

   MIN_BPS is the minimum data transfer rate that will be tolerated,
   in bits per second.  If the sender transfers data slower than
   MIN_BPS, the fetch will terminate.

   If MIN_BPS is zero, this check will be disabled.

   FLAGS determine how this zmodem receiver operates.

   The return value is the sum of the sizes of the files successfully
   transfered. */
size_t zmodem_receive(const char *directory,
		      bool (*approver)(const char *filename, size_t size, time_t date),
		      bool tick_cb(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left),
		      void (*complete)(const char *filename, int result, size_t size, time_t date),
		      uint64_t min_bps,
		      uint32_t flags);

/* This runs a zmodem receiver.

   DIRECTORY is the root directory from which files will be downloaded,
   if those files are specified with a relative path.

   If DIRECTORY is null, the current working directory is used.

   FILE_COUNT is the number of files to be transferred, and FILE_LIST
   is an array of strings that contains their (relative or absolute)
   pathnames.

   FILE_COUNT must be 1 or greater, and FILE_LIST must be valid,
   otherwise the result is unspecified.

   TICK is a callback function.  It is called after each packet is
   sent.  If it returns 'true', the zmodem send will continue.  If it
   returns 'false', the zmodem send will terminate prematurely.  It
   should return quickly.

   TICK may be NULL.  If TICK is NULL, the send will continue until
   completion, unless the MIN_BPS check fails.

   COMPLETE is a callback function.  It is called when a file download
   has completed, either successfully or unsuccessfully. It should
   return quickly.  The RESULT parameter will indicate if the send was
   successful.

   COMPLETE may be NULL.

   MIN_BPS is the minimum data transfer rate that will be tolerated.
   If the sender transfers data slower than MIN_BPS, the fetch
   will terminate.

   If MIN_BPS is zero, this test will be disabled.

   FLAGS determine how this zmodem sender operates.

   The return value is the sum of the sizes of the files successfully
   transfered. */
size_t zmodem_send(int file_count,
		   const char **file_list,
		   bool (*tick)(const char *fname, long bytes_sent, long bytes_total, long last_bps, int min_left, int sec_left),
		   void (*complete)(const char *filename, int result, size_t size, time_t date),
		   uint64_t min_bps,
		   uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif
