/*
 * Bannerlator server browser — x86-64 front for ISteamMatchmakingServers (blsteambrowser.dll).
 *
 * Built as a plain x86-64 DLL (NOT ARM64EC) and loaded by lsteamclient.dll on 64-bit games when
 * the Bannerlator browser is active. The game calls the browser interface through this vtable, so
 * the three calls its list sort hammers — GetServerDetails / GetServerCount / IsRefreshing, ~60k
 * per frame for a 5000-row list — stay inside the x86 JIT: they read fields the unix side keeps
 * in the request handle (an array of items at a stable address, its count, the refreshing flag)
 * instead of crossing into ARM64EC code (several µs each under FEX) like every lsteamclient
 * wrapper call does. Everything else forwards to the real (ARM64EC) wrapper, which is how the game
 * would have called it anyway.
 *
 * Field offsets inside the request handle are passed in by lsteamclient (offsetof in its own
 * header), so this file carries no layout assumptions beyond "little-endian 64-bit".
 */

#include <windows.h>

typedef void *(__cdecl *fn_req_filters)( void *self, UINT32 app, void *filters, UINT32 n, void *resp );
typedef void *(__cdecl *fn_req_plain)( void *self, UINT32 app, void *resp );
typedef void  (__cdecl *fn_handle)( void *self, void *h );
typedef void *(__cdecl *fn_details)( void *self, void *h, INT32 i );
typedef INT8  (__cdecl *fn_handle_i8)( void *self, void *h );
typedef INT32 (__cdecl *fn_handle_i32)( void *self, void *h );
typedef void  (__cdecl *fn_handle_idx)( void *self, void *h, INT32 i );
typedef INT32 (__cdecl *fn_query)( void *self, UINT32 ip, UINT16 port, void *resp );
typedef void  (__cdecl *fn_cancel_query)( void *self, INT32 id );

struct shim
{
    const void **vtable;
    void *real;            /* lsteamclient's w_iface for this version (ARM64EC code behind it) */
    void **real_vt;
    UINT32 version;        /* 2 or 3 */
    UINT32 stride;         /* sizeof(gameserveritem_t_165) on the unix side */
    UINT32 off_items;      /* offsets inside the request handle */
    UINT32 off_count;
    UINT32 off_refreshing;
    unsigned char zero_item[1024];
};

#define ITEMS(s, h)      (*(UINT64 *)((char *)(h) + (s)->off_items))
#define COUNT(s, h)      (*(UINT32 *)((char *)(h) + (s)->off_count))
#define REFRESHING(s, h) (*(UINT32 *)((char *)(h) + (s)->off_refreshing))

/* slot numbers in both versions: 0..6 requests + release, 7 details, 8 cancel, 9 refresh,
 * 10 is_refreshing, 11 count, 12 refresh_server, 13 ping, 14 players, 15 rules,
 * 002: 16 cancel_server_query; 003: 16 server_friends, 17 cancel_server_query */

static void *__cdecl S_RequestInternetServerList( struct shim *s, UINT32 app, void *f, UINT32 n, void *r ) { return ((fn_req_filters)s->real_vt[0])( s->real, app, f, n, r ); }
static void *__cdecl S_RequestLANServerList( struct shim *s, UINT32 app, void *r ) { return ((fn_req_plain)s->real_vt[1])( s->real, app, r ); }
static void *__cdecl S_RequestFriendsServerList( struct shim *s, UINT32 app, void *f, UINT32 n, void *r ) { return ((fn_req_filters)s->real_vt[2])( s->real, app, f, n, r ); }
static void *__cdecl S_RequestFavoritesServerList( struct shim *s, UINT32 app, void *f, UINT32 n, void *r ) { return ((fn_req_filters)s->real_vt[3])( s->real, app, f, n, r ); }
static void *__cdecl S_RequestHistoryServerList( struct shim *s, UINT32 app, void *f, UINT32 n, void *r ) { return ((fn_req_filters)s->real_vt[4])( s->real, app, f, n, r ); }
static void *__cdecl S_RequestSpectatorServerList( struct shim *s, UINT32 app, void *f, UINT32 n, void *r ) { return ((fn_req_filters)s->real_vt[5])( s->real, app, f, n, r ); }
static void  __cdecl S_ReleaseRequest( struct shim *s, void *h ) { ((fn_handle)s->real_vt[6])( s->real, h ); }

