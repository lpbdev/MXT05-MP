/* LOG Module */
#include "logmod.h"

typedef enum
{
    LOG_OFF = 0,
    LOG_NOLIMIT,
    LOG_FIXSIZE
} logmod;

const char* strlogmod[] = {"OFF", "UNLIMIT", "FIXSIZE"};

#define LOGLINELEN 128
static logmod log_mode = LOG_OFF;
static int    log_nmax = 0;
static int    log_n    = 0;
// static int log_level = 0;
static FILE* fp_log = NULL; /* file pointer of log */
static char  logph  = '~';  // log file place holder, space( ), comma(;), or star (*), tilde(~)

// set log filesize, unit:KB
static int logsize(int filesize)
{
    if (filesize == 0)
    {
        log_mode = LOG_NOLIMIT;
        return 0;
    }

    log_mode = LOG_FIXSIZE;
    log_nmax = (int)filesize * 1024 / LOGLINELEN;

    char emptyline[LOGLINELEN];
    int  i = 0;

    memset(emptyline, logph, LOGLINELEN);
    emptyline[LOGLINELEN - 2] = '\n';
    emptyline[LOGLINELEN - 1] = '\0';

    for (i = 0; i < log_nmax - 1; i++)
    {
        fprintf(fp_log, "%s", emptyline);
    }
    fprintf(fp_log, "%010d", 0);
    return 0;
}

static int logstatus(const char* path, int filesize)
{
    printf("\n++++++++++++++++++++++++++++++++++++++\n");
    printf("LOG MODE            : %s\n", strlogmod[log_mode]);

    if (log_mode != LOG_OFF)
    {
        printf("LOG FILE PATH       : %s\n", path);
    }

    if (log_mode == LOG_FIXSIZE)
    {
        printf("LOG FILE MAX SIZE   : %d KB\n", filesize);
        printf("MAX LEN OF PER LINE : %d   \n", LOGLINELEN);
        printf("PLACE HOLDER CHAR   : %c   \n", logph);
    }

    printf("++++++++++++++++++++++++++++++++++++++\n");

    return 0;
}

/*  logopen: start log module
    log_mode change with `path` and `filesize`
    filesize unit:KB
*/
extern void logopen(const char* path, int filesize)
{
    if (path == NULL || !*path || !(fp_log = fopen(path, "w")))
    {
        log_mode = LOG_OFF;
        logstatus(path, filesize);
        fp_log = stderr;
        return;
    }

    logsize(filesize);

    logstatus(path, filesize);
    return;
}

extern void logclose()
{
    if (fp_log && fp_log != stderr)
    {
        fclose(fp_log);
        fp_log = NULL;
    }
    log_mode = LOG_OFF;
    log_nmax = 0;
    log_n    = 0;
}

static int writeLog(char* line, int linelen)
{
    int offset = 0;
    int nline  = 0;

    nline  = log_n % (log_nmax - 1);
    offset = nline * (LOGLINELEN);

    /* update oldest line */
    fseek(fp_log, sizeof(char) * offset, SEEK_SET);
    fprintf(fp_log, "%s", line);

    log_n++;

    /* write newest line number */
    offset = (log_nmax - 1) * (LOGLINELEN);
    fseek(fp_log, sizeof(char) * offset, SEEK_SET);
    fprintf(fp_log, "%010d", nline + 1);

    if (log_n > log_nmax * 3)
    {
        log_n = log_n - log_nmax;
    }

    return 0;
}

extern void logmsg(int level, const char* format, ...)
{
    va_list ap;
    char    logline[LOGLINELEN] = {0};
    int     i                   = 0;

    /* print error message to stderr */
    if (level <= 0)
    {
        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);
    }

    if (!fp_log)
    {
        return;
    }

    if (log_mode == LOG_NOLIMIT)
    {
        fprintf(fp_log, "%d ", level);
        va_start(ap, format);
        vfprintf(fp_log, format, ap);
        va_end(ap);
        fflush(fp_log);
        return;
    }

    va_start(ap, format);
    vsnprintf(logline, sizeof(logline), format, ap);
    va_end(ap);

    for (i = 0; i < LOGLINELEN - 1; i++)
    {
        if (logline[i] == '\n')
        {
            logline[i] = logph;
        }
        else if (logline[i] == '\0')
        {
            logline[i] = logph;
        }
    }

    logline[LOGLINELEN - 2] = '\n';
    logline[LOGLINELEN - 1] = '\0';

    writeLog(logline, strlen(logline));

    return;
}
