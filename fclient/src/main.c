#include "core.h"
#include <stdio.h>
#include <strings.h>
#include <ctype.h>

static void fhelp()
{
    printf("  exit - exit the client\n");
    printf("  help - print help\n");
    printf("  connect IP:port - connect to other node\n");
    printf("  sync path - synchronize directories\n");
    printf("  nodes - print nodes list\n");
}

static void fconnect(fcore_t *core, char *cmd)
{
    for(; *cmd && isspace(*cmd); ++cmd);
    char *c = cmd;
    for(; *c && !isspace(*c); ++c);
    *c = 0;
    fcore_connect(core, cmd);
}

static void fsync(fcore_t *core, char *cmd)
{
    for(; *cmd && isspace(*cmd); ++cmd);
    char *c = cmd;
    for(; *c && !isspace(*c); ++c);
    *c = 0;
    fcore_sync(core, cmd);
}

static void fnodes(fcore_t *core)
{
    char buf[2 * sizeof(fuuid_t) + 1] = { 0 };
    fcore_nodes_iterator_t *it = 0;
    if (fcore_nodes_iterator(core, &it))
    {
        fcore_node_info_t info = { 0 };
        while(fcore_nodes_next(it, &info))
            printf("%s %s %s\n",
                    fuuid2str(&info.uuid, buf, sizeof buf),
                    info.address,
                    info.connected ? "[Connected]" : "");
        fcore_nodes_iterator_delete(it);
    }
}

static const char CMD_EXIT[4]    = "exit";
static const char CMD_HELP[4]    = "help";
static const char CMD_CONNECT[7] = "connect";
static const char CMD_SYNC[4]    = "sync";
static const char CMD_NODES[5]    = "nodes";

int main(int argc, char **argv)
{
    printf("Starting...\n");

    fcore_t *core = fcore_start(argc > 1 ? argv[1] : 0);

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
            else if (strncasecmp(cmd, CMD_SYNC, sizeof CMD_SYNC) == 0)          fsync(core, cmd + sizeof CMD_SYNC);
            else if (strncasecmp(cmd, CMD_NODES, sizeof CMD_NODES) == 0)        fnodes(core);
            else                                                                printf("Unknown command\n");
            printf(">");
        }

        fcore_stop(core);
    }
    else if (argc < 2)
        printf("Usage: client.exe IP:port");

    return 0;
}
