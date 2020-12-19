
#include <mpmc_bounded_queue.h>
#include <iostream>
#include <map>
#include <vector>
#include <thread.h>
#include <algorithm>
#include <set>
#include <list>
#include <string.h>

enum {
    SHARDS = 13,
    NODES = 5,
    CLIENTS = 7
};

enum { DATA_COUNT = 1000000 };

std::atomic<uint32_t> s_states[NODES];

typedef std::vector<uint32_t> ids_t;

uint32_t hash(const void* key_, size_t len, uint32_t seed = 0);

typedef std::pair<uint32_t, uint32_t> pairuu_t;
std::vector<pairuu_t> get_RVH_table_for_shard_id (uint32_t key, ids_t const & node);

void get_owners_table_for_shard_id (uint32_t key, ids_t const & nodes, ids_t & owners);

struct message
{
    enum cmd : uint32_t {
        nop,
        put,
        get,
        del,
        get_replica,
        eof,
        cluster_changed
    };
    cmd _cmd = nop;
    uint32_t _shard = 0xffffffffU;
    uint32_t _back_queue_id = 0xffffffffU;
    uint32_t _key = 0;
    uint32_t _data = 0;
};

typedef mpmc_bounded_queue<message> mqueue_t;

mqueue_t s_node_channels[NODES];
mqueue_t s_client_channels[CLIENTS];

typedef std::map<uint32_t, uint32_t> storage_t;

bool send_message_to_node (uint32_t node_id, message const & msg)
{
    while( not s_node_channels[node_id].enqueue(msg)) usleep(1000);
    return true;
}

