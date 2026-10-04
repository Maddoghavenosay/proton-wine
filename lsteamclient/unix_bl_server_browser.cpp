#if 0
#pragma makedep unix
#endif

/*
 * Bannerlator server browser for the Headless Steam path.
 *
 * Valve's Android libsteamclient.so (the client half the bridge loads into the game under
 * Headless Steam) ships an unfinished ISteamMatchmakingServers: the first Request*ServerList
 * trips a never-initialised thread-sync object (tier0 "Thread synchronization object is
 * unuseable") and the game hangs. Everything else the game asks of Steam goes through the
 * app's session host and works, so this file supplies the one missing piece:
 *
 *   - the server LIST comes from the app over a loopback socket (BL_SB_PORT; the app asks
 *     Steam's Web API GetServerList with the session's web token),
 *   - server DETAILS come from A2S_INFO pings sent from here (plain UDP),
 *   - players / rules / single-server pings are A2S_PLAYER / A2S_RULES / A2S_INFO,
 *   - results reach the game through the same response objects the manual layer wraps.
 *
 * Threading: the network work runs on plain pthreads, which have NO Wine TEB. Nothing on
 * those threads may touch Wine (no TRACE, no response wrappers — the wrappers TRACE). They
 * only queue results; bl_server_browser_pump(), called on the game thread from the
 * Steam_BGetCallback dispatcher (RunCallbacks, every frame), delivers the callbacks and logs.
 *
 * Opt-in: BL_SERVER_BROWSER=1 in the game's environment (the app sets it for Headless Steam
 * launches only); without it Valve's interface is handed out unchanged.
 *
 * Host protocol (text, one request per connection):
 *   -> "LIST <appid> <filter>\n"            filter = "\key\value\key\value" (may be empty)
 *   <- "S <ip> <port> [qport]\n"              endpoint only — we ping it
 *   <- "D <ip> <port> <qport> <ping> <players> <max> <bots> <secure> <password> <appid>
 *        <version>\t<map>\t<gamedir>\t<desc>\t<name>\t<tags>\n"   full item — no ping needed
 *   <- "END\n"                                (or "ERR <text>\n")
 */

#include "unix_private.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

WINE_DEFAULT_DEBUG_CHANNEL(steamclient);

namespace {

/* ── logging that is safe from any thread (Android log, no Wine involved) ──────────────── */

static void alog( const char *fmt, ... )
{
    typedef int (*log_fn)( int, const char *, const char *, ... );
    static log_fn fn = nullptr;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        if (void *h = dlopen( "liblog.so", RTLD_NOW )) fn = (log_fn)dlsym( h, "__android_log_print" );
    }
    if (!fn) return;
    char buf[1024];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof(buf), fmt, ap );
    va_end( ap );
    fn( 4 /* ANDROID_LOG_INFO */, "lsteamclient", "sb: %s", buf );
}

/* ── small helpers ─────────────────────────────────────────────────────────────────────── */

