#include "core.h"
#include <stdio.h>
#include <strings.h>
#include <ctype.h>

static void fhelp()
{
    printf("\texit: exit the client\n");
    printf("\thelp: print help\n");
    printf("\tconnect IP:port: connect to other node\n");
}

static void fconnect(fcore_t *core, char *cmd)
{
    for(; *cmd && isspace(*cmd); ++cmd);
    char *c = cmd;
    for(; *c && !isspace(*c); ++c);
    *c = 0;
    fcore_connect(core, cmd);
}

static const char CMD_EXIT[4]    = "exit";
static const char CMD_HELP[4]    = "help";
static const char CMD_CONNECT[7] = "connect";

int main()
{
    fcore_t *core = fcore_start("127.0.0.1:6005");
    if (core)
    {
        char cmd[1024];
        printf(">");

        while (fgets(cmd, sizeof cmd, stdin))
        {
            if (strncasecmp(cmd, "\n", sizeof cmd) == 0)                        ;
            else if (strncasecmp(cmd, CMD_EXIT, sizeof CMD_EXIT) == 0)          break;
            else if (strncasecmp(cmd, CMD_HELP, sizeof CMD_HELP) == 0)          fhelp();
            else if (strncasecmp(cmd, CMD_CONNECT, sizeof CMD_CONNECT) == 0)    fconnect(core, cmd + sizeof CMD_CONNECT);
            else                                                                printf("Unknown command\n");
            printf(">");
        }

        fcore_stop(core);
    }

    return 0;
}
