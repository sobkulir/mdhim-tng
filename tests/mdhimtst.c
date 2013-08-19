/*
 mdhimiftst.c - file based test frame

 * based on the pbliftst.c
 Copyright (C) 2002 - 2007   Peter Graf

   pbliftst.c file is part of PBL - The Program Base Library.
   PBL is free software.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   For more information on the Program Base Library or Peter Graf,
   please see: http://www.mission-base.com/.


------------------------------------------------------------------------------
*/

/*
 * make sure "strings <exe> | grep Id | sort -u" shows the source file versions
 */
char * mdhimTst_c_id = "$Id: mdhimTst.c,v 1.00 2013/07/08 20:56:50 JHR Exp $";

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "mpi.h"
#include "mdhim.h"
#include "db_options.h"

// From partitioner.h:
/*
#define MDHIM_INT_KEY 1
//64 bit signed integer
#define MDHIM_LONG_INT_KEY 2
#define MDHIM_FLOAT_KEY 3
#define MDHIM_DOUBLE_KEY 4
#define MDHIM_LONG_DOUBLE_KEY 5
#define MDHIM_STRING_KEY 6
//An arbitrary sized key
#define MDHIM_BYTE_KEY 7  */

#define TEST_BUFLEN              2048

static FILE * logfile;
static FILE * infile;
int verbose = 1;   // By default generate lost of feedback status lines
int to_log = 0;
// MDHIM_INT_KEY=1, MDHIM_LONG_INT_KEY=2, MDHIM_FLOAT_KEY=3, MDHIM_DOUBLE_KEY=4
// MDHIM_LONG_DOUBLE_KEY=5, MDHIM_STRING_KEY=6, MDHIM_BYTE_KEY=7 
int key_type = 1;  // Default "int"

static char *getOpLabel[] = { "MDHIM_GET_EQ", "MDHIM_GET_NEXT", "MDHIM_GET_PREV",
                              "MDHIM_GET_FIRST", "MDHIM_GET_LAST"};

#ifdef _WIN32
#include <direct.h>
#endif

#include <stdarg.h>

static void tst_say(
char * format,
...
)
{
    va_list ap;
    
    /*
     * use fprintf to give out the text
     */
    if (to_log)
    {
        va_start( ap, format );
        vfprintf( logfile, format, ap);
        va_end(ap);
    }
    else
    {
        va_start( ap, format );
        vfprintf( stdout, format, ap);
        va_end(ap);
    }
    
}

static void putChar( int c )
{
   static int last = 0;

   if( last == '\n' && c == '\n' )
   {
       return;
   }

   last = c;
   putc( last, logfile );
}

static int getChar( void )
{
    int c;
    c = getc( infile );

    /*
     * a '#' starts a comment for the rest of the line
     */
    if( c == '#')
    {
        /*
         * comments starting with ## are duplicated to the output
         */
        c = getc( infile );
        if( c == '#' )
        {
            putChar( '#' );
            putChar( '#' );

            while( c != '\n' && c != EOF )
            {
                c = getc( infile );
                if( c != EOF )
                {
                    putChar( c );
                }
            }
        }
        else
        {
            while( c != '\n' && c != EOF )
            {
                c = getc( infile );
            }
        }
    }

/*
    if( c != EOF )
    {
        putChar( c );
    }
*/

    return( c );
}