static long long now_ms()
{
    struct timeval tv;
    gettimeofday( &tv, nullptr );
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static bool env_on( const char *name )
{
    const char *v = getenv( name );
    return v && *v && strcmp( v, "0" ) != 0;
}

static int env_int( const char *name, int def )
{
    const char *v = getenv( name );
    return v && *v ? atoi( v ) : def;
}

static void copy_str( char *dst, size_t cap, const char *src )
{
    if (!cap) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strnlen( src, cap - 1 );
    memcpy( dst, src, n );
    dst[n] = 0;
}

struct endpoint
{
    uint32_t ip;    /* host order */
    uint16_t port;  /* game port */
    uint16_t qport; /* query port (0 = same as port) */
};

/* ── A2S (Source query protocol) ───────────────────────────────────────────────────────── */

static const uint8_t A2S_HEADER[4] = { 0xff, 0xff, 0xff, 0xff };

static int udp_socket( int timeout_ms )
{
    int fd = socket( AF_INET, SOCK_DGRAM, 0 );
    if (fd < 0) return -1;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    return fd;
}

/* Send a query, handle the S2C_CHALLENGE (0x41) round-trip, return the reply (header stripped
 * of the 4 0xff bytes) or false. 'want' is the expected reply type byte. */
static bool a2s_query( uint32_t ip, uint16_t port, const std::vector<uint8_t> &query, uint8_t want,
                       std::vector<uint8_t> &reply, int timeout_ms, int *rtt_ms )
{
    int fd = udp_socket( timeout_ms );
    if (fd < 0) return false;
    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons( port );
    sa.sin_addr.s_addr = htonl( ip );

    std::vector<uint8_t> pkt( A2S_HEADER, A2S_HEADER + 4 );
    pkt.insert( pkt.end(), query.begin(), query.end() );
    uint8_t buf[4096];
    bool ok = false;

    for (int attempt = 0; attempt < 3 && !ok; attempt++)
    {
        long long t0 = now_ms();
        if (sendto( fd, pkt.data(), pkt.size(), 0, (struct sockaddr *)&sa, sizeof(sa) ) < 0) break;
        ssize_t n = recvfrom( fd, buf, sizeof(buf), 0, nullptr, nullptr );
        if (n < 5 || memcmp( buf, A2S_HEADER, 4 )) break;
        if (buf[4] == 0x41 && n >= 9) /* challenge: resend with it */
        {
            std::vector<uint8_t> q( A2S_HEADER, A2S_HEADER + 4 );
            q.insert( q.end(), query.begin(), query.end() );
            /* A2S_PLAYER / A2S_RULES carry a 4-byte challenge placeholder at the end; A2S_INFO
             * appends the challenge after the string. */
            if (query[0] == 'U' || query[0] == 'V') q.resize( q.size() - 4 );
            q.insert( q.end(), buf + 5, buf + 9 );
            pkt = q;
            continue;
        }
        if (buf[4] != want) break;
        reply.assign( buf + 5, buf + n );
        if (rtt_ms) *rtt_ms = (int)(now_ms() - t0);
        ok = true;
    }
    close( fd );
    return ok;
}

struct reader
{
    const std::vector<uint8_t> &d;
    size_t p = 0;
    reader( const std::vector<uint8_t> &data ) : d( data ) {}
    bool left( size_t n ) const { return p + n <= d.size(); }
    uint8_t u8() { return left( 1 ) ? d[p++] : 0; }
    uint16_t u16() { uint16_t v = 0; if (left( 2 )) { memcpy( &v, &d[p], 2 ); p += 2; } return v; }
    uint32_t u32() { uint32_t v = 0; if (left( 4 )) { memcpy( &v, &d[p], 4 ); p += 4; } return v; }
    float f32() { float v = 0; if (left( 4 )) { memcpy( &v, &d[p], 4 ); p += 4; } return v; }
    std::string str()
    {
        std::string s;
        while (left( 1 ) && d[p]) s += (char)d[p++];
        if (left( 1 )) p++;
        return s;
    }
};

/* A2S_INFO → fills a gameserveritem_t_165. Returns false on no reply. */
static bool a2s_info( const endpoint &ep, gameserveritem_t_165 *it, int timeout_ms )
{
    static const char q[] = "TSource Engine Query";
    std::vector<uint8_t> query( q, q + sizeof(q) ); /* includes the NUL */
    std::vector<uint8_t> reply;
    int rtt = 0;
    uint16_t qport = ep.qport ? ep.qport : ep.port;
    if (!a2s_query( ep.ip, qport, query, 'I', reply, timeout_ms, &rtt )) return false;

    reader r( reply );
    r.u8(); /* protocol */
    std::string name = r.str(), map = r.str(), folder = r.str(), game = r.str();
    uint16_t appid = r.u16();
    uint8_t players = r.u8(), maxp = r.u8(), bots = r.u8();
    r.u8(); /* server type */
    r.u8(); /* environment */
    uint8_t vis = r.u8(), vac = r.u8();
    std::string version = r.str();
    uint16_t game_port = ep.port;
    uint64_t steamid = 0;
    std::string tags;
    if (r.left( 1 ))
    {
        uint8_t edf = r.u8();
        if (edf & 0x80) game_port = r.u16();
        if (edf & 0x10) steamid = ((uint64_t)r.u32()) | ((uint64_t)r.u32() << 32);
        if (edf & 0x40) { r.u16(); r.str(); }
        if (edf & 0x20) tags = r.str();
        if (edf & 0x01) { r.u32(); r.u32(); }
    }

    memset( it, 0, sizeof(*it) );
    it->m_NetAdr.m_unIP = ep.ip;
    it->m_NetAdr.m_usConnectionPort = game_port ? game_port : ep.port;
    it->m_NetAdr.m_usQueryPort = qport;
    it->m_nPing = rtt;
    it->m_bHadSuccessfulResponse = 1;
    it->m_bDoNotRefresh = 0;
    copy_str( it->m_szGameDir, sizeof(it->m_szGameDir), folder.c_str() );
    copy_str( it->m_szMap, sizeof(it->m_szMap), map.c_str() );
    copy_str( it->m_szGameDescription, sizeof(it->m_szGameDescription), game.c_str() );
    it->m_nAppID = appid;
    it->m_nPlayers = players;
    it->m_nMaxPlayers = maxp;
    it->m_nBotPlayers = bots;
    it->m_bPassword = vis ? 1 : 0;
    it->m_bSecure = vac ? 1 : 0;
    it->m_ulTimeLastPlayed = 0;
    it->m_nServerVersion = atoi( version.c_str() );
    copy_str( it->m_szServerName, sizeof(it->m_szServerName), name.c_str() );
    copy_str( it->m_szGameTags, sizeof(it->m_szGameTags), tags.c_str() );
    memcpy( &it->m_steamID, &steamid, sizeof(steamid) );
    return true;
}

/* ── list source: the app's session service ────────────────────────────────────────────── */

struct list_result
{
    std::vector<endpoint> targets;              /* "S" lines: to be pinged */
    std::vector<gameserveritem_t_165> details;  /* "D" lines: complete */
    std::string error;
};

static bool read_line( int fd, std::string &line )
{
    line.clear();
    char c;
    for (;;)
    {
        ssize_t n = recv( fd, &c, 1, 0 );
        if (n <= 0) return !line.empty();
        if (c == '\n') return true;
        line += c;
    }
}

static bool parse_ip( const char *s, uint32_t *out )
{
    struct in_addr a;
    if (inet_aton( s, &a ) == 0) return false;
    *out = ntohl( a.s_addr );
    return true;
}

static void parse_detail_line( const std::string &line, list_result &res )
{
    char ip[64];
    unsigned port, qport, players, maxp, bots, secure, password, appid, version;
    int ping;
    int consumed = 0;
    if (sscanf( line.c_str(), "D %63s %u %u %d %u %u %u %u %u %u %u%n", ip, &port, &qport, &ping, &players, &maxp,
                &bots, &secure, &password, &appid, &version, &consumed ) < 11) return;
    gameserveritem_t_165 it;
    memset( &it, 0, sizeof(it) );
    if (!parse_ip( ip, &it.m_NetAdr.m_unIP )) return;
    it.m_NetAdr.m_usConnectionPort = (uint16_t)port;
    it.m_NetAdr.m_usQueryPort = (uint16_t)(qport ? qport : port);
    it.m_nPing = ping;
    it.m_bHadSuccessfulResponse = 1;
    it.m_nPlayers = players; it.m_nMaxPlayers = maxp; it.m_nBotPlayers = bots;
    it.m_bSecure = secure ? 1 : 0; it.m_bPassword = password ? 1 : 0;
    it.m_nAppID = appid; it.m_nServerVersion = version;
    const char *p = line.c_str() + consumed;
    if (*p == '\t') p++;
    char *fields[5] = { it.m_szMap, it.m_szGameDir, it.m_szGameDescription, it.m_szServerName, it.m_szGameTags };
    const size_t caps[5] = { sizeof(it.m_szMap), sizeof(it.m_szGameDir), sizeof(it.m_szGameDescription),
                             sizeof(it.m_szServerName), sizeof(it.m_szGameTags) };
    for (int i = 0; i < 5; i++)
    {
        const char *e = strchr( p, '\t' );
        std::string v = e ? std::string( p, e - p ) : std::string( p );
        copy_str( fields[i], caps[i], v.c_str() );
        if (!e) break;
        p = e + 1;
    }
    res.details.push_back( it );
}

static bool fetch_list_from_host( uint32_t appid, const std::string &filter, list_result &res )
{
    int port = env_int( "BL_SB_PORT", 0 );
    if (!port) { res.error = "BL_SB_PORT not set"; return false; }

    int fd = socket( AF_INET, SOCK_STREAM, 0 );
    if (fd < 0) { res.error = "socket"; return false; }
    struct timeval tv = { 30, 0 };
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons( port );
    sa.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    if (connect( fd, (struct sockaddr *)&sa, sizeof(sa) ) < 0)
    {
        res.error = std::string( "connect: " ) + strerror( errno );
        close( fd );
        return false;
    }
    std::string req = "LIST " + std::to_string( appid ) + " " + filter + "\n";
    if (send( fd, req.data(), req.size(), 0 ) < 0) { res.error = "send"; close( fd ); return false; }

    std::string line;
    bool ended = false;
    while (read_line( fd, line ))
    {
        if (line == "END") { ended = true; break; }
        if (line.rfind( "ERR", 0 ) == 0) { res.error = line; break; }
        if (line.rfind( "S ", 0 ) == 0)
        {
            char ip[64]; unsigned port2 = 0, qport = 0;
            if (sscanf( line.c_str(), "S %63s %u %u", ip, &port2, &qport ) >= 2)
            {
                endpoint ep;
                if (parse_ip( ip, &ep.ip )) { ep.port = (uint16_t)port2; ep.qport = (uint16_t)qport; res.targets.push_back( ep ); }
            }
        }
        else if (line.rfind( "D ", 0 ) == 0) parse_detail_line( line, res );
    }
    close( fd );
    if (!ended && res.error.empty()) res.error = "host closed the list early";
    return ended;
}

/* ── requests ──────────────────────────────────────────────────────────────────────────── */

enum { kListInternet, kListLAN, kListFriends, kListFavorites, kListHistory, kListSpectator };

/* eMatchMakingServerResponse */
enum { eServerResponded = 0, eServerFailedToRespond = 1, eNoServersListedOnMasterServer = 2 };

struct request
{
    uint32_t appid = 0;
    int kind = kListInternet;
    std::string filter;
    u_ISteamMatchmakingServerListResponse_106 *response = nullptr;
    std::deque<gameserveritem_t_165> items;   /* deque: pointers handed to the game stay valid; the list is
                                               * filled COMPLETELY before the first callback (the PE layer sizes
                                               * its details array from GetServerCount once), pings update in place */
    std::mutex lock;
    std::atomic<bool> cancel{ false };
    std::atomic<bool> refreshing{ false };
    std::thread worker;

    ~request() { if (worker.joinable()) worker.join(); }
};

/* ── results queue: filled by workers, drained on the game thread by the pump ───────────── */

enum pend_kind
{
    PEND_LIST_ITEM,      /* rq, idx, ok */
    PEND_LIST_COMPLETE,  /* rq, result */
    PEND_PING,           /* ping_resp, ok, item */
    PEND_PLAYERS,        /* players_resp, ok, players */
    PEND_RULES,          /* rules_resp, ok, rules */
    PEND_LOG,            /* text */
};

struct player_row { std::string name; int32_t score; float time; };

struct pending
{
    pend_kind kind;
    request *rq = nullptr;
    int idx = 0;
    bool ok = false;
    uint32_t result = 0;
    int query_id = 0;
    u_ISteamMatchmakingPingResponse *ping_resp = nullptr;
    u_ISteamMatchmakingPlayersResponse *players_resp = nullptr;
    u_ISteamMatchmakingRulesResponse *rules_resp = nullptr;
    gameserveritem_t_165 item;
    std::vector<player_row> players;
    std::vector<std::pair<std::string, std::string>> rules;
    std::string text;
};

static std::mutex g_pending_lock;
static std::deque<std::unique_ptr<pending>> g_pending;

static void push_pending( std::unique_ptr<pending> p )
{
    std::lock_guard<std::mutex> g( g_pending_lock );
    g_pending.push_back( std::move( p ) );
}

static void push_log( const std::string &text )
{
    alog( "%s", text.c_str() );
    auto p = std::unique_ptr<pending>( new pending() );
    p->kind = PEND_LOG;
    p->text = text;
    push_pending( std::move( p ) );
}

static void push_list_item( request *rq, int idx, bool ok )
{
    auto p = std::unique_ptr<pending>( new pending() );
    p->kind = PEND_LIST_ITEM; p->rq = rq; p->idx = idx; p->ok = ok;
    push_pending( std::move( p ) );
}

static void push_list_complete( request *rq, uint32_t result )
{
    auto p = std::unique_ptr<pending>( new pending() );
    p->kind = PEND_LIST_COMPLETE; p->rq = rq; p->result = result;
    push_pending( std::move( p ) );
}

/* Single-server query cancellation: ids handed out by CancelServerQuery. */
static std::atomic<int> g_query_id{ 1 };
static std::mutex g_query_lock;
static std::vector<int> g_cancelled;

static bool query_cancelled( int id )
{
    std::lock_guard<std::mutex> g( g_query_lock );
    for (int c : g_cancelled) if (c == id) return true;
    return false;
}

/* ── workers (plain pthreads: NO Wine calls in here) ───────────────────────────────────── */

static std::string build_filter( MatchMakingKeyValuePair_t **filters, uint32_t n, uint32_t appid )
{
    std::string f;
    bool have_appid = false;
    /* Steamworks quirk: ppchFilters is a pointer to ONE pointer, and *ppchFilters is the start of a
     * contiguous array of n pairs (not n pointers). Reading filters[i] for i > 0 walks into the key
     * text of pair 0. */
    MatchMakingKeyValuePair_t *base = (filters && n) ? filters[0] : nullptr;
    for (uint32_t i = 0; base && i < n; i++)
    {
        const MatchMakingKeyValuePair_t &kv = base[i];
        if (!kv.m_szKey[0]) continue;
        std::string key( kv.m_szKey, strnlen( kv.m_szKey, sizeof(kv.m_szKey) ) );
        std::string val( kv.m_szValue, strnlen( kv.m_szValue, sizeof(kv.m_szValue) ) );
        if (key == "appid") have_appid = true;
        f += "\\" + key + "\\" + val;
    }
    if (!have_appid) f = "\\appid\\" + std::to_string( appid ) + f;
    return f;
}

static int append_item( request *rq, const gameserveritem_t_165 &it )
{
    std::lock_guard<std::mutex> g( rq->lock );
    rq->items.push_back( it );
    return (int)rq->items.size() - 1;
}

static void run_lan_scan( request *rq )
{
    /* Broadcast A2S_INFO on the common Source ports and collect whoever answers for ~2 s. */
    int fd = socket( AF_INET, SOCK_DGRAM, 0 );
    if (fd < 0) return;
    int yes = 1;
    setsockopt( fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes) );
    struct timeval tv = { 0, 300000 };
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    static const char q[] = "TSource Engine Query";
    std::vector<uint8_t> pkt( A2S_HEADER, A2S_HEADER + 4 );
    pkt.insert( pkt.end(), q, q + sizeof(q) );
    for (uint16_t port = 27015; port <= 27020; port++)
    {
        struct sockaddr_in sa = {};
        sa.sin_family = AF_INET; sa.sin_port = htons( port ); sa.sin_addr.s_addr = htonl( INADDR_BROADCAST );
        sendto( fd, pkt.data(), pkt.size(), 0, (struct sockaddr *)&sa, sizeof(sa) );
    }
    long long until = now_ms() + 2000;
    uint8_t buf[4096];
    std::vector<gameserveritem_t_165> found;
    while (now_ms() < until && !rq->cancel)
    {
        struct sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom( fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl );
        if (n < 5 || memcmp( buf, A2S_HEADER, 4 ) || buf[4] != 'I') continue;
        endpoint ep = { ntohl( from.sin_addr.s_addr ), ntohs( from.sin_port ), ntohs( from.sin_port ) };
        gameserveritem_t_165 it;
        if (!a2s_info( ep, &it, 1000 )) continue;
        found.push_back( it );
    }
    close( fd );
    /* Append everything first (final count), then tell the game. */
    std::vector<int> idx;
    for (auto &it : found) idx.push_back( append_item( rq, it ) );
    for (int i : idx) { if (rq->cancel) break; push_list_item( rq, i, true ); }
}

