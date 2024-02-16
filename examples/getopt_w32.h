#ifndef __GETOPT_W32_H__
#define __GETOPT_W32_H__

/* 
 * LICENSE: see LICENSE.getopt at the top-level directory. 
 *
 * This header provides declarations for a cross-platform getopt
 * function implementation, specifically designed for use in
 * Windows environments where the standard Unix `getopt` might
 * not be available.
 */

// Index of the next element to be processed in argv
extern int optind;

// Pointer to the argument of the current option, if any
extern char *optarg;

// Flag for enabling error messages: 1 enables, 0 disables
extern int opterr;

/**
 * Parses command-line options.
 * @param argc The argument count.
 * @param argv The argument vector.
 * @param optstring A string containing the legitimate option characters.
 * @return The option character if an option was successfully found, 
 *         '?' if an option was not found, or -1 if there are no more options to process.
 */
int getopt(int argc, char *argv[], const char *optstring);

#endif // __GETOPT_W32_H__