struct node
    : thread
{
    struct standby
    {
        uint32_t _last_sent = 0;
        uint32_t _node_id = 0xffffffffU;
        uint32_t _sent = 0;
        uint32_t _shard_id = 0;
    };

    void make_routes ()
    {
        // std::cout << "routes:\n";
        ids_t nodes = {0, 1, 2, 3, 4};
        for (uint32_t k = 0; k < SHARDS; ++k ) {
            get_owners_table_for_shard_id(k, nodes, _owners[k]);
            // std::cout << "shard " << k << " -> " << _owners[k][0] << std::endl;
        }
    }

    // bool is_owner_shard (uint32_t shard, uint32_t & failover_id) const
    // {
    //     failover_id = uint32_t(-1);
    //     auto  & replicas = _owners[shard];
    //     uint32_t idx = 0;
    //     for ( ; idx < replicas.size() && !s_states[replicas[idx]]; ++idx );
    //     bool is_owner = (_id == replicas[idx]);
    //     if ( is_owner && idx < (replicas.size() - 1) ) {
    //         failover_id = replicas[idx + 1];
    //     }
    //     return is_owner;
    // }

    void get_owner_for_shard (uint32_t shard, uint32_t & owner_id, uint32_t & failover_id) const
    {
        failover_id = uint32_t(-1);
        owner_id = uint32_t(-1);
        auto  & replicas = _owners[shard];
        auto const failovers = replicas.size();
        uint32_t idx = 0;
        for ( ; idx < failovers && 0 == s_states[replicas[idx]]; ++idx );
        if ( failovers > idx ) {
            owner_id = replicas[idx];
            for ( ++idx ; idx < failovers && 0 == s_states[replicas[idx]]; ++idx );
            if ( failovers > idx ) {
                failover_id = replicas[idx];
            }
        }
    }

    int send_to_replica_node (message const & msg)
    {
        auto & r = replications[msg._shard];
        if ( 0xffffffffU != r._node_id) {
            if ( r._last_sent > msg._key ) {
                if ( !s_node_channels[r._node_id].enqueue(msg) ) {
                    buffer.push_back( {r._node_id, msg } );
                }
            }
        }
        return 0;
    }

    int send_to_failover_node (message const & msg)
    {
        uint32_t failover_node_id, owner_id;
        get_owner_for_shard(msg._shard, owner_id, failover_node_id);
        if ( owner_id == _id && uint32_t(-1) != failover_node_id ) {
            if ( !s_node_channels[failover_node_id].enqueue(msg) ) {
                buffer.push_back( {failover_node_id, msg } );
            }
        }
        return 0;
    }

    int replicate_record (standby & stand, uint32_t key, uint32_t value)
    {
        int rc = -1;
        uint32_t const shard = hash(&key, sizeof(key)) % SHARDS;
        message msg { message::put, shard, 0xff'ff'ff'ffU, key, value };
        if ( s_node_channels[stand._node_id].enqueue( msg ) ) {
            stand._last_sent = key;
            ++stand._sent;
            rc = 0;
        }
        return rc;
    }

    bool has_requests () const 
    {
        return 0 < s_node_channels[_id].count();
    }

    void send_buffered ()
    {
        auto f = buffer.begin(), l = buffer.end();
        for ( ; f != l; ) {
            if ( s_node_channels[f->first].enqueue(f->second) ) {
                buffer.erase(f++);
            } else {
                break;
            }
        }
    }

    int replicate ()
    {
        auto & ctx = _pending_replicas.front();
        storage_t & storage = _shards[ctx._shard_id];
        storage_t::iterator what = storage.begin();

        if ( 0 < ctx._sent ) {
            what = storage.upper_bound(ctx._last_sent);
        }

        auto const end = storage.end();

        int rc = 0;

        bool accepted = true;
        for ( uint32_t i = 0; what != end and i < 32 and not has_requests() and accepted; ++i ) {
            auto res = replicate_record(ctx, what->first, what->second);
            accepted = 0 == res;
            if (accepted) {
                ++what;
                ++rc;
            }
        }

        if ( end == what ) {
            if ( s_node_channels[ctx._node_id].enqueue( { message::eof, ctx._shard_id, 0xff'ff'ff'ffU, ctx._sent, 0} ) ) {

                uint32_t owner_id, failover_id;
                get_owner_for_shard(ctx._shard_id, owner_id, failover_id);
                if ( owner_id != _id && failover_id != _id ) {
                    std::cout << "clear shard[" << ctx._shard_id << "]" << std::endl;
                    _shards[ctx._shard_id].clear();
                }

                replications[ctx._shard_id]._node_id = 0xff'ff'ff'ffU;
                replications[ctx._shard_id]._sent = 0;
                _pending_replicas.pop_front();
            }
        }

        return rc;
    }

    void on_cluster_changed ()
    {
        ids_t nodes;
        for ( uint32_t idx = 0; idx < NODES; ++idx ) {
            if ( s_states[idx] ) nodes.push_back(idx);
        }
        ids_t owners[SHARDS];
        for (uint32_t shard_id = 0; shard_id < SHARDS; ++shard_id) {
            auto & sho = owners[shard_id];
            get_owners_table_for_shard_id (shard_id, nodes, sho);
            if ( _id == sho[0] && _id != _owners[shard_id][0] ) {

            } else if ( _id == sho[1] && _id != _owners[shard_id][1] ) {

                uint32_t shard_owner_id, shard_failover_id;
                get_owner_for_shard(shard_id, shard_owner_id, shard_failover_id);
                if ( _id != _owners[shard_id][0] ) { // we do not  have replica
                    message rm { message::get_replica, shard_id, _id, 0, 0 };
                    auto const node_id = uint32_t(-1) != shard_owner_id ? shard_owner_id: shard_failover_id;

                    std::cout << "request replica shard[" << shard_id << "]: node[" << _id << "] <- from node[" << node_id << "]" << std::endl;

                    _waiting_replicas.insert(shard_id);
                    if (not s_node_channels[node_id].enqueue( rm ) ) {
                        buffer.push_back( { node_id, rm } );
                    }
                }
            } else if ( _id != sho[0] and _id != sho[1] ) { // we are now neither not owner nor failover
                if ( not _shards[shard_id].empty() ) {
                    std::cout << "node[" << _id << "] clear shard[" << shard_id << "]" << std::endl;
                    _shards[shard_id].clear();
                }
            }

            _owners[shard_id] = sho;
        }

    }

    void thread_function() override
    {
        {
            pthread_t thread = pthread_self();
            char buffer[64];
            snprintf(buffer, 63, "node_%d", _id);
            pthread_setname_np(thread, buffer);
        }

        for ( message msg; s_node_channels[_id].dequeue(msg); );

        make_routes();
        s_states[_id] = 1;

        for (uint32_t shard_id = 0; shard_id < SHARDS; ++shard_id ) {
            uint32_t owner_id, failover_id;
            get_owner_for_shard(shard_id, owner_id, failover_id);
            if ( owner_id == _id or failover_id == _id) {
                message rm { message::get_replica, shard_id, _id, 0, 0 };
                auto const node_id = owner_id == _id ? failover_id: owner_id;
                if ( uint32_t(-1) != node_id ) {
                    _waiting_replicas.insert(shard_id);
                    if (not s_node_channels[node_id].enqueue( rm ) ) {
                        buffer.push_back( { node_id, rm } );
                    }
                }
            }
        }

        for ( ; _proceed; ) {
            bool recvd = false;
            message msg {};
            for ( ; s_node_channels[_id].dequeue(msg); ) {

                recvd = true;
                switch (msg._cmd) {
                    case message::nop:
                        break;

                    case message::get: {
                        message resp = msg;
                        auto where = _shards[msg._shard].find(msg._key);
                        if ( _shards[msg._shard].end() != where ) {
                            resp._data = where->second;
                        } else {
                            resp._cmd = message::nop;
                        }
                        while (not s_client_channels[msg._back_queue_id].enqueue(resp)) usleep(1000);
                    }
                    break;

                    case message::put: {
                        _shards[msg._shard][msg._key] = msg._data;
                        if ( 0xffffffffU != msg._back_queue_id ) { // is not replicated data
                            send_to_failover_node(msg);
                            send_to_replica_node(msg);
                        }
                    }
                    break;


                    case message::del: {
                        _shards[_id].erase(msg._key);
                        if ( 0xffffffffU != msg._back_queue_id ) { // is not replicated data
                            send_to_failover_node(msg);
                            send_to_replica_node(msg);
                        }
                    }
                    break;

                    case message::get_replica: {
                        auto & r = replications[msg._shard];
                        r._last_sent = 0;
                        r._node_id = msg._back_queue_id;
                        r._sent = 0;
                        r._shard_id = msg._shard;
                        _pending_replicas.push_back(r);
                    }
                    break;

                    case message::eof: {
                        _waiting_replicas.erase(msg._shard);
                        std::cout << "node[" << _id << "] shard [" << msg._shard << "] receive EOF, replicated count = " << msg._key << std::endl;
                    }
                    break;

                    case message::cluster_changed: {
                        on_cluster_changed();
                    }
                    break;

                    default: ;
                }

            }

            if ( not recvd ) {
                if ( not buffer.empty() ) {
                    send_buffered();
                } else if ( _pending_replicas.empty() || 0 == replicate() ) {
                    sched_yield();
                }
            }
        }

        std::cout << "node " << _id << " ended" << std::endl;
    }

    void stop ()
    {
        if ( _proceed ) {
            _proceed = 0;
            s_node_channels[_id].enqueue( { message::nop, 0, 0, 0, 0} );
            wait();
        }
    }

    void clear (void)
    {
        for ( auto & i : _owners ) i.clear();
        for ( auto & s : _shards ) s.clear();
        for ( auto & r : replications ) r = {};
        
        _pending_replicas.clear();
        buffer.clear();
    }


    uint32_t _id;
    volatile uint32_t _proceed = 1;

    ids_t _owners[SHARDS];
    
    storage_t _shards[SHARDS];

    standby replications[SHARDS];

    std::list<standby> _pending_replicas;

    std::set<uint32_t> _waiting_replicas;

    std::list<std::pair<uint32_t, message>> buffer;
};


int put (uint32_t key, uint32_t value, std::vector<pairuu_t> owners[])
{
    uint32_t shard = hash(&key, sizeof(key)) % SHARDS;
    auto const & own = owners[shard];
    for ( auto o : own ) {
        if ( s_states[o.second] ) {
            send_message_to_node(o.second, { message::put, shard, 0, key, value });
            return 0;
        }
    }
    return -1;
}

int get (uint32_t key, uint32_t & value, std::vector<pairuu_t> owners[])
{
    uint32_t shard = hash(&key, sizeof(key)) % SHARDS;
    auto const & own = owners[shard];
    for ( auto o : own ) {
        if ( s_states[o.second] ) {
            if ( send_message_to_node(o.second, { message::get, shard, 0, key, 0 }) ) {
                message resp;
                while ( not s_client_channels[0].dequeue(resp) ) ;
                if ( message::get == resp._cmd ) {
                    value = resp._data;
                    return 0;
                } else {
                    return -1;
                }
            }
        }
    }
    return -1;
}

uint32_t values[DATA_COUNT];

int main (void)
{
    for ( auto & q : s_node_channels ) q.create(2047);
    for ( auto & q : s_client_channels ) q.create(1023);

    uint32_t id = 0;
    node nodes[NODES];
    for ( auto & n : nodes ) {
        n._id = id++;
        n.start();
        while ( not s_states[n._id] ) sched_yield();
    }

    usleep(100000);

    std::vector<pairuu_t> owners[SHARDS];
    {
        std::cout << "\nowners\n";
        ids_t nodes = {0, 1, 2, 3, 4};
        for (uint32_t k = 0; k < SHARDS; ++k ) {
            owners[k] = get_RVH_table_for_shard_id(k, nodes);
            
            std::cout << "shard " << k << " -> ";
            for ( size_t j = 0; j < owners[k].size(); ++j )
                std::cout << owners[k][j].second;

            std::cout << std::endl;
        }
    }

    for ( uint32_t k = 0; k < DATA_COUNT; ++k ) {
        put(k, 11 + k, owners);
    }

    for ( uint32_t k = 0; k < DATA_COUNT; ++k ) {
        uint32_t value;
        get(k, value, owners);
        if ( k + 11 != value ) {
            std::cout << "error @ " << k << std::endl;
        }
    }

    size_t cnt = 0;
    for ( uint32_t z = 0; z < NODES; ++z ) {
        for ( uint32_t y = 0; y < SHARDS; ++y ) {
            std::cout << "node[" << z << "] shard[" << y << "] count " << nodes[z]._shards[y].size() << std::endl;
            cnt += nodes[z]._shards[y].size();
        }
    }
    std::cout << "total records: " << cnt << std::endl;

    std::cout << "stop node 2" << std::endl;

    s_states[2] = 0;

    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) {
            s_node_channels[n].enqueue( { message::cluster_changed, 0, 0, 0, 0 } );
            usleep(10000);
        }
    }

    nodes[2].stop();
    nodes[2].clear();

    std::cout << "\n\n**********************\nget data without node 2" << std::endl;

    for ( uint32_t k = 0; k < DATA_COUNT; ++k ) {
        uint32_t value;
        get(k, value, owners);
        if ( k + 11 != value ) {
            std::cout << "error @ " << k << std::endl;
        }
    }

    std::cout << "\n\n**********************\nwait nodes synced-up" << std::endl;
    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) for ( ; 0 != nodes[n]._waiting_replicas.size(); ) usleep(10000);
    }