static void run_request( request *rq )
{
    /* Pace like Valve's client: the game does real work per answered server (row insert, sort,
     * map-file checks), so results must arrive at a rate it digests between frames. 16 pingers
     * ≈ 150 answers/s; the pump then hands the game a few per frame. */
    const int concurrency = env_int( "BL_SB_PINGERS", 16 );
    list_result res;
    bool ok = true;
    long long t0 = now_ms();

    if (rq->kind == kListLAN)
    {
        run_lan_scan( rq );
    }
    else if (rq->kind == kListInternet)
    {
        ok = fetch_list_from_host( rq->appid, rq->filter, res );
        if (!ok) push_log( "list for app " + std::to_string( rq->appid ) + " failed: " + res.error );
        push_log( "app " + std::to_string( rq->appid ) + " filter " + rq->filter + " -> " + std::to_string( res.details.size() )
                  + " complete items + " + std::to_string( res.targets.size() ) + " endpoints to ping ("
                  + std::to_string( now_ms() - t0 ) + " ms)" );

        /* Fill the list completely before the first callback: complete items first, then one
         * placeholder per endpoint (address known, no response yet). Indexes are stable from here. */
        std::vector<int> target_idx( res.targets.size() );
        {
            std::lock_guard<std::mutex> g( rq->lock );
            for (auto &it : res.details) rq->items.push_back( it );
            for (size_t i = 0; i < res.targets.size(); i++)
            {
                gameserveritem_t_165 ph;
                memset( &ph, 0, sizeof(ph) );
                ph.m_NetAdr.m_unIP = res.targets[i].ip;
                ph.m_NetAdr.m_usConnectionPort = res.targets[i].port;
                ph.m_NetAdr.m_usQueryPort = res.targets[i].qport ? res.targets[i].qport : res.targets[i].port;
                ph.m_nPing = -1;
                ph.m_nAppID = rq->appid;
                rq->items.push_back( ph );
                target_idx[i] = (int)rq->items.size() - 1;
            }
        }
        for (size_t i = 0; i < res.details.size(); i++)
        {
            if (rq->cancel) break;
            push_list_item( rq, (int)i, true );
        }

        /* Ping the endpoints with a pool of threads; each answer replaces its placeholder in place
         * (same index, same address the game may already hold) and the game is told; a non-answer
         * is reported as failed (what the real client does). */
        std::atomic<size_t> next{ 0 };
        std::atomic<int> answered{ 0 };
        std::vector<std::thread> pool;
        for (int t = 0; t < concurrency && t < (int)res.targets.size(); t++)
        {
            pool.emplace_back( [rq, &res, &target_idx, &next, &answered]() {
                for (;;)
                {
                    if (rq->cancel) return;
                    size_t i = next.fetch_add( 1 );
                    if (i >= res.targets.size()) return;
                    gameserveritem_t_165 it;
                    bool got = a2s_info( res.targets[i], &it, 1000 );
                    if (rq->cancel) return;
                    if (got)
                    {
                        answered++;
                        std::lock_guard<std::mutex> g( rq->lock );
                        rq->items[target_idx[i]] = it;
                    }
                    push_list_item( rq, target_idx[i], got );
                }
            } );
        }
        for (auto &th : pool) th.join();
        push_log( "app " + std::to_string( rq->appid ) + ": " + std::to_string( answered.load() ) + "/"
                  + std::to_string( res.targets.size() ) + " endpoints answered A2S_INFO (" + std::to_string( now_ms() - t0 ) + " ms)" );
    }
    /* friends / favorites / history / spectator: nothing to list on this path */

    rq->refreshing = false;
    size_t n;
    { std::lock_guard<std::mutex> g( rq->lock ); n = rq->items.size(); }
    if (!rq->cancel) push_list_complete( rq, (!ok || n == 0) ? eNoServersListedOnMasterServer : eServerResponded );
}

