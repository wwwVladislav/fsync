#include "search_engine_sync_agent.h"
#include "sync_agents.h"
#include <futils/log.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    fsync_agent_t       agent;
    volatile uint32_t   ref_counter;
} fsearch_engine_sync_agent_t;

static fsync_agent_t* fsearch_engine_sync_agent_retain(fsearch_engine_sync_agent_t *pagent)
{
    if (pagent)
        pagent->ref_counter++;
    else
        FS_ERR("Invalid sync agent");
    return (fsync_agent_t*)pagent;
}

static void fsearch_engine_sync_agent_release(fsearch_engine_sync_agent_t *pagent)
{
    if (pagent)
    {
        if (!pagent->ref_counter)
            FS_ERR("Invalid sync agent");
        else if (!--pagent->ref_counter)
        {
            free(pagent);
        }
    }
    else
        FS_ERR("Invalid sync agent");
}

static bool fsearch_engine_sync_agent_accept(fsearch_engine_sync_agent_t *pagent, binn *metainf, fistream_t **pistream, fostream_t **postream)
{
    return true;
}

static void fsearch_engine_sync_agent_error_handler(fsearch_engine_sync_agent_t *pagent, binn *metainf, ferr_t err, char const *err_msg)
{
}

static void fsearch_engine_sync_agent_completion_handler(fsearch_engine_sync_agent_t *pagent, binn *metainf)
{
}

fsync_agent_t *search_engine_sync_agent()
{
    fsearch_engine_sync_agent_t *pagent = malloc(sizeof(fsearch_engine_sync_agent_t));
    if (!pagent)
    {
        FS_ERR("No free space of memory");
        return 0;
    }
    memset(pagent, 0, sizeof *pagent);

    pagent->ref_counter = 1;
    pagent->agent.id = FSEARCH_ENGINE_SYNC_AGENT;
    pagent->agent.retain = (fsync_agent_retain_fn_t)fsearch_engine_sync_agent_retain;
    pagent->agent.release = (fsync_agent_release_fn_t)fsearch_engine_sync_agent_release;
    pagent->agent.accept = (fsync_agent_accept_fn_t)fsearch_engine_sync_agent_accept;
    pagent->agent.failed = (fsync_error_handler_fn_t)fsearch_engine_sync_agent_error_handler;
    pagent->agent.complete = (fsync_completion_handler_fn_t)fsearch_engine_sync_agent_completion_handler;

    return (fsync_agent_t *)pagent;
}