static void *__cdecl S_GetServerDetails( struct shim *s, void *h, INT32 i )
{
    if (h)
    {
        UINT64 items = ITEMS( s, h );
        UINT32 count = COUNT( s, h );
        if (items)
        {
            if (i >= 0 && (UINT32)i < count) return (char *)(UINT_PTR)items + (UINT64)i * s->stride;
            return s->zero_item; /* what Valve's client does for a bad index: a zeroed item */
        }
    }
    return ((fn_details)s->real_vt[7])( s->real, h, i ); /* list not built yet: ask the real one */
}

static void __cdecl S_CancelQuery( struct shim *s, void *h ) { ((fn_handle)s->real_vt[8])( s->real, h ); }
static void __cdecl S_RefreshQuery( struct shim *s, void *h ) { ((fn_handle)s->real_vt[9])( s->real, h ); }

static INT8 __cdecl S_IsRefreshing( struct shim *s, void *h )
{
    if (h && ITEMS( s, h )) return REFRESHING( s, h ) ? 1 : 0;
    return ((fn_handle_i8)s->real_vt[10])( s->real, h );
}

static INT32 __cdecl S_GetServerCount( struct shim *s, void *h )
{
    if (h && ITEMS( s, h )) return (INT32)COUNT( s, h );
    return ((fn_handle_i32)s->real_vt[11])( s->real, h );
}

static void  __cdecl S_RefreshServer( struct shim *s, void *h, INT32 i ) { ((fn_handle_idx)s->real_vt[12])( s->real, h, i ); }
static INT32 __cdecl S_PingServer( struct shim *s, UINT32 ip, UINT16 port, void *r ) { return ((fn_query)s->real_vt[13])( s->real, ip, port, r ); }
static INT32 __cdecl S_PlayerDetails( struct shim *s, UINT32 ip, UINT16 port, void *r ) { return ((fn_query)s->real_vt[14])( s->real, ip, port, r ); }
static INT32 __cdecl S_ServerRules( struct shim *s, UINT32 ip, UINT16 port, void *r ) { return ((fn_query)s->real_vt[15])( s->real, ip, port, r ); }
static void  __cdecl S002_CancelServerQuery( struct shim *s, INT32 id ) { ((fn_cancel_query)s->real_vt[16])( s->real, id ); }
static INT32 __cdecl S003_ServerFriends( struct shim *s, UINT32 ip, UINT16 port, void *r ) { return ((fn_query)s->real_vt[16])( s->real, ip, port, r ); }
static void  __cdecl S003_CancelServerQuery( struct shim *s, INT32 id ) { ((fn_cancel_query)s->real_vt[17])( s->real, id ); }

static const void *vtable_002[] =
{
    S_RequestInternetServerList, S_RequestLANServerList, S_RequestFriendsServerList, S_RequestFavoritesServerList,
    S_RequestHistoryServerList, S_RequestSpectatorServerList, S_ReleaseRequest, S_GetServerDetails, S_CancelQuery,
    S_RefreshQuery, S_IsRefreshing, S_GetServerCount, S_RefreshServer, S_PingServer, S_PlayerDetails, S_ServerRules,
    S002_CancelServerQuery,
};

static const void *vtable_003[] =
{
    S_RequestInternetServerList, S_RequestLANServerList, S_RequestFriendsServerList, S_RequestFavoritesServerList,
    S_RequestHistoryServerList, S_RequestSpectatorServerList, S_ReleaseRequest, S_GetServerDetails, S_CancelQuery,
    S_RefreshQuery, S_IsRefreshing, S_GetServerCount, S_RefreshServer, S_PingServer, S_PlayerDetails, S_ServerRules,
    S003_ServerFriends, S003_CancelServerQuery,
};

/* Called by lsteamclient (ARM64EC → x86 transition, once per interface). */
__declspec(dllexport) void *__cdecl bl_shim_create( UINT32 version, void *real, UINT32 stride,
                                                     UINT32 off_items, UINT32 off_count, UINT32 off_refreshing )
{
    struct shim *s;
    if (!real || (version != 2 && version != 3)) return NULL;
    s = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s) );
    if (!s) return NULL;
    s->vtable = version == 2 ? vtable_002 : vtable_003;
    s->real = real;
    s->real_vt = *(void ***)real;
    s->version = version;
    s->stride = stride;
    s->off_items = off_items;
    s->off_count = off_count;
    s->off_refreshing = off_refreshing;
    return s;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls( inst );
    return TRUE;
}