static request *start_request( uint32_t appid, int kind, MatchMakingKeyValuePair_t **filters, uint32_t n,
                               u_ISteamMatchmakingServerListResponse_106 *response )
{
    request *rq = new request();
    rq->appid = appid;
    rq->kind = kind;
    rq->filter = build_filter( filters, n, appid );
    rq->response = response;
    rq->refreshing = true;
    TRACE( "request %p kind %d app %u filter %s\n", rq, kind, appid, debugstr_a( rq->filter.c_str() ) );
    rq->worker = std::thread( run_request, rq );
    return rq;
}

/* ── the interface the game sees (all of these run on the game thread) ─────────────────── */

static std::mutex g_live_lock;
static std::vector<request *> g_live; /* requests not yet released: the pump drops results for others */

static bool is_live( request *rq )
{
    std::lock_guard<std::mutex> g( g_live_lock );
    for (request *r : g_live) if (r == rq) return true;
    return false;
}

struct browser_core
{
    void *request_list( int kind, uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n,
                        u_ISteamMatchmakingServerListResponse_106 *resp )
    {
        request *rq = start_request( app, kind, f, n, resp );
        { std::lock_guard<std::mutex> g( g_live_lock ); g_live.push_back( rq ); }
        return rq;
    }
    void release( void *h )
    {
        request *rq = (request *)h;
        if (!rq || !is_live( rq )) return;
        {
            std::lock_guard<std::mutex> g( g_live_lock );
            for (size_t i = 0; i < g_live.size(); i++) if (g_live[i] == rq) { g_live.erase( g_live.begin() + i ); break; }
        }
        rq->cancel = true;
        rq->response = nullptr;
        TRACE( "release %p\n", rq );
        delete rq; /* joins the worker; queued results for it are dropped by the pump */
    }
    gameserveritem_t_165 *details( void *h, int32_t i )
    {
        request *rq = (request *)h;
        if (!rq || !is_live( rq )) return nullptr;
        std::lock_guard<std::mutex> g( rq->lock );
        if (i < 0 || (size_t)i >= rq->items.size()) return nullptr;
        return &rq->items[i];
    }
    void cancel( void *h ) { request *rq = (request *)h; if (rq && is_live( rq )) rq->cancel = true; }
    void refresh( void *h )
    {
        request *rq = (request *)h;
        if (!rq || !is_live( rq ) || rq->refreshing) return;
        if (rq->worker.joinable()) rq->worker.join();
        rq->cancel = false;
        { std::lock_guard<std::mutex> g( rq->lock ); rq->items.clear(); }
        rq->refreshing = true;
        rq->worker = std::thread( run_request, rq );
    }
    int8_t is_refreshing( void *h ) { request *rq = (request *)h; return rq && is_live( rq ) && rq->refreshing ? 1 : 0; }
    int32_t count( void *h )
    {
        request *rq = (request *)h;
        if (!rq || !is_live( rq )) return 0;
        std::lock_guard<std::mutex> g( rq->lock );
        return (int32_t)rq->items.size();
    }
    void refresh_server( void *h, int32_t i )
    {
        request *rq = (request *)h;
        gameserveritem_t_165 *it = details( h, i );
        if (!it) return;
        endpoint ep = { it->m_NetAdr.m_unIP, it->m_NetAdr.m_usConnectionPort, it->m_NetAdr.m_usQueryPort };
        std::thread( [rq, ep, i]() {
            gameserveritem_t_165 fresh;
            bool got = a2s_info( ep, &fresh, 1000 );
            if (got) { std::lock_guard<std::mutex> g( rq->lock ); if ((size_t)i < rq->items.size()) rq->items[i] = fresh; }
            push_list_item( rq, i, got );
        } ).detach();
    }
    int32_t ping( uint32_t ip, uint16_t port, u_ISteamMatchmakingPingResponse *resp )
    {
        int id = g_query_id.fetch_add( 1 );
        std::thread( [ip, port, resp, id]() {
            auto p = std::unique_ptr<pending>( new pending() );
            p->kind = PEND_PING; p->ping_resp = resp; p->query_id = id;
            endpoint ep = { ip, port, port };
            p->ok = a2s_info( ep, &p->item, 1000 );
            push_pending( std::move( p ) );
        } ).detach();
        return id;
    }
    int32_t players( uint32_t ip, uint16_t port, u_ISteamMatchmakingPlayersResponse *resp )
    {
        int id = g_query_id.fetch_add( 1 );
        std::thread( [ip, port, resp, id]() {
            auto p = std::unique_ptr<pending>( new pending() );
            p->kind = PEND_PLAYERS; p->players_resp = resp; p->query_id = id;
            std::vector<uint8_t> query = { 'U', 0xff, 0xff, 0xff, 0xff }, reply;
            p->ok = a2s_query( ip, port, query, 'D', reply, 1000, nullptr );
            if (p->ok)
            {
                reader r( reply );
                uint8_t n = r.u8();
                for (uint8_t i = 0; i < n && r.left( 1 ); i++)
                {
                    r.u8();
                    player_row row;
                    row.name = r.str();
                    row.score = (int32_t)r.u32();
                    row.time = r.f32();
                    p->players.push_back( row );
                }
            }
            push_pending( std::move( p ) );
        } ).detach();
        return id;
    }
    int32_t rules( uint32_t ip, uint16_t port, u_ISteamMatchmakingRulesResponse *resp )
    {
        int id = g_query_id.fetch_add( 1 );
        std::thread( [ip, port, resp, id]() {
            auto p = std::unique_ptr<pending>( new pending() );
            p->kind = PEND_RULES; p->rules_resp = resp; p->query_id = id;
            std::vector<uint8_t> query = { 'V', 0xff, 0xff, 0xff, 0xff }, reply;
            p->ok = a2s_query( ip, port, query, 'E', reply, 1000, nullptr );
            if (p->ok)
            {
                reader r( reply );
                uint16_t n = r.u16();
                for (uint16_t i = 0; i < n && r.left( 1 ); i++)
                {
                    std::string k = r.str(), v = r.str();
                    p->rules.emplace_back( k, v );
                }
            }
            push_pending( std::move( p ) );
        } ).detach();
        return id;
    }
    void cancel_query( int32_t id )
    {
        std::lock_guard<std::mutex> g( g_query_lock );
        g_cancelled.push_back( id );
        if (g_cancelled.size() > 256) g_cancelled.erase( g_cancelled.begin() );
    }
};

