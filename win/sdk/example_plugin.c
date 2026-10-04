#include "mod_api.h"
static const ModHostApi *host;
static void on_pickup(void *user, ModEvent *ev) {
    (void)user;
    host->give_item(ev->i[0], ev->i[1]);
    host->log("gave a bonus copy");
}
MOD_EXPORT int ModInit(const ModHostApi *api) {
    if (api->version != MOD_API_VERSION) return 1;
    host = api;
    return api->on("item_pickup", on_pickup, 0);
}
