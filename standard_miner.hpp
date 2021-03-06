//
// Standard miner. Relays and mines on longest chain it has seen.
//
#ifndef STANDARD_MINER_H
#define STANDARD_MINER_H

#include <boost/function.hpp>
#include <list>
#include <vector>
#include <set>
#include <utility>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>

class Miner;
class PeerInfo {
public:
    PeerInfo(Miner* _peer, int _chain_tip, double _latency) :
        peer(_peer), chain_tip(_chain_tip), latency(_latency) { }

    Miner* peer;
    int chain_tip;
    double latency;
};

using namespace boost::multi_index;

struct Record {
  uint64_t id;
  int      fee;

  Record(uint64_t id, int fee):id(id),fee(fee){}

  bool operator<  (const Record& a) const{return fee<a.fee;}
  bool operator== (const Record& a) const{return id==a.id;}
};

struct Block {
    std::vector<Record> txn;
};

typedef multi_index_container <
  Record,
  indexed_by<
    hashed_unique< member<Record, uint64_t, &Record::id> >,
    ordered_non_unique< member<Record, int, &Record::fee> >
  >
> Mempool;

class Miner {
public:
    // public
    std::shared_ptr<std::vector<Block>> blocks;
    Mempool mem_pool;
    u_int64_t reward;
    u_int64_t balance;

    // Return a random double in the range passed.
    typedef boost::function<double(double, double)> JitterFunction;

    Miner(double _hash_fraction, double _block_latency, JitterFunction _func) :
        hash_fraction(_hash_fraction), block_latency(_block_latency), jitter_func(_func) {
        best_chain = std::make_shared<std::vector<int>>();
        blocks = std::make_shared<std::vector<Block>>();
        reward = 0;
        balance= 0;
    }

    void AddPeer(Miner* peer, double latency) {
        peers.push_back(PeerInfo(peer, -1, latency));
    }

    virtual void FindBlock(CScheduler& s, int blockNumber) {
        // Extend the chain:
        auto chain_copy = std::make_shared<std::vector<int>>(best_chain->begin(),
                                                             best_chain->end());
        chain_copy->push_back(blockNumber);
        best_chain = chain_copy;
        reward++;

        // Transactions in block
        auto blocks_copy = std::make_shared<std::vector<Block>>(blocks->begin(),
                                                                blocks->end());
        Mempool::nth_index<1>::type &fee_index = mem_pool.get<1>();
        Block tmp_block;
        int i = 0;
        for (auto it = fee_index.rbegin(); it != fee_index.rend(); it++) {
            // std::cout << "[" << it->id << ",fee/" << it->fee << "] ";
            tmp_block.txn.push_back(Record{it->id, it->fee});
            if (i >= 2000) break; // max 3000 transactions in block
            i++;
        }
        blocks_copy->push_back(tmp_block);
        blocks = blocks_copy;

#ifdef TRACE
        // std::cout << "Miner " << hash_fraction << " found block at simulation time "
        //           << s.getSimTime() << "\n";
#endif
        RelayChain(this, s, chain_copy, blocks_copy, tmp_block, block_latency);
    }

    virtual void ConsiderChain(Miner* from, CScheduler& s,
                               std::shared_ptr<std::vector<int>> chain,
                               std::shared_ptr<std::vector<Block>> blcks,
                               Block b, double latency) {
        if (chain->size() > best_chain->size()) {
#ifdef TRACE
            // std::cout << "Miner " << hash_fraction
            //           << " relaying chain at simulation time " << s.getSimTime() << "\n";
#endif
            best_chain = chain;
            blocks = blcks;
            // TODO change local mempool
            Mempool::nth_index<0>::type &id_index = mem_pool.get<0>();
            for (auto &elem: b.txn) {
                id_index.erase(elem.id);
            }
            RelayChain(from, s, chain, blcks, b, latency);
        }
    }

    virtual void RelayChain(Miner* from, CScheduler& s,
                            std::shared_ptr<std::vector<int>> chain,
                            std::shared_ptr<std::vector<Block>> blcks,
                            Block b, double latency) {
        for (auto&& peer : peers) {
            if (peer.chain_tip == chain->back()) continue; // Already relayed to this peer
            peer.chain_tip = chain->back();
            if (peer.peer == from) continue; // don't relay to peer that just sent it!

            double jitter = 0;
            if (peer.latency > 0) jitter =
                jitter_func(-peer.latency/1000., peer.latency/1000.);
            double tPeer = s.getSimTime() + peer.latency + jitter + latency;
            auto f = boost::bind(&Miner::ConsiderChain, peer.peer, from, boost::ref(s),
                                 chain, blcks, b, block_latency);
            s.schedule(f, tPeer);
        }
    }

    virtual void ResetChain() {
        best_chain->clear();
    }

    const double GetHashFraction() const { return hash_fraction; }
    std::vector<int> GetBestChain() const { return *best_chain; }

protected:
    double hash_fraction; // This miner has hash_fraction of hash rate
    // This miner produces blocks that take block_latency seconds to relay/validate
    double block_latency;
    JitterFunction jitter_func;

    std::shared_ptr<std::vector<int>> best_chain;
    std::list<PeerInfo> peers;
};

#endif /* STANDARD_MINER_H */
