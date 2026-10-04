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
 *   - the server LIST comes from the app's session host over a loopback socket
 *     (BL_SB_PORT; the host asks Steam's master server through its logged-in session),
 *   - server DETAILS come from the host when it has them, otherwise from A2S_INFO pings
 *     sent from here (plain UDP),
 *   - players / rules / single-server pings are A2S_PLAYER / A2S_RULES / A2S_INFO,
 *   - results reach the game through the same response objects the manual layer wraps.
 *
 * Opt-in: BL_SERVER_BROWSER=1 in the game's environment (the app sets it for Headless Steam
 * launches only); without it Valve's interface is handed out unchanged.
 *
 * Host protocol (text, one request per connection):
 *   -> "LIST <appid> <filter>\n"            filter = "\key\value\key\value" (may be empty)
 *   <- "S <ip> <port>\n"                      ip:port only — we ping it
 *   <- "D <ip> <port> <qport> <ping> <players> <max> <bots> <secure> <password> <appid>
 *        <version>\t<map>\t<gamedir>\t<desc>\t<name>\t<tags>\n"   full item — no ping needed
 *   <- "END\n"                                (or "ERR <text>\n")
 */

#include "unix_private.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

WINE_DEFAULT_DEBUG_CHANNEL(steamclient);

namespace {

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
        if (buf[4] == 0x41 && n >= 9) /* challenge: resend with it appended (or replacing -1) */
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

/* ── list source: the app's session host ───────────────────────────────────────────────── */

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
    /* "D ip port qport ping players max bots secure password appid version\tmap\tgamedir\tdesc\tname\ttags" */
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
    const char *fields[5] = { it.m_szMap, it.m_szGameDir, it.m_szGameDescription, it.m_szServerName, it.m_szGameTags };
    const size_t caps[5] = { sizeof(it.m_szMap), sizeof(it.m_szGameDir), sizeof(it.m_szGameDescription),
                             sizeof(it.m_szServerName), sizeof(it.m_szGameTags) };
    for (int i = 0; i < 5; i++)
    {
        const char *e = strchr( p, '\t' );
        std::string v = e ? std::string( p, e - p ) : std::string( p );
        copy_str( (char *)fields[i], caps[i], v.c_str() );
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
    std::deque<gameserveritem_t_165> items;   /* deque: pointers handed to the game stay valid */
    std::mutex lock;
    std::atomic<bool> cancel{ false };
    std::atomic<bool> refreshing{ false };
    std::atomic<bool> released{ false };
    std::thread worker;

    ~request() { if (worker.joinable()) worker.join(); }
};

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
    while (now_ms() < until && !rq->cancel)
    {
        struct sockaddr_in from = {};
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom( fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl );
        if (n < 5 || memcmp( buf, A2S_HEADER, 4 ) || buf[4] != 'I') continue;
        endpoint ep = { ntohl( from.sin_addr.s_addr ), ntohs( from.sin_port ), ntohs( from.sin_port ) };
        gameserveritem_t_165 it;
        if (!a2s_info( ep, &it, 1000 )) continue;
        int idx;
        {
            std::lock_guard<std::mutex> g( rq->lock );
            rq->items.push_back( it );
            idx = (int)rq->items.size() - 1;
        }
        if (rq->response) rq->response->ServerResponded( rq, idx );
    }
    close( fd );
}

