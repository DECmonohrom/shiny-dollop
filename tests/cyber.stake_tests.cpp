#include "golos_tester.hpp"
#include "cyber.token_test_api.hpp"
#include "cyber.stake_test_api.hpp"
#include "contracts.hpp"
#include "../cyber.stake/include/cyber.stake/config.hpp"
#include "../cyber.govern/include/cyber.govern/config.hpp"
#include "../cyber.bios/include/cyber.bios/config.hpp"

namespace cfg = cyber::config;
using eosio::chain::config::stake_account_name;
using eosio::chain::config::govern_account_name;
using namespace eosio::testing;
using namespace eosio::chain;
using namespace fc; 
static const auto _token = cfg::system_token;
const auto sys_priv_key = eosio::testing::base_tester::get_private_key(config::system_account_name, name{config::active_name}.to_string());

class cyber_stake_tester : public golos_tester {
protected:
    cyber_token_api token;
    cyber_stake_api stake;

    time_point_sec head_block_time() { return time_point_sec(control->head_block_time().time_since_epoch().to_seconds()); };
    
    account_name user_name(size_t n) {
        string ret_str("user");
        while (n) {
            constexpr auto q = 'z' - 'a' + 1;
            ret_str += std::string(1, 'a' + n % q);
            n /= q;
        }
        return string_to_name(ret_str.c_str()); 
    }; 
 
public: 
    cyber_stake_tester()
        : golos_tester(stake_account_name, false)
        , token({this, cfg::token_name, cfg::system_token})
        , stake({this, _code})
    { 
        create_accounts({_issuer, _alice, _bob, _carol,
            cfg::token_name});
        produce_block();
        install_contract(_code, contracts::stake_wasm(), contracts::stake_abi(), true, &sys_priv_key);
        install_contract(cfg::token_name, contracts::token_wasm(), contracts::token_abi());
        
    }
    static const std::vector<symbol_code> res_purposes;
    
    const account_name _issuer = N(cyber.issuer);
    const account_name _alice = N(alice);
    const account_name _bob = N(bob);
    const account_name _carol = N(carol);
    
    void issuer_delegate_authority() {
        delegate_authority(_issuer, {_code}, cfg::token_name, N(retire), cfg::amerce_name);
        delegate_authority(_issuer, {_code}, cfg::token_name, N(issue), cfg::reward_name);
        produce_block();
        delegate_authority(_issuer, {_code}, cfg::token_name, N(transfer), cfg::reward_name);
    }
    
    void deploy_sys_contracts() {

        std::vector<uint8_t> max_proxies = {30, 10, 3, 1};
        BOOST_TEST_MESSAGE("--- creating token and stake"); 
        BOOST_CHECK_EQUAL(success(), token.create(_issuer, asset(1000000, token._symbol)));
        BOOST_CHECK_EQUAL(success(), stake.create(_issuer, token._symbol, res_purposes, 
            max_proxies, cfg::balances_update_window, 7 * 24 * 60 * 60, 52));
            
        BOOST_TEST_MESSAGE("--- installing governance contract");
        install_contract(govern_account_name, contracts::govern_wasm(), contracts::govern_abi(), true, &sys_priv_key);
        BOOST_TEST_MESSAGE("--- installing bios contract");
        //sys token and stake must already exist at this moment
        install_contract(config::system_account_name, contracts::bios_wasm(), contracts::bios_abi());
        produce_block();
        BOOST_TEST_MESSAGE("    ...done"); 
    }

    struct errors: contract_error_messages {
        string incorrect_proxy_levels(uint8_t g, uint8_t a) {
            return amsg("incorrect proxy levels: grantor " + std::to_string(g) + ", agent " + std::to_string(a));
        };
    } err;
};

const std::vector<symbol_code> cyber_stake_tester::res_purposes = {to_code("CPU"), to_code("NET"), to_code("RAM")};

