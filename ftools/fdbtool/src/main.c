#include "dbtool.h"
#include <stdio.h>
#include <strings.h>
#include <ctype.h>

static void fhelp()
{
    printf("  exit - exit the DB view\n");
    printf("  help - print help\n");
    printf("  tables - print tables list\n");
}

static const char CMD_EXIT[4]    = "exit";
static const char CMD_HELP[4]    = "help";
static const char CMD_TABLES[6]  = "tables";

int main(int argc, char **argv)
{
    fdbtool_t *ptool = fdbtool(argc > 1 ? argv[1] : 0);

    if (ptool)
    {
        char cmd[1024];
        printf(">");

        while (fgets(cmd, sizeof cmd, stdin))
        {
            if (strncasecmp(cmd, "\n", sizeof cmd) == 0)                        ;
            else if (strncasecmp(cmd, CMD_EXIT, sizeof CMD_EXIT) == 0)          break;
            else if (strncasecmp(cmd, CMD_HELP, sizeof CMD_HELP) == 0)          fhelp();
            else if (strncasecmp(cmd, CMD_TABLES, sizeof CMD_TABLES) == 0)      fdbtool_tables(ptool);
            else                                                                printf("Unknown command\n");
            printf(">");
        }

        fdbtool_close(ptool);
    }
    else if (argc < 2)
        printf("Usage: fdbtool.exe DB_dir");
    return 0;
}