static void run_request( request *rq )
{
    const int concurrency = 48; /* GetServerList hands back up to 5000 endpoints; ~1 s timeout each */
    list_result res;
    bool ok = true;

    if (rq->kind == kListLAN)
    {
        run_lan_scan( rq );
    }
    else if (rq->kind == kListInternet)
    {
        ok = fetch_list_from_host( rq->appid, rq->filter, res );
        if (!ok) WARN( "server list for app %u failed: %s\n", rq->appid, res.error.c_str() );
        TRACE( "app %u filter %s -> %zu complete items + %zu to ping\n", rq->appid, debugstr_a( rq->filter.c_str() ),
               res.details.size(), res.targets.size() );

        for (auto &it : res.details)
        {
            if (rq->cancel) break;
            int idx;
            {
                std::lock_guard<std::mutex> g( rq->lock );
                rq->items.push_back( it );
                idx = (int)rq->items.size() - 1;
            }
            if (rq->response) rq->response->ServerResponded( rq, idx );
        }

        /* Ping the bare endpoints with a small pool of threads. Each success appends an item and
         * tells the game; failures are reported with a placeholder item so the game can show them
         * as non-responding (that is what the real client does). */
        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (int t = 0; t < concurrency && t < (int)res.targets.size(); t++)
        {
            pool.emplace_back( [rq, &res, &next]() {
                for (;;)
                {
                    if (rq->cancel) return;
                    size_t i = next.fetch_add( 1 );
                    if (i >= res.targets.size()) return;
                    gameserveritem_t_165 it;
                    bool got = a2s_info( res.targets[i], &it, 1000 );
                    int idx;
                    {
                        std::lock_guard<std::mutex> g( rq->lock );
                        if (!got)
                        {
                            memset( &it, 0, sizeof(it) );
                            it.m_NetAdr.m_unIP = res.targets[i].ip;
                            it.m_NetAdr.m_usConnectionPort = res.targets[i].port;
                            it.m_NetAdr.m_usQueryPort = res.targets[i].qport ? res.targets[i].qport : res.targets[i].port;
                            it.m_nPing = -1;
                            it.m_nAppID = rq->appid;
                        }
                        rq->items.push_back( it );
                        idx = (int)rq->items.size() - 1;
                    }
                    if (rq->cancel || !rq->response) continue;
                    if (got) rq->response->ServerResponded( rq, idx );
                    else rq->response->ServerFailedToRespond( rq, idx );
                }
            } );
        }
        for (auto &th : pool) th.join();
    }
    /* friends / favorites / history / spectator: nothing to list on this path */

    rq->refreshing = false;
    if (rq->response && !rq->cancel)
    {
        size_t n;
        { std::lock_guard<std::mutex> g( rq->lock ); n = rq->items.size(); }
        rq->response->RefreshComplete( rq, (!ok || n == 0) ? eNoServersListedOnMasterServer : eServerResponded );
    }
    TRACE( "request %p done, %zu items\n", rq, rq->items.size() );
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

/* ── single-server queries (ping / players / rules) ─────────────────────────────────────── */

static std::atomic<int> g_query_id{ 1 };
static std::mutex g_query_lock;
static std::vector<int> g_cancelled;

static bool query_cancelled( int id )
{
    std::lock_guard<std::mutex> g( g_query_lock );
    for (int c : g_cancelled) if (c == id) return true;
    return false;
}

static int spawn_query( std::thread &&t )
{
    t.detach();
    return g_query_id.fetch_add( 1 );
}

/* ── the interface the game sees ───────────────────────────────────────────────────────── */

struct browser_core
{
    void *request_list( int kind, uint32_t app, MatchMakingKeyValuePair_t **f, uint32_t n,
                        u_ISteamMatchmakingServerListResponse_106 *resp )
    {
        return start_request( app, kind, f, n, resp );
    }
    void release( void *h )
    {
        request *rq = (request *)h;
        if (!rq) return;
        rq->cancel = true;
        rq->response = nullptr;
        delete rq; /* joins the worker */
    }
    gameserveritem_t_165 *details( void *h, int32_t i )
    {
        request *rq = (request *)h;
        if (!rq) return nullptr;
        std::lock_guard<std::mutex> g( rq->lock );
        if (i < 0 || (size_t)i >= rq->items.size()) return nullptr;
        return &rq->items[i];
    }
    void cancel( void *h ) { request *rq = (request *)h; if (rq) rq->cancel = true; }
    void refresh( void *h )
    {
        request *rq = (request *)h;
        if (!rq || rq->refreshing) return;
        if (rq->worker.joinable()) rq->worker.join();
        rq->cancel = false;
        { std::lock_guard<std::mutex> g( rq->lock ); rq->items.clear(); }
        rq->refreshing = true;
        rq->worker = std::thread( run_request, rq );
    }
    int8_t is_refreshing( void *h ) { request *rq = (request *)h; return rq && rq->refreshing ? 1 : 0; }
    int32_t count( void *h )
    {
        request *rq = (request *)h;
        if (!rq) return 0;
        std::lock_guard<std::mutex> g( rq->lock );
        return (int32_t)rq->items.size();
    }
    void refresh_server( void *h, int32_t i )
    {
        request *rq = (request *)h;
        if (!rq) return;
        gameserveritem_t_165 *it = details( h, i );
        if (!it) return;
        endpoint ep = { it->m_NetAdr.m_unIP, it->m_NetAdr.m_usConnectionPort, it->m_NetAdr.m_usQueryPort };
        std::thread( [rq, ep, i]() {
            gameserveritem_t_165 fresh;
            bool got = a2s_info( ep, &fresh, 1500 );
            if (got) { std::lock_guard<std::mutex> g( rq->lock ); if ((size_t)i < rq->items.size()) rq->items[i] = fresh; }
            if (rq->response && !rq->cancel)
            {
                if (got) rq->response->ServerResponded( rq, i );
                else rq->response->ServerFailedToRespond( rq, i );
            }
        } ).detach();
    }
    int32_t ping( uint32_t ip, uint16_t port, u_ISteamMatchmakingPingResponse *resp )
    {
        int id = g_query_id.load();
        return spawn_query( std::thread( [ip, port, resp, id]() {
            gameserveritem_t_165 it;
            endpoint ep = { ip, port, port };
            bool got = a2s_info( ep, &it, 1500 );
            if (query_cancelled( id ) || !resp) return;
            if (got) resp->ServerResponded( &it );
            else resp->ServerFailedToRespond();
        } ) );
    }
    int32_t players( uint32_t ip, uint16_t port, u_ISteamMatchmakingPlayersResponse *resp )
    {
        int id = g_query_id.load();
        return spawn_query( std::thread( [ip, port, resp, id]() {
            std::vector<uint8_t> query = { 'U', 0xff, 0xff, 0xff, 0xff }, reply;
            bool ok = a2s_query( ip, port, query, 'D', reply, 1500, nullptr );
            if (query_cancelled( id ) || !resp) return;
            if (!ok) { resp->PlayersFailedToRespond(); return; }
            reader r( reply );
            uint8_t n = r.u8();
            for (uint8_t i = 0; i < n && r.left( 1 ); i++)
            {
                r.u8();
                std::string name = r.str();
                int32_t score = (int32_t)r.u32();
                float t = r.f32();
                resp->AddPlayerToList( name.c_str(), score, t );
            }
            resp->PlayersRefreshComplete();
        } ) );
    }
    int32_t rules( uint32_t ip, uint16_t port, u_ISteamMatchmakingRulesResponse *resp )
    {
        int id = g_query_id.load();
        return spawn_query( std::thread( [ip, port, resp, id]() {
            std::vector<uint8_t> query = { 'V', 0xff, 0xff, 0xff, 0xff }, reply;
            bool ok = a2s_query( ip, port, query, 'E', reply, 1500, nullptr );
            if (query_cancelled( id ) || !resp) return;
            if (!ok) { resp->RulesFailedToRespond(); return; }
            reader r( reply );
            uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && r.left( 1 ); i++)
            {
                std::string k = r.str(), v = r.str();
                resp->RulesResponded( k.c_str(), v.c_str() );
            }
            resp->RulesRefreshComplete();
        } ) );
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

} /* namespace */

/* Called by every ISteamClient_*_GetISteamMatchmakingServers dispatcher with Valve's pointer.
 * Returns ours when the app opted in and the version is one we implement. */
extern "C" void *bl_server_browser_override( const char *version, void *valve_iface )
{
    static int enabled = -1;
    if (enabled < 0)
    {
        enabled = env_on( "BL_SERVER_BROWSER" ) ? 1 : 0;
        if (enabled) TRACE( "Bannerlator server browser enabled (BL_SB_PORT=%d)\n", env_int( "BL_SB_PORT", 0 ) );
    }
    if (!enabled || !version) return valve_iface;
    if (!strcmp( version, "SteamMatchMakingServers002" )) return &g_browser_002;
    if (!strcmp( version, "SteamMatchMakingServers003" )) return &g_browser_003;
    WARN( "server browser version %s not implemented, using Valve's\n", debugstr_a( version ) );
    return valve_iface;
}