BOOST_AUTO_TEST_SUITE(cyber_stake_tests)
BOOST_FIXTURE_TEST_CASE(basic_tests, cyber_stake_tester) try {
    BOOST_TEST_MESSAGE("Basic stake tests");
    auto purpose_str = "CPU";
    auto purpose = to_code(purpose_str);
    issuer_delegate_authority();

    std::vector<uint8_t> max_proxies = {30, 10, 3, 1};
    int64_t frame_length = 30;
    asset stake_a(100, token._symbol);
    asset stake_a2(400, token._symbol);
    asset stake_b(10, token._symbol);
    asset stake_c(1000, token._symbol);
    double pct_b = 0.5;
    double pct_c = 0.2;
    double amerced_c = 0.5;
    int16_t fee_c = 5555; 
    asset supply = stake_a + stake_a2 + stake_b + stake_c;
    BOOST_CHECK_EQUAL(success(), token.create(_issuer, asset(1000000, token._symbol)));
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _alice, stake_a + stake_a2, ""));
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _bob,   stake_b, ""));
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _carol, stake_c, ""));
 
    BOOST_CHECK_EQUAL(success(), stake.create(_issuer, token._symbol, {purpose}, max_proxies, frame_length, 7 * 24 * 60 * 60, 52));

    BOOST_TEST_MESSAGE("--- alice stakes " << stake_a); 
    BOOST_CHECK_EQUAL(success(), token.transfer(_alice, _code, stake_a, purpose_str));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_carol, token._symbol.to_symbol_code(), purpose, 0));
    BOOST_CHECK_EQUAL(success(), stake.setgrntterms(_bob, _carol, token._symbol.to_symbol_code(), purpose, pct_c * cfg::_100percent, 0));
    BOOST_CHECK_EQUAL(err.incorrect_proxy_levels(4, 4), 
        stake.setgrntterms(_alice, _bob, token._symbol.to_symbol_code(), purpose, pct_b * cfg::_100percent));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_bob, token._symbol.to_symbol_code(), purpose, 1));
  
    BOOST_CHECK_EQUAL(success(), stake.setgrntterms(_alice, _bob, token._symbol.to_symbol_code(), purpose, pct_b * cfg::_100percent));

    produce_block();
    auto t = head_block_time();
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, max_proxies.size(), t, stake_a.get_amount(),
        0, stake_a.get_amount(), stake_a.get_amount()));
    
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t));

    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 0, t));

    BOOST_CHECK_EQUAL(success(), token.transfer(_bob, _code, stake_b, purpose_str));
    BOOST_CHECK_EQUAL(success(), token.transfer(_alice, _code, stake_a2, purpose_str));
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, _code, stake_c, purpose_str));
    int64_t total_staked = stake_a.get_amount() + stake_a2.get_amount() + stake_b.get_amount() + stake_c.get_amount();
    BOOST_CHECK_EQUAL(stake.get_stats(token._symbol, purpose), stake.make_stats(token._symbol, purpose, total_staked));
    produce_block();
    double own_a = stake_a.get_amount() + stake_a2.get_amount();
    double own_b = stake_b.get_amount();
    double own_c = stake_c.get_amount();
    double balance_a = stake_a.get_amount() + ((1.0 - pct_b) * stake_a2.get_amount());
    double balance_b = ((stake_a2.get_amount() * pct_b) + stake_b.get_amount()) * (1.0 - pct_c);
    double balance_c = ((stake_a2.get_amount() * pct_b) + stake_b.get_amount()) * pct_c + stake_c.get_amount();
    double total_a = stake_a.get_amount() + stake_a2.get_amount();
    double total_b = stake_b.get_amount() + (total_a - balance_a);
    double total_c = balance_c;
 
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, max_proxies.size(), t, balance_a, total_a - balance_a, total_a, own_a));
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t, balance_b, total_b - balance_b, total_b, own_b));
    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 0, t, balance_c, total_c - balance_c, total_c, own_c));
    
    BOOST_CHECK_EQUAL(token.get_stats()["supply"], supply.to_string());
    auto amerced = asset(balance_c * amerced_c, token._symbol);
    BOOST_CHECK_EQUAL(success(), stake.amerce(_issuer, _carol, amerced, purpose));
    supply -= amerced;
    total_staked -= amerced.get_amount();
    BOOST_CHECK_EQUAL(stake.get_stats(token._symbol, purpose), stake.make_stats(token._symbol, purpose, total_staked));
    
    BOOST_CHECK_EQUAL(token.get_stats()["supply"], supply.to_string());
    produce_block();
    
    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 0, t, balance_c * (1.0 - amerced_c), total_c - balance_c, total_c, own_c));
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_alice, token._symbol.to_symbol_code(), purpose));
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t, balance_b, total_b - balance_b, total_b, own_b));
    
    auto t1 = time_point_sec(t.sec_since_epoch() + frame_length);
    auto blocks_num = ((t1.sec_since_epoch() - head_block_time().sec_since_epoch()) * 1000) / cfg::block_interval_ms - 1;
    BOOST_TEST_MESSAGE("--- produce " << blocks_num << " blocks");
    produce_blocks(blocks_num);
    double bob_lost = (total_b - balance_b) * amerced_c;
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_alice, token._symbol.to_symbol_code(), purpose));
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t1, balance_b, (total_b - balance_b) - bob_lost, total_b, own_b));
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, max_proxies.size(), t1, balance_a, 
            (total_a - balance_a) - (bob_lost * (1.0 - own_b / total_b)), 
            total_a, own_a));
    produce_block();
    BOOST_CHECK_EQUAL(success(), stake.reward(_issuer, _carol, amerced, purpose));
    supply += amerced;
    total_staked += amerced.get_amount();
    BOOST_CHECK_EQUAL(stake.get_stats(token._symbol, purpose), stake.make_stats(token._symbol, purpose, total_staked));
    BOOST_CHECK_EQUAL(token.get_stats()["supply"], supply.to_string());
     
    t1 = time_point_sec(t1.sec_since_epoch() + frame_length);
    blocks_num = ((t1.sec_since_epoch() - head_block_time().sec_since_epoch()) * 1000) / cfg::block_interval_ms - 1;
    BOOST_TEST_MESSAGE("--- produce " << blocks_num << " blocks");
    produce_blocks(blocks_num);
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_alice, token._symbol.to_symbol_code(), purpose));
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, max_proxies.size(), t1, balance_a, total_a - balance_a, total_a, own_a));
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t1, balance_b, total_b - balance_b, total_b, own_b));
    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 0, t1, balance_c, 0, total_c, own_c));
    BOOST_TEST_MESSAGE("--- carol sets fee");
    BOOST_CHECK_EQUAL(success(), stake.setproxyfee(_carol, token._symbol.to_symbol_code(), purpose, fee_c));
    t1 = time_point_sec(t1.sec_since_epoch() + frame_length);
    blocks_num = ((t1.sec_since_epoch() - head_block_time().sec_since_epoch()) * 1000) / cfg::block_interval_ms - 1;
    BOOST_TEST_MESSAGE("--- produce " << blocks_num << " blocks");
    produce_blocks(blocks_num);

    BOOST_CHECK_EQUAL(success(), stake.updatefunds(_bob, token._symbol.to_symbol_code(), purpose));
    
    balance_c -= (total_b - balance_b);
    total_c = balance_c;
    balance_b = total_b;
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t1, balance_b, total_b - balance_b, total_b, own_b));
    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 0, t1, balance_c, 0, total_c, own_c, fee_c));

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(increase_proxy_level_test, cyber_stake_tester) try {
    BOOST_TEST_MESSAGE("Increase proxy level test");
    auto purpose_str = "CPU";
    auto purpose = to_code(purpose_str);
    issuer_delegate_authority();
    
    std::vector<uint8_t> max_proxies = {30, 10, 3, 1};
    int64_t frame_length = 30;
    double pct_a = 0.4;
    double pct_b = 0.5;
    double pct_c = 0.2;
    asset staked(10000000, token._symbol);
    BOOST_CHECK_EQUAL(success(), token.create(_issuer, asset(1000000000, token._symbol)));
    BOOST_CHECK_EQUAL(success(), stake.create(_issuer, token._symbol, {purpose}, max_proxies, frame_length, 7 * 24 * 60 * 60, 52));

    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _carol, staked, ""));
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _alice, staked, ""));

    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_alice, token._symbol.to_symbol_code(), purpose, 0));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_bob  , token._symbol.to_symbol_code(), purpose, 1));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_carol, token._symbol.to_symbol_code(), purpose, 2));
    BOOST_CHECK_EQUAL(success(), stake.setgrntterms(_bob, _alice, token._symbol.to_symbol_code(), purpose, pct_b * cfg::_100percent));
    BOOST_CHECK_EQUAL(success(), stake.setgrntterms(_carol, _bob, token._symbol.to_symbol_code(), purpose, pct_c * cfg::_100percent));
    BOOST_TEST_MESSAGE("--- carol stakes " << staked);
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, _code, staked, purpose_str));
    BOOST_TEST_MESSAGE("--- alice stakes " << staked);
    BOOST_CHECK_EQUAL(success(), token.transfer(_alice, _code, staked, purpose_str));
    produce_block();
    auto t = head_block_time();
    auto balance_a = ((pct_b * pct_c) + 1.0) * staked.get_amount();
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, 0, t, balance_a, 0, balance_a, staked.get_amount()));
    BOOST_TEST_MESSAGE("--- alice changes level");
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_alice, token._symbol.to_symbol_code(), purpose, 4));
    produce_block();
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, 4, t, balance_a, 0, balance_a, staked.get_amount()));
    
    BOOST_TEST_MESSAGE("--- alice delegates");
    BOOST_CHECK_EQUAL(success(), stake.delegate(_alice, _carol, asset(staked.get_amount() * pct_a, token._symbol), purpose));
    produce_block();
    t = head_block_time();
    balance_a = (1.0 - pct_a) * staked.get_amount();
    auto proxied_a = staked.get_amount() - balance_a;
    BOOST_CHECK_EQUAL(stake.get_agent(_alice, token._symbol, purpose),
        stake.make_agent(_alice, token._symbol, purpose, 4, t, balance_a, proxied_a, staked.get_amount(), staked.get_amount()));
        
    auto total_c = proxied_a + staked.get_amount();
    auto balance_c = total_c * (1.0 - pct_c);
    auto proxied_c = total_c * pct_c;
    BOOST_CHECK_EQUAL(stake.get_agent(_carol, token._symbol, purpose),
        stake.make_agent(_carol, token._symbol, purpose, 2, t, balance_c, proxied_c, total_c, staked.get_amount()));

    auto total_b = proxied_c;
    auto balance_b = total_b;
    auto proxied_b = 0;
    BOOST_CHECK_EQUAL(stake.get_agent(_bob, token._symbol, purpose),
        stake.make_agent(_bob, token._symbol, purpose, 1, t, balance_b, proxied_b, balance_b, 0));
    BOOST_CHECK_EQUAL(success(), stake.recall(_alice, _carol, token._symbol.to_symbol_code(), purpose, cfg::_100percent));
    produce_block();

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(set_producers_test, cyber_stake_tester) try {
    BOOST_TEST_MESSAGE("Set producers test");
    deploy_sys_contracts();
    stake.register_candidate(_alice, token._symbol.to_symbol_code());
    auto blocks_num = (cfg::govern_update_window * 1000) / cfg::block_interval_ms;
    
    produce_blocks(blocks_num - 1);
    BOOST_CHECK_EQUAL(stake.get_producers(), stake.make_producers({_alice}));
    
    stake.register_candidate(_bob, token._symbol.to_symbol_code());
    produce_blocks(blocks_num);
    BOOST_CHECK_EQUAL(stake.get_producers(), stake.make_producers({_alice, _bob}));
    
    std::vector<account_name> producers;
    std::vector<account_name> crowd_of_bps;
    for (size_t u = 0; u < cfg::producers_num - 1; u++) {
        auto user = user_name(u);
        crowd_of_bps.emplace_back(user);
        create_accounts({user});
        stake.register_candidate(user, token._symbol.to_symbol_code());
        BOOST_CHECK_EQUAL(success(), token.issue(_issuer, user, asset(u + 1, token._symbol), ""));
        BOOST_CHECK_EQUAL(success(), token.transfer(user, _code, asset(u + 1, token._symbol), "CPU"));
    }
    
    auto crowd_and_alice = crowd_of_bps;
    crowd_and_alice.emplace_back(_alice);
    auto crowd_and_bob = crowd_of_bps;
    crowd_and_bob.emplace_back(_bob);
    
    produce_blocks(blocks_num);
    BOOST_CHECK_EQUAL(stake.get_producers(), stake.make_producers(crowd_and_bob));
    
    BOOST_CHECK_EQUAL(success(), token.issue(_issuer, _carol, asset(3, token._symbol), ""));
    BOOST_CHECK_EQUAL(success(), stake.setproxylvl(_carol, token._symbol.to_symbol_code(), to_code("CPU"), 1));
    
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, _code, asset(1, token._symbol), "CPU"));
    BOOST_CHECK_EQUAL(success(), stake.delegate(_carol, _alice, asset(1, token._symbol), to_code("CPU")));
    
    produce_blocks(blocks_num);
    BOOST_CHECK_EQUAL(stake.get_producers(), stake.make_producers(crowd_and_alice));
    
    BOOST_CHECK_EQUAL(success(), token.transfer(_carol, _code, asset(2, token._symbol), "CPU"));
    BOOST_CHECK_EQUAL(success(), stake.delegate(_carol, _bob, asset(2, token._symbol), to_code("CPU")));
    
    produce_blocks(blocks_num);
    BOOST_CHECK_EQUAL(stake.get_producers(), stake.make_producers(crowd_and_bob));

    
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(cyber_stake_tests_ext, * boost::unit_test::disabled()) 
BOOST_FIXTURE_TEST_CASE(recursive_update_test, cyber_stake_tester) try {
    BOOST_TEST_MESSAGE("Recursive update test");
    auto purpose_str = "CPU"; 
    auto purpose = to_code(purpose_str);
    issuer_delegate_authority();
    
    std::vector<uint8_t> max_proxies = {50, 20, 5, 1};
    int64_t frame_length = 30;
    asset staked(10000000, token._symbol);
    double amerced = 0.5;
    
    BOOST_CHECK_EQUAL(success(), token.create(_issuer, asset(1000000000, token._symbol)));
    BOOST_CHECK_EQUAL(success(), stake.create(_issuer, token._symbol, {purpose}, max_proxies, frame_length, 7 * 24 * 60 * 60, 52));

    uint8_t level = 0;
    size_t u = 0;
    account_name user;
    std::vector<account_name> prev_level_accs;
    std::vector<account_name> bp_accs;
    while (level <= max_proxies.size()) {
        std::vector<account_name> cur_level_accs;
        for (size_t i = 0; i < max_proxies[std::min(static_cast<size_t>(level), max_proxies.size() - 1)]; i++) {
            user = user_name(u++);
            cur_level_accs.emplace_back(user);
            create_accounts({user});
            BOOST_CHECK_EQUAL(success(), token.issue(_issuer, user, staked, ""));
            BOOST_CHECK_EQUAL(success(), stake.setproxylvl(user, token._symbol.to_symbol_code(), purpose, level));
            for (auto proxy : prev_level_accs) {
                int16_t pct = (1.0 / prev_level_accs.size()) * cfg::_100percent;
                BOOST_TEST_MESSAGE("--- setgrntterms pct: " << pct << " " << user << " -> " << proxy);
                BOOST_CHECK_EQUAL(success(), stake.setgrntterms(user, proxy, token._symbol.to_symbol_code(), purpose, pct));
            }
            BOOST_CHECK_EQUAL(success(), token.transfer(user, _code, staked, purpose_str));
            if(u % 20 == 0)
                produce_block();
        }
        prev_level_accs = cur_level_accs;
        if(!level)
            bp_accs = cur_level_accs;
        cur_level_accs.clear();
        ++level;
    }
    auto total_amount = staked.get_amount() * u; 
    auto bp_amount = total_amount / bp_accs.size();
    BOOST_TEST_MESSAGE("--- total_amount = " << total_amount);
    BOOST_TEST_MESSAGE("--- bp_amount = " << bp_amount);
    for (auto bp : bp_accs) {
        BOOST_CHECK_EQUAL(success(), stake.amerce(_issuer, bp, asset(bp_amount * amerced, token._symbol), purpose));
    }
    
    produce_block();
    auto t = head_block_time();
    BOOST_CHECK_EQUAL(stake.get_agent(user, token._symbol, purpose),
        stake.make_agent(user, token._symbol, purpose, max_proxies.size(), t, 0,
        staked.get_amount(), staked.get_amount(), staked.get_amount()));
    
    produce_blocks(frame_length * 1000 / cfg::block_interval_ms);
    BOOST_CHECK_EQUAL(success(), stake.updatefunds(user, token._symbol.to_symbol_code(), purpose));
    produce_block();
    t = head_block_time();
    BOOST_CHECK_EQUAL(stake.get_agent(user, token._symbol, purpose),
        stake.make_agent(user, token._symbol, purpose, max_proxies.size(), t, 0,
        staked.get_amount() * (1.0 - amerced), staked.get_amount(), staked.get_amount()));

    produce_block();
} FC_LOG_AND_RETHROW()
BOOST_AUTO_TEST_SUITE_END()