static browser_core g_core;

struct browser_002 : u_ISteamMatchmakingServers_SteamMatchMakingServers002
{
    void *RequestInternetServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListInternet, app, f, n, r ); }
    void *RequestLANServerList( uint32_t app, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListLAN, app, nullptr, 0, r ); }
    void *RequestFriendsServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListFriends, app, f, n, r ); }
    void *RequestFavoritesServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListFavorites, app, f, n, r ); }
    void *RequestHistoryServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListHistory, app, f, n, r ); }
    void *RequestSpectatorServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListSpectator, app, f, n, r ); }
    void ReleaseRequest( void *h ) override { g_core.release( h ); }
    gameserveritem_t_105 *GetServerDetails( void *h, int32_t i ) override { return (gameserveritem_t_105 *)g_core.details( h, i ); }
    void CancelQuery( void *h ) override { g_core.cancel( h ); }
    void RefreshQuery( void *h ) override { g_core.refresh( h ); }
    int8_t IsRefreshing( void *h ) override { return g_core.is_refreshing( h ); }
    int32_t GetServerCount( void *h ) override { return g_core.count( h ); }
    void RefreshServer( void *h, int32_t i ) override { g_core.refresh_server( h, i ); }
    int32_t PingServer( uint32_t ip, uint16_t port, u_ISteamMatchmakingPingResponse *r ) override { return g_core.ping( ip, port, r ); }
    int32_t PlayerDetails( uint32_t ip, uint16_t port, u_ISteamMatchmakingPlayersResponse *r ) override { return g_core.players( ip, port, r ); }
    int32_t ServerRules( uint32_t ip, uint16_t port, u_ISteamMatchmakingRulesResponse *r ) override { return g_core.rules( ip, port, r ); }
    void CancelServerQuery( int32_t id ) override { g_core.cancel_query( id ); }
};