//////////

     s_states[0] = 0;

    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) {
            s_node_channels[n].enqueue( { message::cluster_changed, 0, 0, 0, 0 } );
            usleep(10000);
        }
    }

    nodes[0].stop();
    nodes[0].clear();

    std::cout << "\n\n**********************\nget data without node 2 and node 0" << std::endl;

    for ( uint32_t k = 0; k < DATA_COUNT; ++k ) {
        uint32_t value;
        get(k, value, owners);
        if ( k + 11 != value ) {
            std::cout << "error @ " << k << std::endl;
        }
    }

    std::cout << "\n\n**********************\nwait nodes synced-up" << std::endl;
    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) for ( ; 0 != nodes[n]._waiting_replicas.size(); ) usleep(10000);
    }

//////////

    std::cout << "\n\n**********************\nsynced node 2" << std::endl;
    cnt = 0;
    for ( uint32_t z = 0; z < NODES; ++z ) {
        for ( uint32_t y = 0; y < SHARDS; ++y ) {
            std::cout << "node[" << z << "] shard[" << y << "] count " << nodes[z]._shards[y].size() << std::endl;
            cnt += nodes[z]._shards[y].size();
        }
    }
    std::cout << "total records: " << cnt << std::endl;

// /////////////


    std::cout << "\n\n**********************\nrestart node 2" << std::endl;

    nodes[2]._proceed = 1;
    nodes[2].start();
    for ( ; not s_states[2]; ) usleep(1000);

    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) {
            s_node_channels[n].enqueue( { message::cluster_changed, 0, 0, 0, 0 } );
            usleep(10000);
        }
    }

    std::cout << "\n\n**********************\nwait nodes synced-up" << std::endl;
    for ( int n = 0; n < NODES; ++n ) {
        if ( s_states[n] ) for ( ; 0 != nodes[n]._waiting_replicas.size(); ) usleep(10000);
    }