static int getWordFromString (char *aLine,  char *buffer, int charIdx )
{
    int c;
    int i;

    // Check to see if past the end
    if (charIdx >= strlen(aLine))
    {
        *buffer = '\0';
        return charIdx;
    }
    
    /*
     * skip preceeding blanks
     */
    c = aLine[charIdx++];
    while( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
    {
        c = aLine[charIdx++];
    }

    /*
     * read one word
     */
    for( i = 0; i < TEST_BUFLEN - 1; i++, c = aLine[charIdx++] )
    {

        if( c == '\r' )
        {
            continue;
        }

        if( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
        {
            *buffer = '\0';
            return charIdx;
        }

        *buffer++ = c;
    }

    *buffer = '\0';
    return charIdx;
}

/* Read one line at a time. Skip any leadings blanks, and send an end of file
 as if a "q" command had been encountered. Return a string with the line read. */
static void getLine( char * buffer )
{
    int c;
    int i;

    // skip preceeding blanks
    c = ' ';
    while( c == '\t' || c == ' ' || c == '\n' || c == '\r' )
    {
        c = getChar();
    }

    // End of input file (even if we did not find a q!
    if( c == EOF )
    {
        *buffer++ = 'q';
        *buffer++ = '\0';
        return;
    }
    
    // Read one line
    for( i = 0; i < TEST_BUFLEN - 1; i++, c = getChar() )
    {

        if( c == EOF || c == '\n' || c == '\r' )
        {
            *buffer = '\0';
            return;
        }

        *buffer++ = c;
    }

    *buffer = '\0';
}

void usage(void)
{
	printf("Usage:\n");
	printf(" -f<BatchInputFileName> (file with batch commands)\n");
	printf(" -d<DataBaseType> (Type of DB to use: unqLite=1, levelDB=2)\n");
        printf(" -t<IndexKeyType> (Type of keys: int=1, longInt=2, float=3, "
               "double=4, longDouble=5, string=6, byte=7)\n");
        printf(" -p<pathForDataBase> (path where DB will be created)\n");
        printf(" -n<DataBaseName> (Name of DataBase file or directory)\n");
        printf(" -b<DebugLevel> (MLOG_CRIT=1, MLOG_DBG=2)\n");
        printf(" -q<0|1> (Quiet mode, default is verbose) 1=write out to log file\n");
	exit (8);
}

//======================================FLUSH============================
static void execFlush(char *command, struct mdhim_t *md, int charIdx)
{
        //Get the stats
	int ret = mdhimStatFlush(md);

	if (ret != MDHIM_SUCCESS) {
		tst_say("Error executing flush.\n");
	} else {
		tst_say("FLush executed successfully.\n");
	}
        
}

//======================================PUT============================
static void execPut(char *command, struct mdhim_t *md, int charIdx)
{
    int i_key;
    long l_key;
    float f_key;
    double d_key;
    struct mdhim_rm_t *rm;
    char str_key [ TEST_BUFLEN ];
    char buffer2 [ TEST_BUFLEN ];
    char key_string [ TEST_BUFLEN ];
    char value [ TEST_BUFLEN ];
    int ret;
    
    if (verbose) tst_say( "# put key data\n" );
    charIdx = getWordFromString( command, str_key, charIdx);
    // Get value to store
    charIdx = getWordFromString( command, buffer2, charIdx);
    sprintf(value, "%s_%d", buffer2, (md->mdhim_rank + 1));
    
    // Based on key type generate a key using rank 
    switch (key_type)
    {
        case MDHIM_INT_KEY:
             i_key = atoi(str_key) * (md->mdhim_rank + 1);
             sprintf(key_string, "%d", i_key);
             if (verbose) tst_say( "# mdhimPut( %s, %s) [int]\n", key_string, value );
             rm = mdhimPut(md, &i_key, sizeof(i_key), value, strlen(value)+1);
             break;
             
        case MDHIM_LONG_INT_KEY:
             l_key = atol(str_key) * (md->mdhim_rank + 1);
             sprintf(key_string, "%ld", l_key);
             if (verbose) tst_say( "# mdhimPut( %s, %s) [long]\n", key_string, value );
             rm = mdhimPut(md, &l_key, sizeof(l_key), value, strlen(value)+1);
             break;

        case MDHIM_FLOAT_KEY:
            f_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%f", f_key);
            if (verbose) tst_say( "# mdhimPut( %s, %s ) [float]\n", key_string, value );
            rm = mdhimPut(md, &f_key, sizeof(f_key), value, strlen(value)+1);
            break;
            
       case MDHIM_DOUBLE_KEY:
            d_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%e", d_key);
            if (verbose) tst_say( "# mdhimPut( %s, %s ) [double]\n", key_string, value );
            rm = mdhimPut(md, &d_key, sizeof(d_key), value, strlen(value)+1);
            break;
                                     
        case MDHIM_STRING_KEY:
        case MDHIM_BYTE_KEY:
             sprintf(key_string, "%s0%d", str_key, (md->mdhim_rank + 1));
             if (verbose) tst_say( "# mdhimPut( %s, %s) [string|byte]\n", key_string, value );
             rm = mdhimPut(md, (void *)key_string, strlen(key_string), value, strlen(value)+1);
             break;
             
        default:
            tst_say("Error, unrecognized Key_type in execPut\n");
    }

    // Report any error(s)
    if (!rm || rm->error)
    {
        tst_say("Error putting key: %s with value: %s into MDHIM\n", key_string, value);
    }
    else
    {
        tst_say("Successfully put key/value into MDHIM\n");
    }
    
    //Commit the database
    ret = mdhimCommit(md);
    if (ret != MDHIM_SUCCESS)
    {
        tst_say("Error committing put key: %s to MDHIM database\n", key_string);
    }
    else
    {
        tst_say("Committed put to MDHIM database\n");
    }

}

//======================================GET============================
// Operations for getting a key/value from messages.h
// MDHIM_GET_EQ=0, MDHIM_GET_NEXT=1, MDHIM_GET_PREV=2
// MDHIM_GET_FIRST=3, MDHIM_GET_LAST=4
static void execGet(char *command, struct mdhim_t *md, int charIdx)
{
    int i_key;
    long l_key;
    float f_key;
    double d_key;
    struct mdhim_getrm_t *grm;
    char str_key [ TEST_BUFLEN ];
    char buffer2 [ TEST_BUFLEN ];
    char key_string [ TEST_BUFLEN ];
    int getOp, newIdx;
    
    if (verbose) tst_say( "# get key getOperator\n" );

    charIdx = getWordFromString( command, str_key, charIdx);
    newIdx = getWordFromString( command, buffer2, charIdx);
    
    if (newIdx != charIdx)
    {
        getOp = atoi(buffer2); // Get operation type
        charIdx = newIdx;
    }
    else
    {
        getOp = MDHIM_GET_EQ;  //Default a get with an equal operator
    }
    
    // Based on key type generate a key using rank 
    switch (key_type)
    {
       case MDHIM_INT_KEY:
            i_key = atoi( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%d", i_key);
            if (verbose) tst_say( "# mdhimGet( %s, %s ) [int]\n", key_string, getOpLabel[getOp]);
            grm = mdhimGet(md, &i_key, sizeof(i_key), getOp);
            break;
            
       case MDHIM_LONG_INT_KEY:
            l_key = atol( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%ld", l_key);
            if (verbose) tst_say( "# mdhimGet( %s, %s ) [long]\n", key_string, getOpLabel[getOp]);
            grm = mdhimGet(md, &l_key, sizeof(l_key), getOp);
            break;
            
       case MDHIM_FLOAT_KEY:
            f_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%f", f_key);
            if (verbose) tst_say( "# mdhimGet( %s, %s ) [float]\n", key_string, getOpLabel[getOp]);
            grm = mdhimGet(md, &f_key, sizeof(f_key), getOp);
            break;
            
       case MDHIM_DOUBLE_KEY:
            d_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%e", d_key);
            if (verbose) tst_say( "# mdhimGet( %s, %s ) [double]\n", key_string, getOpLabel[getOp]);
            grm = mdhimGet(md, &d_key, sizeof(d_key), getOp);
            break;
                        
       case MDHIM_STRING_KEY:
       case MDHIM_BYTE_KEY:
            sprintf(key_string, "%s0%d", str_key, (md->mdhim_rank + 1));
            if (verbose) tst_say( "# mdhimGet( %s, %s ) [string|byte]\n", key_string, getOpLabel[getOp]);
            grm = mdhimGet(md, (void *)key_string, strlen(key_string), getOp);
            break;
 
       default:
            tst_say("Error, unrecognized Key_type in execGet\n");
    }
    
    // Report any error(s)
    if (!grm || grm->error)
    {
        tst_say("Error getting value for key (%s): %s from MDHIM\n", getOpLabel[getOp], key_string);
    }
    else 
    {
        tst_say("Successfully got value: %s for key (%s): %s from MDHIM\n", 
                (char *) grm->value, getOpLabel[getOp], key_string);
    }

}

//======================================BPUT============================
static void execBput(char *command, struct mdhim_t *md, int charIdx)
{
    int nkeys = 100;
    int ret;
    char buffer1 [ TEST_BUFLEN ];
    char str_key [ TEST_BUFLEN ];
    char value [ TEST_BUFLEN ];
    struct mdhim_brm_t *brm, *brmp;
    int i, size_of;
    void **keys;
    int *key_lens;
    char **values;
    int *value_lens;    
    if (verbose) tst_say( "# bput n key data\n" );

    // Number of keys to generate
    charIdx = getWordFromString( command, buffer1, charIdx);
    nkeys = atoi( buffer1 );
    
    key_lens = malloc(sizeof(int) * nkeys);
    value_lens = malloc(sizeof(int) * nkeys);
    
    // starting key value
    charIdx = getWordFromString( command, str_key, charIdx);

    // starting data value
    charIdx = getWordFromString( command, value, charIdx);

    if (verbose) tst_say( "# mdhimBPut(%d, %s, %s )\n", nkeys, str_key, value );

    // Allocate memory and size of key (size of string|byte key will be modified
    // when the key is constructed.)
    values = malloc(sizeof(char *) * nkeys);
    switch (key_type)
    {
       case MDHIM_INT_KEY:
           keys = malloc(sizeof(int *) * nkeys);
           size_of = sizeof(int);
           break;
           
       case MDHIM_LONG_INT_KEY:
           keys = malloc(sizeof(long *) * nkeys);
           size_of = sizeof(long);
           break;
           
       case MDHIM_FLOAT_KEY:
           keys = malloc(sizeof(float *) * nkeys);
           size_of = sizeof(float);
           break;
           
       case MDHIM_DOUBLE_KEY:
           keys = malloc(sizeof(double *) * nkeys);
           size_of = sizeof(double);
           break;
           
       case MDHIM_STRING_KEY:
       case MDHIM_BYTE_KEY:
           keys = malloc(sizeof(char *) * nkeys);
           size_of = sizeof(char);
           break;
    }    

    // Create the keys and values to store
    for (i = 0; i < nkeys; i++)
    {
        keys[i] = malloc(size_of);
        key_lens[i] = size_of;
        values[i] = malloc(sizeof(char) * TEST_BUFLEN);
        sprintf(values[i], "%s_%d_%d", value, (md->mdhim_rank + 1), (i + 1));
        value_lens[i] = strlen(values[i]) + 1;

        // Based on key type, rank and index number generate a key
        switch (key_type)
        {
           case MDHIM_INT_KEY:
                {
                    int **i_keys = (int **)keys;
                    *i_keys[i] = (atoi( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating int key (to insert): "
                            "%d with value: %s\n", 
                            md->mdhim_rank, *i_keys[i], values[i]);
                }
                break;

           case MDHIM_LONG_INT_KEY:
                {
                    long **l_keys = (long **)keys;
                    *l_keys[i] = (atol( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating long key (to insert): "
                            "%ld with value: %s\n", 
                            md->mdhim_rank, *l_keys[i], values[i]);
                }
                break;
                
             case MDHIM_FLOAT_KEY:
                {
                    float **f_keys = (float **)keys;
                    *f_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating float key (to insert): "
                            "%f with value: %s\n", 
                            md->mdhim_rank, *f_keys[i], values[i]);
                 }
                break;

           case MDHIM_DOUBLE_KEY:
                {
                   double **d_keys = (double **)keys;
                   *d_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                   if (verbose) tst_say("Rank: %d - Creating double key (to insert): "
                           "%e with value: %s\n", 
                            md->mdhim_rank, *d_keys[i], values[i]);
                }
                break;
                
           case MDHIM_STRING_KEY:
           case MDHIM_BYTE_KEY:
                {
                    char **s_keys = (char **)keys;
                    s_keys[i] = malloc(size_of * TEST_BUFLEN);
                    sprintf(s_keys[i], "%s0%d0%d", str_key, (md->mdhim_rank + 1), (i + 1));
                    key_lens[i] = strlen(s_keys[i]);
                    if (verbose) tst_say("Rank: %d - Creating string|byte key "
                            "(to insert): %s with value: %s\n", 
                            md->mdhim_rank, (char *)s_keys[i], values[i]);
                }
                break;
        }		
    }

    //Insert the keys into MDHIM
    brm = mdhimBPut(md, keys, key_lens, (void **) values, value_lens, nkeys);
    brmp = brm;
    if (!brm || brm->error)
    {
        tst_say("Rank - %d: Error bulk inserting keys/values into MDHIM\n", 
                    md->mdhim_rank);
    }
    
    ret = 0;
    while (brmp)
    {
        if (brmp->error < 0)
        {
            tst_say("Rank: %d - Error bulk inserting key/values info MDHIM\n", 
                        md->mdhim_rank);
            ret = 1;
        }

        brmp = brmp->next;
        //Free the message
        mdhim_full_release_msg(brm);
        brm = brmp;
    }
    
    // if NO errors report success
    if (!ret)
    {
        tst_say("Rank: %d - Successfully bulk inserted key/values into MDHIM\n", 
                        md->mdhim_rank);
    }

    //Commit the database
    ret = mdhimCommit(md);
    if (ret != MDHIM_SUCCESS)
    {
        tst_say("Error committing bput to MDHIM database\n");
    }
    else
    {
        tst_say("Committed bput to MDHIM database\n");
    }
}
        
//======================================BGET============================
static void execBget(char *command, struct mdhim_t *md, int charIdx)
{
    int nkeys = 100;
    char buffer1 [ TEST_BUFLEN ];
    char str_key [ TEST_BUFLEN ];
    if (verbose) tst_say( "# bget n key\n" );
    struct mdhim_bgetrm_t *bgrm, *bgrmp;
    int i, size_of, ret;
    void **keys;
    int *key_lens;
    
    // Get the number of records to create for bget
    charIdx = getWordFromString( command, buffer1, charIdx);
    nkeys = atoi( buffer1 );
    
    key_lens = malloc(sizeof(int) * nkeys);
    
    // Get the key to use as starting point
    charIdx = getWordFromString( command, str_key, charIdx);

    if (verbose) tst_say( "# mdhimBGet(%d, %s)\n", nkeys, str_key );

    // Allocate memory and size of key (size of string|byte key will be modified
    // when the key is constructed.)
    switch (key_type)
    {
       case MDHIM_INT_KEY:
           keys = malloc(sizeof(int *) * nkeys);
           size_of = sizeof(int);
           break;
           
       case MDHIM_LONG_INT_KEY:
           keys = malloc(sizeof(long *) * nkeys);
           size_of = sizeof(long);
           break;
           
       case MDHIM_FLOAT_KEY:
           keys = malloc(sizeof(float *) * nkeys);
           size_of = sizeof(float);
           break;
           
       case MDHIM_DOUBLE_KEY:
           keys = malloc(sizeof(double *) * nkeys);
           size_of = sizeof(double);
           break;
                 
       case MDHIM_STRING_KEY:
       case MDHIM_BYTE_KEY:
           keys = malloc(sizeof(char *) * nkeys);
           size_of = sizeof(char);
           break;
    }
    
    // Generate the keys as set above
    for (i = 0; i < nkeys; i++)
    {   
        keys[i] = malloc(size_of);
        key_lens[i] = size_of;
        
        // Based on key type, rank and index number generate a key
        switch (key_type)
        {
           case MDHIM_INT_KEY:
                {
                    int **i_keys = (int **)keys;
                    *i_keys[i] = (atoi( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating int key (to get): %d\n", 
                                         md->mdhim_rank, *i_keys[i]);
                }
                break;

           case MDHIM_LONG_INT_KEY:
                {
                    long **l_keys = (long **)keys;
                    *l_keys[i] = (atol( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating long key (to get): %ld\n", 
                                         md->mdhim_rank, *l_keys[i]);
                }
                break;

          case MDHIM_FLOAT_KEY:
                {
                    float **f_keys = (float **)keys;
                    *f_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating float key (to get): %f\n", 
                                         md->mdhim_rank, *f_keys[i]);
                }
                break;

           case MDHIM_DOUBLE_KEY:
                {
                    double **d_keys = (double **)keys;
                    *d_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating double key (to get): "
                                         " %e\n", md->mdhim_rank, *d_keys[i]);
                }
                break; 

           case MDHIM_STRING_KEY:
           case MDHIM_BYTE_KEY:
                {
                    char **s_keys = (char **)keys;
                    s_keys[i] = malloc(size_of * TEST_BUFLEN);
                    sprintf(s_keys[i], "%s0%d0%d", str_key, (md->mdhim_rank + 1), (i + 1));
                    key_lens[i] = strlen(s_keys[i]);
                    if (verbose) tst_say("Rank: %d - Creating string|byte key (to get):"
                                         " %s\n", md->mdhim_rank, s_keys[i]);
                }
                break;
  
           default:
                tst_say("Error, unrecognized Key_type in execBGet\n");
                return;
        }
            		
    }
    
    //Get the values back for each key retrieved
    bgrm = mdhimBGet(md, keys, key_lens, nkeys);
    ret = 0; // Used to determine if any errors are encountered
    bgrmp = bgrm;
    while (bgrmp) {
            if (bgrmp->error < 0)
            {
                tst_say("Rank: %d - Error retrieving values\n", 
                        md->mdhim_rank);
            }

            for (i = 0; i < bgrmp->num_records && bgrmp->error >= 0; i++)
            {
                if (verbose) tst_say("Rank: %d - got value[%d]: %s\n", 
                             md->mdhim_rank, i, (char *)bgrmp->values[i]);
            }

            bgrmp = bgrmp->next;
            //Free the message received
            mdhim_full_release_msg(bgrm);
            bgrm = bgrmp;
    }
    
    // if NO errors report success
    if (!ret)
    {
        tst_say("Rank: %d - Successfully bulk retrieved key/values from MDHIM\n", 
                        md->mdhim_rank);
    }
}
        
//======================================DEL============================
static void execDel(char *command, struct mdhim_t *md, int charIdx)
{
    int i_key;
    long l_key;
    float f_key;
    double d_key;
    char str_key [ TEST_BUFLEN ];
    char key_string [ TEST_BUFLEN ];
    struct mdhim_rm_t *rm;

    if (verbose) tst_say( "# del key\n" );

    charIdx = getWordFromString( command, str_key, charIdx);
    
    switch (key_type)
    {
       case MDHIM_INT_KEY:
            i_key = atoi( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%d", i_key);
            if (verbose) tst_say( "# mdhimDelete( %s ) [int]\n", key_string);
            rm = mdhimDelete(md, &i_key, sizeof(i_key));
            break;
            
       case MDHIM_LONG_INT_KEY:
            l_key = atol( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%ld", l_key);
            if (verbose) tst_say( "# mdhimDelete( %s ) [long]\n", key_string);
            rm = mdhimDelete(md, &l_key, sizeof(l_key));
            break;
            
       case MDHIM_FLOAT_KEY:
            f_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%f", f_key);
            if (verbose) tst_say( "# mdhimDelete( %s ) [float]\n", key_string);
            rm = mdhimDelete(md, &f_key, sizeof(f_key));
            break;
            
       case MDHIM_DOUBLE_KEY:
            d_key = atof( str_key ) * (md->mdhim_rank + 1);
            sprintf(key_string, "%e", d_key);
            if (verbose) tst_say( "# mdhimDelete( %s ) [double]\n", key_string);
            rm = mdhimDelete(md, &d_key, sizeof(d_key));
            break;
                        
       case MDHIM_STRING_KEY:
       case MDHIM_BYTE_KEY:
            sprintf(key_string, "%s0%d", str_key, (md->mdhim_rank + 1));
            if (verbose) tst_say( "# mdhimDelete( %s ) [string|byte]\n", key_string);
            rm = mdhimDelete(md, (void *)key_string, strlen(key_string));
            break;
 
       default:
            tst_say("Error, unrecognized Key_type in execDelete\n");
    }

    if (!rm || rm->error)
    {
        tst_say("Error deleting key/value from MDHIM. key: %s\n", key_string);
    }
    else
    {
        tst_say("Successfully deleted key/value from MDHIM. key: %s\n", key_string);
    }

}

//======================================NDEL============================
static void execBdel(char *command, struct mdhim_t *md, int charIdx)
{
    int nkeys = 100;
    char buffer1 [ TEST_BUFLEN ];
    char str_key [ TEST_BUFLEN ];
    void **keys;
    int *key_lens;
    struct mdhim_brm_t *brm, *brmp;
    int i, size_of, ret;
    
    if (verbose) tst_say( "# bdel n key\n" );
    
    // Number of records to delete
    charIdx = getWordFromString( command, buffer1, charIdx);
    nkeys = atoi( buffer1 );
    key_lens = malloc(sizeof(int) * nkeys);

    // Starting key value
    charIdx = getWordFromString( command, str_key, charIdx);

    if (verbose) tst_say( "# mdhimBDelete(%d, %s )\n", nkeys, str_key );
    
    // Allocate memory and size of key (size of string|byte key will be modified
    // when the key is constructed.)
    switch (key_type)
    {
       case MDHIM_INT_KEY:
           keys = malloc(sizeof(int *) * nkeys);
           size_of = sizeof(int);
           break;
           
       case MDHIM_LONG_INT_KEY:
           keys = malloc(sizeof(long *) * nkeys);
           size_of = sizeof(long);
           break;
           
       case MDHIM_FLOAT_KEY:
           keys = malloc(sizeof(float *) * nkeys);
           size_of = sizeof(float);
           break;
           
       case MDHIM_DOUBLE_KEY:
           keys = malloc(sizeof(double *) * nkeys);
           size_of = sizeof(double);
           break;
                 
       case MDHIM_STRING_KEY:
       case MDHIM_BYTE_KEY:
           keys = malloc(sizeof(char *) * nkeys);
           size_of = sizeof(char);
           break;
    }

    for (i = 0; i < nkeys; i++)
    {
        keys[i] = malloc(size_of);
        key_lens[i] = size_of;

        // Based on key type, rank and index number generate a key
        switch (key_type)
        {
           case MDHIM_INT_KEY:
                {
                    int **i_keys = (int **)keys;
                    *i_keys[i] = (atoi( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating int key (to delete): %d\n", 
                                         md->mdhim_rank, *i_keys[i]);
                }
                break;

           case MDHIM_LONG_INT_KEY:
                {
                    long **l_keys = (long **)keys;
                    *l_keys[i] = (atol( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating long key (to delete): %ld\n", 
                                         md->mdhim_rank, *l_keys[i]);
                }
                break;

          case MDHIM_FLOAT_KEY:
                {
                    float **f_keys = (float **)keys;
                    *f_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating float key (to delete): %f\n", 
                                         md->mdhim_rank, *f_keys[i]);
                }
                break;

           case MDHIM_DOUBLE_KEY:
                {
                    double **d_keys = (double **)keys;
                    *d_keys[i] = (atof( str_key ) * (md->mdhim_rank + 1)) + (i + 1);
                    if (verbose) tst_say("Rank: %d - Creating double key (to delete): "
                                         " %e\n", md->mdhim_rank, *d_keys[i]);
                }
                break; 

           case MDHIM_STRING_KEY:
           case MDHIM_BYTE_KEY:
                {
                    char **s_keys = (char **)keys;
                    s_keys[i] = malloc(size_of * TEST_BUFLEN);
                    sprintf(s_keys[i], "%s0%d0%d", str_key, (md->mdhim_rank + 1), (i + 1));
                    key_lens[i] = strlen(s_keys[i]); 
                    if (verbose) tst_say("Rank: %d - Creating string|byte key (to delete):"
                                         " %s\n", md->mdhim_rank, s_keys[i]);
                }
                break;
  
           default:
                tst_say("Error, unrecognized Key_type in execBDel\n");
                return;
        }
            
    }

    //Delete the records
    brm = mdhimBDelete(md, (void **) keys, key_lens, nkeys);
    brmp = brm;
    if (!brm || brm->error) {
            tst_say("Rank - %d: Error deleting keys/values from MDHIM\n", 
                    md->mdhim_rank);
    } 
    
    ret = 0;
    while (brmp)
    {
            if (brmp->error < 0)
            {
                    tst_say("Rank: %d - Error deleting keys\n", md->mdhim_rank);
                    ret = 1;
            }

            brmp = brmp->next;
            //Free the message
            mdhim_full_release_msg(brm);
            brm = brmp;
    }
    
    // if NO errors report success
    if (!ret)
    {
        tst_say("Rank: %d - Successfully bulk deleted key/values from MDHIM\n", 
                        md->mdhim_rank);
    }

}

/**
 * test frame for the MDHIM
 *
 * This test frame calls the MDHIM subroutines, it is an interactive or batch file
 * test frame which could be used for regression tests.
 * 
 * When the program is called in verbose mode (not quiet) it also writes a log file
 * for each rank with the name mdhimTst-#.log (where # is the rank)
 *
 * <B>Interactive mode or commands read from input file.</B>
 * -- Interactive mode simply execute mdhimtst, (by default it is verbose)
 * -- Batch mode mdhimtst -f <file_name> -d <databse_type> -t <key_type> <-quiet>
 * <UL>
 * Call the program mdhimtst from a UNIX or DOS shell. (with a -f<filename> for batch mode)
 * <BR>
 * Use the following commands to test the MDHIM subroutines supplied:
 * <UL>
 * <PRE>
 q       FOR QUIT
 /////////open filename 
 /////////transaction < START | COMMIT | ROLLBACK >
 /////////close
 /////////flush
 put key data
 bput n key data
 /////////find index key < LT | LE | FI | EQ | LA | GE | GT >
 /////////nfind n index key < LT | LE | FI | EQ | LA | GE | GT >
 get key
 bget n key
 del key
 bdel n key
 /////////datalen
 /////////readdata
 /////////readkey index
 /////////updatedata data
 /////////updatekey index key
 </PRE>
 * </UL>
 * Do the following if you want to run the test cases per hand
 * <PRE>
   1. Build the mdhimtst executable.          make all
   2. Run the test frame on a file.        mdhimtst tst0001.TST
 </PRE>
 *
 * </UL>
 */

int main( int argc, char * argv[] )
{
    char     commands[ 1000 ] [ TEST_BUFLEN ]; // Command to be read
    int      cmdIdx = 0; // Command current index
    int      cmdTot = 0; // Total number of commands read
    int      charIdx; // Index to last processed character of a command line
    char     command  [ TEST_BUFLEN ];
    char     filename [ TEST_BUFLEN ];
    char     *db_path = "./";
    char     *db_name = "mdhimTst-";
    int      dowork = 1;
    int      dbug = 1; //MLOG_CRIT=1, MLOG_DBG=2

    clock_t  begin, end;
    double   time_spent;
    
    db_options_t *db_opts; // Local variable for db create options to be passed
    
    int ret;
    int provided = 0;
    struct mdhim_t *md;
    
    int db_type = 2; //UNQLITE=1, LEVELDB=2 (data_store.h) 

    // Process arguments
    infile = stdin;
    while ((argc > 1) && (argv[1][0] == '-'))
    {
        switch (argv[1][1])
        {
            case 'f':
                printf("Input file: %s || ", &argv[1][2]);
                infile = fopen( &argv[1][2], "r" );
                if( !infile )
                {
                    fprintf( stderr, "Failed to open %s, %s\n", 
                             &argv[1][2], strerror( errno ));
                    exit( -1 );
                }
                break;

            case 'd': // DataBase type (1=unQlite, 2=levelDB)
                printf("Data Base type: %s || ", &argv[1][2]);
                db_type = atoi( &argv[1][2] );
                break;

            case 't':
                printf("Key type: %s || ", &argv[1][2]);
                key_type = atoi( &argv[1][2] );
                break;

            case 'b':
                printf("Debug mode: %s || ", &argv[1][2]);
                dbug = atoi( &argv[1][2] );
                break;
                
            case 'p':
                printf("DB Path: %s || ", &argv[1][2]);
                db_path = &argv[1][2];
                break;
                
            case 'n':
                printf("DB name: %s || ", &argv[1][2]);
                db_name = &argv[1][2];
                break;

            case 'q':
                to_log = atoi( &argv[1][2] );
                if (!to_log) 
                {
                    printf("Quiet mode || ");
                    verbose = 0;
                }
                else
                {
                    printf("Quiet to_log file mode || ");
                }
                break;
                
            default:
                printf("Wrong Argument (it will be ignored): %s\n", argv[1]);
                usage();
        }

        ++argv;
        --argc;
    }
    printf("\n");
    
    // Set the debug flag to the appropriate Mlog mask
    switch (dbug)
    {
        case 2:
            dbug = MLOG_DBG;
            break;
            
        default:
            dbug = MLOG_CRIT;
    }
    
    // calls to init MPI for mdhim
    argc = 1;  // Ignore other parameters passed to program
    ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (ret != MPI_SUCCESS)
    {
            printf("Error initializing MPI with threads\n");
            exit(1);
    }

    if (provided != MPI_THREAD_MULTIPLE)
    {
            printf("Not able to enable MPI_THREAD_MULTIPLE mode\n");
            exit(1);
    }

    // Create options for DB initialization
    db_opts = db_options_init();
    db_options_set_path(db_opts, db_path);
    db_options_set_name(db_opts, db_name);
    db_options_set_type(db_opts, db_type);
    db_options_set_key_type(db_opts, key_type);
    db_options_set_debug_level(db_opts, dbug);
    
    md = mdhimInit(MPI_COMM_WORLD, db_opts);
    if (!md)
    {
            printf("Error initializing MDHIM\n");
            exit(1);
    }	

    /*
     * open the log file (one per rank if in verbose mode, otherwise write to stderr)
     */
    if (verbose)
    {
        sprintf(filename, "./%s%d.log", db_name, md->mdhim_rank);
        logfile = fopen( filename, "wb" );
        if( !logfile )
        {
            fprintf( stderr, "can't open logfile, %s, %s\n", filename,
                     strerror( errno ));
            exit( 1 );
        }
    }
    else
    {
        logfile = stderr;
    }
    
    while( dowork && cmdIdx < 1000)
    {
        // read the next command
        memset( commands[cmdIdx], 0, sizeof( command ));
        errno = 0;
        getLine( commands[cmdIdx]);
        
        if (verbose) tst_say( "\n##command %d: %s\n", cmdIdx, commands[cmdIdx]);
        
        // Is this the last/quit command?
        if( commands[cmdIdx][0] == 'q' || commands[cmdIdx][0] == 'Q' )
        {
            dowork = 0;
        }
        cmdIdx++;
    }
    cmdTot = cmdIdx -1;

    // main command execute loop
    for(cmdIdx=0; cmdIdx < cmdTot; cmdIdx++)
    {
        memset( command, 0, sizeof( command ));
        errno = 0;
        
        charIdx = getWordFromString( commands[cmdIdx], command, 0);

        if (verbose) tst_say( "\n##exec command: %s\n", command );
        begin = clock();
        
        // execute the command given
        if( !strcmp( command, "put" ))
        {
            execPut(commands[cmdIdx], md, charIdx);
        }
        else if( !strcmp( command, "get" ))
        {
            execGet(commands[cmdIdx], md, charIdx);
        }
        else if ( !strcmp( command, "bput" ))
        {
            execBput(commands[cmdIdx], md, charIdx);
        }
        else if ( !strcmp( command, "bget" ))
        {
            execBget(commands[cmdIdx], md, charIdx);
        }
        else if( !strcmp( command, "del" ))
        {
            execDel(commands[cmdIdx], md, charIdx);
        }
        else if( !strcmp( command, "bdel" ))
        {
            execBdel(commands[cmdIdx], md, charIdx);
        }
        else if( !strcmp( command, "flush" ))
        {
            execFlush(commands[cmdIdx], md, charIdx);
        }
        else
        {
            printf( "# q       FOR QUIT\n" );
            //printf( "# open filename keyfile1,dkeyfile2,... update\n" );
            //printf( "# transaction < START | COMMIT | ROLLBACK >\n" );
            //printf( "# close\n" );
            printf( "# flush\n" );
            printf( "# put key data\n" );
            printf( "# bput n key data\n" );
            //printf( "# find index key < LT | LE | FI | EQ | LA | GE | GT >\n" );
            //printf( "# nfind n index key < LT | LE | FI | EQ | LA | GE | GT >\n" );
            printf( "# get key getOp\n           <getOp: EQ=0 | NEXT=2 | PREV=3 | FIRST=4 | LAST=5> >\n" );
            printf( "# bget n key\n" );
            //printf( "# datalen\n" );
            //printf( "# readdata\n" );
            //printf( "# readkey index\n" );
            //printf( "# updatedata data\n" );
            //printf( "# updatekey index key\n" );
            printf( "# del key\n" );
            printf( "# bdel n key\n" );
        }
        
        end = clock();
        time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
        tst_say("Seconds to %s : %f\n\n", commands[cmdIdx], time_spent);
    }
    
    fclose(logfile);
    
    // Calls to finalize mdhim session and close MPI-communication
    ret = mdhimClose(md);
    if (ret != MDHIM_SUCCESS)
    {
        tst_say("Error closing MDHIM\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();

    return( 0 );
}