struct browser_003 : u_ISteamMatchmakingServers_SteamMatchMakingServers003
{
    void *RequestInternetServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListInternet, app, f, n, r ); }
    void *RequestLANServerList( uint32_t app, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListLAN, app, nullptr, 0, r ); }
    void *RequestFriendsServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListFriends, app, f, n, r ); }
    void *RequestFavoritesServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListFavorites, app, f, n, r ); }
    void *RequestHistoryServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListHistory, app, f, n, r ); }
    void *RequestSpectatorServerList( uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n, u_ISteamMatchmakingServerListResponse_106 *r ) override { return g_core.request_list( kListSpectator, app, f, n, r ); }
    void ReleaseRequest( void *h ) override { g_core.release( h ); }
    gameserveritem_t_165 *GetServerDetails( void *h, int32_t i ) override { return g_core.details( h, i ); }
    void CancelQuery( void *h ) override { g_core.cancel( h ); }
    void RefreshQuery( void *h ) override { g_core.refresh( h ); }
    int8_t IsRefreshing( void *h ) override { return g_core.is_refreshing( h ); }
    int32_t GetServerCount( void *h ) override { return g_core.count( h ); }
    void RefreshServer( void *h, int32_t i ) override { g_core.refresh_server( h, i ); }
    int32_t PingServer( uint32_t ip, uint16_t port, u_ISteamMatchmakingPingResponse *r ) override { return g_core.ping( ip, port, r ); }
    int32_t PlayerDetails( uint32_t ip, uint16_t port, u_ISteamMatchmakingPlayersResponse *r ) override { return g_core.players( ip, port, r ); }
    int32_t ServerRules( uint32_t ip, uint16_t port, u_ISteamMatchmakingRulesResponse *r ) override { return g_core.rules( ip, port, r ); }
    int32_t ServerFriends( uint32_t, uint16_t, u_ISteamMatchmakingServerFriendsResponse *r ) override
    {
        if (r) r->FriendsRefreshComplete();
        return g_query_id.fetch_add( 1 );
    }
    void CancelServerQuery( int32_t id ) override { g_core.cancel_query( id ); }
};