//////////

    std::cout << "\n\n**********************\nsynced node 2" << std::endl;
    cnt = 0;
    for ( uint32_t z = 0; z < NODES; ++z ) {
        for ( uint32_t y = 0; y < SHARDS; ++y ) {
            std::cout << "node[" << z << "] shard[" << y << "] count " << nodes[z]._shards[y].size() << std::endl;
            cnt += nodes[z]._shards[y].size();
        }
    }
    std::cout << "total records: " << cnt << std::endl;

    for ( auto & n : nodes ) n.stop();

    return 0;
}

uint32_t hash (const void* key_, size_t len, uint32_t seed)
{
    auto key = (uint8_t const *)key_;
    uint32_t h = seed;
    if (len > 3) {
        const uint32_t* key_x4 = (const uint32_t*) key;
        size_t i = len >> 2;
        do {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = (h * 5) + 0xe6546b64;
        } while (--i);
        key = (const uint8_t*) key_x4;
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

std::vector<pairuu_t> get_RVH_table_for_shard_id (uint32_t key, ids_t const & nodes)
{
    std::vector<pairuu_t> table;
    auto count = nodes.size();
    for ( uint32_t i = 0; i < count; ++i ) {
        auto con = (uint64_t(nodes[i]) << 32) | uint64_t(key);
        uint32_t h2 = hash( &con, sizeof(con) );
        table.push_back( { h2, nodes[i] } );
    }
    std::sort(table.begin(), table.end(), [] (pairuu_t const & a, pairuu_t const & b) -> bool { return a.first < b.first; } );
    return table;
}

void get_owners_table_for_shard_id (uint32_t key, ids_t const & nodes, ids_t & owners)
{
    owners.clear();
    std::vector<pairuu_t> table;

    auto const count = nodes.size();
    owners.reserve(count);
    table.reserve(count);

    for ( uint32_t i = 0; i < count; ++i ) {
        auto con = (uint64_t(nodes[i]) << 32) | uint64_t(key);
        uint32_t h2 = hash( &con, sizeof(con) );
        table.push_back( { h2, nodes[i] } );
    }

    std::sort(table.begin(), table.end(), [] (pairuu_t const & a, pairuu_t const & b) -> bool { return a.first < b.first; } );

    for ( uint32_t i = 0; i < count; ++i ) {
        owners.push_back( table[i].second );
    }
}