static browser_002 g_browser_002;
static browser_003 g_browser_003;
static std::atomic<bool> g_enabled{ false };

} /* namespace */

/* Called by every ISteamClient_*_GetISteamMatchmakingServers / GetISteamGenericInterface dispatcher
 * and by steamclient_CreateInterface with Valve's pointer. Returns ours when the app opted in and
 * the version is one we implement. */
extern "C" void *bl_server_browser_override( const char *version, void *valve_iface )
{
    static int enabled = -1;
    if (enabled < 0)
    {
        enabled = env_on( "BL_SERVER_BROWSER" ) ? 1 : 0;
        g_enabled = enabled == 1;
        if (enabled) TRACE( "Bannerlator server browser enabled (BL_SB_PORT=%d)\n", env_int( "BL_SB_PORT", 0 ) );
    }
    if (!enabled || !version) return valve_iface;
    if (strncmp( version, "SteamMatchMakingServers", 23 )) return valve_iface;
    if (!strcmp( version, "SteamMatchMakingServers002" )) { TRACE( "serving %s\n", version ); return &g_browser_002; }
    if (!strcmp( version, "SteamMatchMakingServers003" )) { TRACE( "serving %s\n", version ); return &g_browser_003; }
    WARN( "server browser version %s not implemented, using Valve's\n", debugstr_a( version ) );
    return valve_iface;
}

/* Game-thread pump: delivers queued results through the response wrappers (which TRACE and
 * must therefore run on a Wine thread). Called from the Steam_BGetCallback dispatcher, i.e.
 * every SteamAPI_RunCallbacks. */
extern "C" void bl_server_browser_pump( void )
{
    if (!g_enabled) return;
    /* RunCallbacks loops on Steam_BGetCallback until it comes back empty; feeding it continuously
     * would keep the game inside that loop for the whole refresh. Deliver one batch per ~frame. */
    static long long last_batch = 0;
    static int per_batch = -1;
    if (per_batch < 0) per_batch = env_int( "BL_SB_BATCH", 6 );
    long long now = now_ms();
    if (now - last_batch < 16) return;
    last_batch = now;
    for (int budget = 0; budget < per_batch; budget++)
    {
        std::unique_ptr<pending> p;
        {
            std::lock_guard<std::mutex> g( g_pending_lock );
            if (g_pending.empty()) return;
            p = std::move( g_pending.front() );
            g_pending.pop_front();
        }
        switch (p->kind)
        {
        case PEND_LOG:
            TRACE( "%s\n", p->text.c_str() );
            break;
        case PEND_LIST_ITEM:
            if (p->rq && is_live( p->rq ) && p->rq->response && !p->rq->cancel)
            {
                if (p->ok) p->rq->response->ServerResponded( p->rq, p->idx );
                else p->rq->response->ServerFailedToRespond( p->rq, p->idx );
            }
            break;
        case PEND_LIST_COMPLETE:
            if (p->rq && is_live( p->rq ) && p->rq->response && !p->rq->cancel)
            {
                TRACE( "request %p complete, result %u, %d items\n", p->rq, p->result, g_core.count( p->rq ) );
                p->rq->response->RefreshComplete( p->rq, p->result );
            }
            break;
        case PEND_PING:
            if (p->ping_resp && !query_cancelled( p->query_id ))
            {
                if (p->ok) p->ping_resp->ServerResponded( &p->item );
                else p->ping_resp->ServerFailedToRespond();
            }
            break;
        case PEND_PLAYERS:
            if (p->players_resp && !query_cancelled( p->query_id ))
            {
                if (!p->ok) p->players_resp->PlayersFailedToRespond();
                else
                {
                    for (auto &row : p->players) p->players_resp->AddPlayerToList( row.name.c_str(), row.score, row.time );
                    p->players_resp->PlayersRefreshComplete();
                }
            }
            break;
        case PEND_RULES:
            if (p->rules_resp && !query_cancelled( p->query_id ))
            {
                if (!p->ok) p->rules_resp->RulesFailedToRespond();
                else
                {
                    for (auto &kv : p->rules) p->rules_resp->RulesResponded( kv.first.c_str(), kv.second.c_str() );
                    p->rules_resp->RulesRefreshComplete();
                }
            }
            break;
        }
    }
}
