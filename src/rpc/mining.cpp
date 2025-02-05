// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 FXTC developers
// Copyright (c) 2018-2020 The Veles Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <miner.h>
#include <net.h>
#include <policy/fees.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <rpc/mining.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <shutdown.h>
#include <txmempool.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbitsinfo.h>
#include <warnings.h>

// Dash
#include <governance/classes.h>
#include <masternode/payments.h>
#include <masternode/sync.h>
//

#include <memory>
#include <stdint.h>

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
static UniValue GetNetworkHashPS(int lookup, int height, int32_t nAlgo) {   // VELES: added int32_t nAlgo
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == nullptr || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval() + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    // FXTC BEGIN
    CBlockIndex* pbAlgo = pb;
    while (pbAlgo->pprev && nAlgo != (pbAlgo->nVersion & ALGO_VERSION_MASK)) pbAlgo = pbAlgo->pprev;
    CBlockIndex* pb0Algo = pb0;
    while (pb0Algo->pprev && nAlgo != (pb0Algo->nVersion & ALGO_VERSION_MASK)) pb0Algo = pb0Algo->pprev;
    workDiff = pbAlgo->nChainWorkAlgo - pb0Algo->nChainWorkAlgo;
    // FXTC END
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

static UniValue getnetworkhashps(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            RPCHelpMan{"getnetworkhashps",
                "\nReturns the estimated network hashes per second based on the last n blocks.\n"
                "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
                "Pass in [height] to estimate the network speed at the time when a certain block was found.\n",
                {
                    {"nblocks", RPCArg::Type::NUM, /* default */ "120", "The number of blocks, or -1 for blocks since last difficulty change."},
                    {"height", RPCArg::Type::NUM, /* default */ "-1", "To estimate at the time of the given height."},
                    {"algorithm", RPCArg::Type::STR, /* default */ "", "Filter work for selected algorithm.."},
                },
                RPCResult{
            "x             (numeric) Hashes per second estimated\n"
                },
                RPCExamples{
                    HelpExampleCli("getnetworkhashps", "")
            + HelpExampleRpc("getnetworkhashps", "")
                },
            }.ToString());

    LOCK(cs_main);
    return GetNetworkHashPS(!request.params[0].isNull() ? request.params[0].get_int() : 120, !request.params[1].isNull() ? request.params[1].get_int() : -1, !request.params[2].isNull() ? GetAlgoId(request.params[2].get_str()) : miningAlgo);
}

// VELES BEGIN
/* Returns last block mined by the given algo */
static const CBlockIndex *GetLastAlgoBlock(int32_t nAlgo) {
    CBlockIndex *pb = chainActive.Tip();

    // Look up last block mined by the current algo
    while ((pb->nVersion & ALGO_VERSION_MASK) != nAlgo && pb->pprev) {
        pb = pb->pprev;
    }

   return pb;
}

/**
 * Returns correct difficulty value for the current algo, fixes FCTC "bug"
 * where getmininginfo returns the difficulty of the last algo that has
 * been used to find last block, hence returning entirely different value
 * each time a new block has been found on a different algo, instead of
 * returning the difficulty of currently selected algo (eg. in the config).
 *
 * Since networkhashps always returns hashrate related to the currently
 * selected algo, difficulty shown should also be relevant to the current
 * algo, hence in Veles Core we consider this inconsistend information a bug,
 * hence the function below is designed to return last difficulty of algo
 * defined by nAlgo parameter.
 *
 */
static double GetAlgoDifficulty(int32_t nAlgo) {
   return (double)GetDifficulty(GetLastAlgoBlock(nAlgo));
}

/* Returns sum of rewards for blocks mined by this algo from last X blocks */
CAmount CountAlgoBlockRewards(int32_t nAlgo, int nBlocks) {
    CBlockIndex *pb = chainActive.Tip();
    CAmount nRewards = 0;

    // Look up last block mined by the current algo
    while (nBlocks && pb->pprev) {
        if ((pb->nVersion & ALGO_VERSION_MASK) == nAlgo)
            nRewards += GetBlockSubsidy(pb->nHeight, pb->GetBlockHeader(), Params().GetConsensus(), false);
        pb = pb->pprev;
        nBlocks--;
    }

   return nRewards;
}

/* Returns number of blocks mined by the given algo from last X blocks */
int CountAlgoBlocks(int32_t nAlgo, int nBlocks) {
    CBlockIndex *pb = chainActive.Tip();
    int nAlgoBlocks = 0;

    // Look up last block mined by the current algo
    while (nBlocks && pb->pprev) {
        if ((pb->nVersion & ALGO_VERSION_MASK) == nAlgo)
            nAlgoBlocks++;
        pb = pb->pprev;
        nBlocks--;
    }

   return nAlgoBlocks;
}
// VELES END

UniValue generateBlocks(std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript)
{
    static const int nInnerLoopCount = 0x10000;
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = chainActive.Height();
        nHeightEnd = nHeight+nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    while (nHeight < nHeightEnd && !ShutdownRequested())
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        }
        // FXTC BEGIN
        //while (nMaxTries > 0 && pblock->nNonce < nInnerLoopCount && !CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus())) {
        while (nMaxTries > 0 && pblock->nNonce < nInnerLoopCount && !CheckProofOfWork(pblock->GetPoWHash(), pblock->nBits, Params().GetConsensus())) {
        // FXTC END
            ++pblock->nNonce;
            --nMaxTries;
        }
        if (nMaxTries == 0) {
            break;
        }
        if (pblock->nNonce == nInnerLoopCount) {
            continue;
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        //mark script as important because it was used at least for one coinbase output if the script came from the wallet
        if (keepScript)
        {
            coinbaseScript->KeepScript();
        }
    }
    return blockHashes;
}

static UniValue generatetoaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            RPCHelpMan{"generatetoaddress",
                "\nMine blocks immediately to a specified address (before the RPC call returns)\n",
                {
                    {"nblocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated immediately."},
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the newly generated veles to."},
                    {"maxtries", RPCArg::Type::NUM, /* default */ "1000000", "How many iterations to try."},
                },
                RPCResult{
            "[ blockhashes ]     (array) hashes of blocks generated\n"
                },
                RPCExamples{
            "\nGenerate 11 blocks to myaddress\n"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\"")
            + "If you are running the veles core wallet, you can get a new address to send the newly generated veles to with:\n"
            + HelpExampleCli("getnewaddress", "")
                },
            }.ToString());

    int nGenerate = request.params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (!request.params[2].isNull()) {
        nMaxTries = request.params[2].get_int();
    }

    CTxDestination destination = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    std::shared_ptr<CReserveScript> coinbaseScript = std::make_shared<CReserveScript>();
    coinbaseScript->reserveScript = GetScriptForDestination(destination);

    return generateBlocks(coinbaseScript, nGenerate, nMaxTries, false);
}

static UniValue getmininginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {   // Veles: Added paremeter
        throw std::runtime_error(
            RPCHelpMan{"getmininginfo",
                "\nReturns a json object containing mining-related information.",
                // VELES BEGIN
                {
                    {"algorithm", RPCArg::Type::STR, GetAlgoName(miningAlgo) /* default */, "*EXPERIMENTAL* A name of the PoW algorithm used"}
                },
                // VELES END,
                RPCResult{
                    "{\n"
                    "  \"blocks\": nnn,             (numeric) The current block\n"
                    "  \"currentblockweight\": nnn, (numeric, optional) The block weight of the last assembled block (only present if a block was ever assembled)\n"
                    "  \"currentblocktx\": nnn,     (numeric, optional) The number of block transactions of the last assembled block (only present if a block was ever assembled)\n"
                    "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
                    "  \"algo\": \"...\"            (string) The current mining algo\n" // Veles
                    "  \"networkhashps\": nnn,      (numeric) The network hashes per second\n"
                    "  \"pooledtx\": n              (numeric) The size of the mempool\n"
                    "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
                    "  \"warnings\": \"...\"        (string) any network and blockchain warnings\n"
                    "}\n"
                },
                RPCExamples{
                    HelpExampleCli("getmininginfo", "")
                  + HelpExampleRpc("getmininginfo", "")
                },
            }.ToString());
    }

    LOCK(cs_main);

    // VELES BEGIN
    int32_t nPowAlgo = miningAlgo;
    if (!request.params[0].isNull())
        nPowAlgo = GetAlgoId(request.params[0].get_str());

    if (nPowAlgo == ALGO_NULL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown algorithm %s", request.params[0].get_str()));
    // VELES END

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks",           (int)chainActive.Height());
    if (BlockAssembler::m_last_block_weight) obj.pushKV("currentblockweight", *BlockAssembler::m_last_block_weight);
    if (BlockAssembler::m_last_block_num_txs) obj.pushKV("currentblocktx", *BlockAssembler::m_last_block_num_txs);
    // Veles edit
    obj.pushKV("difficulty",       (double)GetAlgoDifficulty(nPowAlgo));
    obj.pushKV("algo",             GetAlgoName(nPowAlgo));
    obj.pushKV("networkhashps",    GetNetworkHashPS(120, -1, nPowAlgo));
    //
    obj.pushKV("pooledtx",         (uint64_t)mempool.size());
    obj.pushKV("chain",            Params().NetworkIDString());
    obj.pushKV("warnings",         GetWarnings("statusbar"));

    return obj;
}

// VELES BEGIN
static UniValue gethalvinginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size()) {
        throw std::runtime_error(
            RPCHelpMan{"gethalvinginfo",
                "\nReturns a json object containing an information related to block reward halving. A halving epoch is time between\n"
                "the start and end of block subsidy halving interval, where maximum block reward is the same for all the blocks\n"
                "within the epoch. If not enough coins are mined during the epoch, the halving will not occur and the current epoch\n"
                "will repeat again (with the same interval and maximum block reward). When the halving eventually occurs, the minimal\n"
                "interval between halvings increases twofold.",
                {},
                RPCResult{
                    "{\n"
                    "  \"halvings_occured\": nnn,           (numeric) The number of successful halvings that has occured\n"
                    "  \"epochs_occured\": nnn,             (numeric) The number of halving epochs that has occured\n"
                    "  \"halving_interval\": nnn,           (numeric) Interval between the last halving and the next potential one\n"
                    "  \"blocks_to_next_epoch\": nnn,       (numeric) Number of blocks to be fund until the start of another halving epoch\n"
                    "  \"epoch_supply_target_reached\": xxx, (string) Ratio between theoretical and actual number of coins to be mined this halving period, see also description of 'supply_target_reached'.\n"
                    "  \"min_epoch_supply_to_halve\": xxx,   (string) Minimum ratio between theoretical and actual coin supply during halving period required for another halving to occur\n"
                    "  \"epochs\" : [                         (array) List of halving epochs that has already occured and the current epoch\n"
                    "     {\n"
                    "       \"epoch_name\": xxx,             (string) Unique name of the epoch\n"
                    "       \"started_by_halving\": xx,     (boolean) If true, the amount of block reward has been halved at the start of current epoch\n"
                    "       \"start_block\": nnn,           (numeric) Height of fist block in the halving epoch\n"
                    "       \"end_block\": nnn,             (numeric) Height of last block of the epoch\n"
                    "       \"max_block_reward\": nnn,      (numeric) Maximum possible number of new coins mined within a single block, the sum of PoW, Masternode and Dev fund reward.\n"
                    "       \"dynamic_rewards_boost\": xxx, (string|false) Percentage of increase in dynamic block rewards (within the max_block_reward limit) if coin supply released during the last epoch was less than " + std::to_string((int)(HALVING_MIN_BOOST_SUPPLY_TARGET * 100)) + "\% of the target\n"
                    "       \"start_supply\": nnn,          (numeric) Total number of coins in circulation before fist block of the epoch\n"
                    "       \"end_supply\": nnn,      (numeric|false) Total number of coins in circulation at the last block of the epoch \n"
                    "       \"supply_target\": nnn,         (numeric) Maximum number of coins that can theoretically be released to the circulation during the epoch\n"
                    "       \"supply_this_epoch\": nnn,     (numeric) Actual number of coins that were released to the circulation during the epoch\n"
                    "       \"supply_since_halving\": nnn,  (numeric) Actual number of coins that were released to the circulation since the last halving\n"
                    "       \"supply_target_reached\" xxx,   (string) Ratio between supply_target and supply_since_halving in percents\n"
                    "     },"
                    "     ...\n"
                    "   ]\n"
                    "}\n"
                },
                RPCExamples{
                    HelpExampleCli("gethalvinginfo", "")
                  + HelpExampleRpc("gethalvinginfo", "")
                },
            }.ToString()
        );
    }

    HalvingParameters *halvingParams = GetSubsidyHalvingParameters();
    std::vector<std::string> knownEpochs = { "COINSWAP", "BOOTSTRAP", "ALPHA" };
    std::string epochName;
    int nHalvings = 0;
    int nEpochsAfterHalving = 0;
    UniValue obj(UniValue::VOBJ);
    UniValue childObj(UniValue::VOBJ);
    UniValue childArr(UniValue::VARR);
    CAmount nEpochMaxSupply;
    CAmount nEpochRealSupply;
    CAmount nSupplySinceHalving = 0;

    //LOCK(cs_main);
    FlushStateToDisk();

    obj.pushKV("halvings_occured", halvingParams->nHalvingCount);
    obj.pushKV("epochs_occured", (int)halvingParams->epochs.size());
    obj.pushKV("halving_interval", halvingParams->nHalvingInterval);
    obj.pushKV("blocks_to_next_epoch", (uint64_t)halvingParams->epochs.back().nEndBlock - chainActive.Height());

    // list through mining epochs
    for (int i = 0; i < (int)halvingParams->epochs.size(); i++) {
        if (halvingParams->epochs[i].fIsSubsidyHalved) {
            nHalvings++;
            nEpochsAfterHalving = 0;
            nSupplySinceHalving = 0;
        } else {
            nEpochsAfterHalving++;
        }

        if (i < (int)knownEpochs.size()) {
            childObj.pushKV("epoch_name", knownEpochs[i]);
            nEpochsAfterHalving = 0;    // make sure first numbered epoch starts after special epochs
            nSupplySinceHalving = 0;    // and this counter starts from when halving coutner starts (block 50k)
        } else {
            epochName = "ALPHA_H" + std::to_string(nHalvings) + "_E" + std::to_string(nEpochsAfterHalving);
            childObj.pushKV("epoch_name", epochName);
        }

        nEpochMaxSupply = halvingParams->epochs[i].nMaxBlockSubsidy
            * (halvingParams->epochs[i].nEndBlock - halvingParams->epochs[i].nStartBlock + 1);
        nEpochRealSupply = (halvingParams->epochs[i].fHasEnded)
            ? halvingParams->epochs[i].nEndSupply - halvingParams->epochs[i].nStartSupply
            : CountBlockRewards(
                halvingParams->epochs[i].nStartBlock,
                chainActive.Height(),
                GetSubsidyHalvingParameters(chainActive.Height(), Params().GetConsensus())
                );
        nSupplySinceHalving += nEpochRealSupply;

        childObj.pushKV("started_by_halving", halvingParams->epochs[i].fIsSubsidyHalved);
        childObj.pushKV("start_block", halvingParams->epochs[i].nStartBlock);
        childObj.pushKV("end_block", halvingParams->epochs[i].nEndBlock);
        childObj.pushKV("max_block_reward", ValueFromAmount(halvingParams->epochs[i].nMaxBlockSubsidy));

        if (halvingParams->epochs[i].nDynamicRewardsBoostFactor > 0)
            childObj.pushKV("dynamic_rewards_boost", "+" + std::to_string((int)(halvingParams->epochs[i].nDynamicRewardsBoostFactor * 100)) + "\%");
        else
            childObj.pushKV("dynamic_rewards_boost", false);

        childObj.pushKV("start_supply", ValueFromAmount(halvingParams->epochs[i].nStartSupply));
        childObj.pushKV("end_supply", (halvingParams->epochs[i].fHasEnded)
            ? ValueFromAmount(halvingParams->epochs[i].nEndSupply)
            : false);

        childObj.pushKV("supply_target", ValueFromAmount(nEpochMaxSupply));
        childObj.pushKV("supply_this_epoch", ValueFromAmount(nEpochRealSupply));
        childObj.pushKV("supply_since_halving", ValueFromAmount(nSupplySinceHalving));
        childObj.pushKV("supply_target_reached", std::to_string((int)floor((double)nSupplySinceHalving
            / ((double)nEpochMaxSupply) * 100)) + "\%");

        childArr.push_back(childObj);
    }

    obj.pushKV("epoch_supply_target_reached", std::to_string((int)floor((double)nSupplySinceHalving
            / ((double)nEpochMaxSupply) * 100)) + "\%");
    obj.pushKV("min_epoch_supply_to_halve", std::to_string((int)(HALVING_MIN_SUPPLY_TARGET * 100)) + "\%");
    obj.pushKV("epochs", childArr);

    return obj;
}

static UniValue getmultialgoinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size())
        throw std::runtime_error(
            RPCHelpMan{"getmultialgoinfo",
            "\nReturns a json object containing information related to multi-algo mining.",
            {},
            RPCResult{
                "[\n"
                "  {\n"
                "    \"algo\": xxxxxx                  (string)  PoW algorithm algorithm name.\n"
                "    \"difficulty\": xxx.xxxxx,        (numeric) The current difficulty\n"
                "    \"hashrate\": xxx.xxxxx,          (numeric) The network hashes per second\n"
                "    \"last_block_index\" : xx         (numeric) Number of the last block generated by the algorithm\n"
                "  },\n"
                "   ..."
                "]\n"
                //"\nSupported algorithms:\n"
                //"  sha256d, scrypt, lyra2z, x11, x16, nist5.\n"
            },
            RPCExamples{
                HelpExampleCli("getmultialgoinfo", "")
              + HelpExampleRpc("getmultialgoinfo", "")
            },
        }.ToString());

    UniValue arr(UniValue::VARR);
    UniValue algoObj(UniValue::VOBJ);
    std::vector<int32_t> algos = {ALGO_SHA256D, ALGO_SCRYPT, ALGO_LYRA2Z, ALGO_X11, ALGO_X16R, ALGO_NIST5};

    for(int i = 0; i < (int)algos.size(); i++) {
        algoObj.pushKV("algo", GetAlgoName(algos[i]));
        algoObj.pushKV("difficulty", (double)GetAlgoDifficulty(algos[i]));
        algoObj.pushKV("hashrate",   GetNetworkHashPS(120, -1, algos[i]));
        algoObj.pushKV("last_block_index", (int)GetLastAlgoBlock(algos[i])->nHeight);
        arr.push_back(algoObj);
    }

    return arr;
}

static UniValue getminingstats(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size())
        throw std::runtime_error(
            RPCHelpMan{"getminingstats",
            "\n*EXPERIMENTAL* Returns a json object containing mining statistics for each algo.",
            {},
            RPCResult{
                "[\n"
                "  {\n"
                "    \"algo\": xxxxxx,                    (string)  PoW algorithm name.\n"
                "    \"last_block_reward\": xxx.xxxxx,    (numeric) Value of last block reward for given algo\n"
                "    \"total_rewards_24h\": xxx.xxxxx,    (numeric) Total of block rewards per algo for past 24 hours\n"
                "    \"total_rewards_7d\" : xxx.xxxxx,    (numeric) Total of block rewards per algo for past 7 days\n"
                "    \"total_blocks_24h\":  xx,           (numeric) Number of blocks found per algo for past 24 hours\n"
                "    \"total_blocks_7d\" :  xx,           (numeric) Number of blocks found per algo for past 7 days\n"
                "  },\n"
                "   ..."
                "]\n"
            },
            RPCExamples{
                HelpExampleCli("getminingstats", "")
              + HelpExampleRpc("getminingstats", "")
            },
        }.ToString());

    UniValue arr(UniValue::VARR);
    UniValue algoObj(UniValue::VOBJ);
    int nBlocksTotal24h = (24 * 3600) / Params().GetConsensus().nPowTargetSpacing;
    int nBlocksTotal7d = (7 * 24 * 3600) / Params().GetConsensus().nPowTargetSpacing;
    int nAlgoBlocks24h;
    int nAlgoBlocks7d;
    std::vector<int32_t> algos = {ALGO_SHA256D, ALGO_SCRYPT, ALGO_LYRA2Z, ALGO_X11, ALGO_X16R, ALGO_NIST5};
    const CBlockIndex* pb;

    for(int i = 0; i < (int)algos.size(); i++) {
        pb = GetLastAlgoBlock(algos[i]);
        nAlgoBlocks24h = CountAlgoBlocks(algos[i], nBlocksTotal24h);
        nAlgoBlocks7d = CountAlgoBlocks(algos[i], nBlocksTotal7d);

        algoObj.pushKV("algo", GetAlgoName(algos[i]));
        algoObj.pushKV("last_block_reward", ValueFromAmount(GetBlockSubsidy(pb->nHeight, pb->GetBlockHeader(), Params().GetConsensus(), false)));

        if (nAlgoBlocks24h > 0) {
            algoObj.pushKV("avg_block_reward_24h", ValueFromAmount(CountAlgoBlockRewards(algos[i], nBlocksTotal24h) / nAlgoBlocks24h));
        } else {
            algoObj.pushKV("avg_block_reward_24h", 0);
        }

        if (nAlgoBlocks7d > 0) {
            algoObj.pushKV("avg_block_reward_7d", ValueFromAmount(CountAlgoBlockRewards(algos[i], nBlocksTotal7d) / nAlgoBlocks7d));
        } else {
            algoObj.pushKV("avg_block_reward_7d", 0);
        }

        algoObj.pushKV("total_blocks_24h", nAlgoBlocks24h);
        algoObj.pushKV("total_blocks_7d", nAlgoBlocks7d);
        // algoObj.pushKV("cost_factor", GetAlgoCostFactor(algos[i]));  // if shown it should be calculated to some human-understandable form, like %
        arr.push_back(algoObj);
    }

    return arr;
}
// VELES END

// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
static UniValue prioritisetransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            RPCHelpMan{"prioritisetransaction",
                "Accepts the transaction into mined blocks at a higher (or lower) priority\n",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id."},
                    {"dummy", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "API-Compatibility for previous API. Must be zero or null.\n"
            "                  DEPRECATED. For forward compatibility use named arguments and omit this parameter."},
                    {"fee_delta", RPCArg::Type::NUM, RPCArg::Optional::NO, "The fee value (in satoshis) to add (or subtract, if negative).\n"
            "                  Note, that this value is not a fee rate. It is a value to modify absolute fee of the TX.\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee."},
                },
                RPCResult{
            "true              (boolean) Returns true\n"
                },
                RPCExamples{
                    HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
            + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000")
                },
            }.ToString());

    LOCK(cs_main);

    uint256 hash(ParseHashV(request.params[0], "txid"));
    CAmount nAmount = request.params[2].get_int64();

    if (!(request.params[1].isNull() || request.params[1].get_real() == 0)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Priority is no longer supported, dummy argument to prioritisetransaction must be 0.");
    }

    mempool.PrioritiseTransaction(hash, nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, FormatStateMessage(state));
    if (state.IsInvalid())
    {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

static std::string gbt_vb_name(const Consensus::DeploymentPos pos) {
    const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force) {
        s.insert(s.begin(), '!');
    }
    return s;
}

static UniValue getblocktemplate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2) // Veles: Added parameter
        throw std::runtime_error(
            RPCHelpMan{"getblocktemplate",
                "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
                "It returns data needed to construct a block to work on.\n"
                "For full specification, see BIPs 22, 23, 9, and 145:\n"
                "    https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki\n"
                "    https://github.com/bitcoin/bips/blob/master/bip-0023.mediawiki\n"
                "    https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"
                "    https://github.com/bitcoin/bips/blob/master/bip-0145.mediawiki\n",
                {
                    {"template_request", RPCArg::Type::OBJ, RPCArg::Optional::NO, "A json object in the following spec",
                        {
                            {"mode", RPCArg::Type::STR, /* treat as named arg */ RPCArg::Optional::OMITTED_NAMED_ARG, "This must be set to \"template\", \"proposal\" (see BIP 23), or omitted"},
                            {"capabilities", RPCArg::Type::ARR, /* treat as named arg */ RPCArg::Optional::OMITTED_NAMED_ARG, "A list of strings",
                                {
                                    {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'"},
                                },
                                },
                            {"rules", RPCArg::Type::ARR, RPCArg::Optional::NO, "A list of strings",
                                {
                                    {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "client side supported softfork deployment"},
                                },
                                },
                        },
                        "\"template_request\""},
                    // VELES BEGIN
                    {"algorithm", RPCArg::Type::STR, GetAlgoName(miningAlgo) /* default */, "*EXPERIMENTAL* A name of the PoW algorithm used"}
                    // VELES END
                },
                RPCResult{
            "{\n"
            "  \"version\" : n,                    (numeric) The preferred block version\n"
            "  \"rules\" : [ \"rulename\", ... ],    (array of strings) specific block rules that are to be enforced\n"
            "  \"vbavailable\" : {                 (json object) set of pending, supported versionbit (BIP 9) softfork deployments\n"
            "      \"rulename\" : bitnumber          (numeric) identifies the bit number as indicating acceptance and readiness for the named softfork rule\n"
            "      ,...\n"
            "  },\n"
            "  \"vbrequired\" : n,                 (numeric) bit mask of versionbits the server requires set in submissions\n"
            "  \"previousblockhash\" : \"xxxx\",     (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",             (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"txid\" : \"xxxx\",             (string) transaction id encoded in little-endian hexadecimal\n"
            "         \"hash\" : \"xxxx\",             (string) hash encoded in little-endian hexadecimal (including witness data)\n"
            "         \"depends\" : [                (array) array of numbers \n"
            "             n                          (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                    (numeric) difference in value between transaction inputs and outputs (in satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,                (numeric) total SigOps cost, as counted for purposes of block limits; if key is not present, sigop cost is unknown and clients MUST NOT assume it is zero\n"
            "         \"weight\" : n,                (numeric) total transaction weight, as counted for purposes of block limits\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                 (json object) data that should be included in the coinbase's scriptSig content\n"
            "      \"flags\" : \"xx\"                  (string) key name is to be ignored, and value included in scriptSig\n"
            "  },\n"
            "  \"coinbasevalue\" : n,              (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in satoshis)\n"
            "  \"coinbasetxn\" : { ... },          (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",                (string) The hash target\n"
            "  \"mintime\" : xxx,                  (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                     (array of string) list of ways the block template may be changed \n"
            "     \"value\"                          (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",(string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"weightlimit\" : n,                (numeric) limit of block weight\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxxxxxxx\",              (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            // Dash
            "  \"masternode\" : {                  (json object) required masternode payee that must be included in the next block\n"
            "      \"payee\" : \"xxxx\",             (string) payee address\n"
            "      \"script\" : \"xxxx\",            (string) payee scriptPubKey\n"
            "      \"amount\": n                   (numeric) required amount to pay\n"
            "  },\n"
            "  \"masternode_payments_started\" :  true|false, (boolean) true, if masternode payments started\n"
            "  \"masternode_payments_enforced\" : true|false, (boolean) true, if masternode payments are enforced\n"
            "  \"superblock\" : [                  (array) required superblock payees that must be included in the next block\n"
            "      {\n"
            "         \"payee\" : \"xxxx\",          (string) payee address\n"
            "         \"script\" : \"xxxx\",         (string) payee scriptPubKey\n"
            "         \"amount\": n                (numeric) required amount to pay\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"superblocks_started\" : true|false, (boolean) true, if superblock payments started\n"
            "  \"superblocks_enabled\" : true|false  (boolean) true, if superblock payments are enabled\n"
            //
            // FXTC BEGIN
            "  \"founderreward\" : {               (json object) required founder reward that must be included in the next block\n"
            "      \"payee\" : \"xxxx\",           (string) payee address\n"
            "      \"amount\": n                   (numeric) required amount to pay\n"
            // FXTC END
            "  },\n"
            "}\n"
                },
                RPCExamples{
                    HelpExampleCli("getblocktemplate", "'{\"rules\": [\"segwit\"]}' x16r")    // Veles: Added parameter to the example
                  + HelpExampleRpc("getblocktemplate", "'{\"rules\": [\"segwit\"]}' x16r")
                },
            }.ToString()
        // VELES BEGIN
            + ((gArgs.GetBoolArg("-rpcbackcompatible", DEFAULT_RPC_BACK_COMPATIBLE))
                ? "\nNotice: RPC backward compatibility is enabled and this method will return a result even without the required argument"
                  "template_request. It will assume the default value of {\"rules\": [\"segwit\"]}."
                  "To enforce strict checking of syntax described above, use -rpcbackcompatible=0\n"
                : "")
        // VELES END
        );

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    int64_t nMaxVersionPreVB = -1;
    if (!request.params[0].isNull())
    {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            const CBlockIndex* pindex = LookupBlockIndex(hash);
            if (pindex) {
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue& aClientRules = find_value(oparam, "rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue& v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        } else {
            // NOTE: It is important that this NOT be read if versionbits is supported
            const UniValue& uvMaxVersion = find_value(oparam, "maxversion");
            if (uvMaxVersion.isNum()) {
                nMaxVersionPreVB = uvMaxVersion.get_int64();
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Veles is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Veles is downloading blocks...");

    // VELES BEGIN
    int32_t nPowAlgo = miningAlgo;
    if (!request.params[1].isNull())
        nPowAlgo = GetAlgoId(request.params[1].get_str());

    if (nPowAlgo == ALGO_NULL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Unknown algorithm %s", request.params[1].get_str()));
    // VELES END

    // Dash
    // when enforcement is on we need information about a masternode payee or otherwise our block is going to be orphaned by the network
    CScript payee;
    if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)
        && !masternodeSync.IsWinnersListSynced()
        && !mnpayments.GetBlockPayee(chainActive.Height() + 1, payee))
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Veles Core is downloading masternode winners...");

    // next bock is a superblock and we need governance info to correctly construct it
    if (sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)
        && !masternodeSync.IsSynced()
        && CSuperblock::IsValidBlockHeight(chainActive.Height() + 1))
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Veles Core is syncing with network...");
    //

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain = ParseHashV(lpstr.substr(0, 64), "longpollid");
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WAIT_LOCK(g_best_block_mutex, lock);
            while (g_best_block == hashWatchedChain && IsRPCRunning())
            {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    const struct VBDeploymentInfo& segwit_info = VersionBitsDeploymentInfo[Consensus::DEPLOYMENT_SEGWIT];
    // GBT must be called with 'segwit' set in the rules
    if (setClientRules.count(segwit_info.name) != 1) {
        // VELES BEGIN
        // Backwards compatibility with 0.17 where the rule parameter is optional
        if (gArgs.GetBoolArg("-rpcbackcompatible", DEFAULT_RPC_BACK_COMPATIBLE))
            setClientRules.insert("segwit");
        else
        // VELES END
            throw JSONRPCError(RPC_INVALID_PARAMETER, "getblocktemplate must be called with the segwit rule set (call with {\"rules\": [\"segwit\"]})");
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static std::unique_ptr<CBlockTemplate> pblocktemplate;
    static int32_t nTemplatePowAlgo;    // Veles
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5) ||
        nPowAlgo != nTemplatePowAlgo)   // Veles: Check whether the cached template has been generated for the same algo
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();
        nTemplatePowAlgo = nPowAlgo;    // Veles

        // Create new block
        CScript scriptDummy = CScript() << OP_TRUE;
        pblocktemplate = BlockAssembler(Params()).CreateNewBlock(scriptDummy, nPowAlgo); // VELES: Use given algo
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    assert(pindexPrev);
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    // NOTE: If at some point we support pre-segwit miners post-segwit-activation, this needs to take segwit support into consideration
    const bool fPreSegWit = (ThresholdState::ACTIVE != VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache));

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto& it : pblock->vtx) {
        const CTransaction& tx = *it;
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));
        entry.pushKV("txid", txHash.GetHex());
        entry.pushKV("hash", tx.GetWitnessHash().GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn &in : tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        int64_t nTxSigOps = pblocktemplate->vTxSigOpsCost[index_in_template];
        if (fPreSegWit) {
            assert(nTxSigOps % WITNESS_SCALE_FACTOR == 0);
            nTxSigOps /= WITNESS_SCALE_FACTOR;
        }
        entry.pushKV("sigops", nTxSigOps);
        entry.pushKV("weight", GetTransactionWeight(tx));

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);

    UniValue aRules(UniValue::VARR);
    UniValue vbavailable(UniValue::VOBJ);
    for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
        switch (state) {
            case ThresholdState::DEFINED:
            case ThresholdState::FAILED:
                // Not exposed to GBT at all
                break;
            case ThresholdState::LOCKED_IN:
                // Ensure bit is set in block version
                pblock->nVersion |= VersionBitsMask(consensusParams, pos);
                // FALL THROUGH to get vbavailable set...
            case ThresholdState::STARTED:
            {
                const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                vbavailable.pushKV(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit);
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    if (!vbinfo.gbt_force) {
                        // If the client doesn't support this, don't indicate it in the [default] version
                        pblock->nVersion &= ~VersionBitsMask(consensusParams, pos);
                    }
                }
                break;
            }
            case ThresholdState::ACTIVE:
            {
                // Add to rules only
                const struct VBDeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                aRules.push_back(gbt_vb_name(pos));
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    // Not supported by the client; make sure it's safe to proceed
                    if (!vbinfo.gbt_force) {
                        // If we do anything other than throw an exception here, be sure version/force isn't sent to old clients
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Support for '%s' rule requires explicit client support", vbinfo.name));
                    }
                }
                break;
            }
        }
    }
    result.pushKV("version", pblock->nVersion);
    result.pushKV("rules", aRules);
    result.pushKV("vbavailable", vbavailable);
    result.pushKV("vbrequired", int(0));

    if (nMaxVersionPreVB >= 2) {
        // If VB is supported by the client, nMaxVersionPreVB is -1, so we won't get here
        // Because BIP 34 changed how the generation transaction is serialized, we can only use version/force back to v2 blocks
        // This is safe to do [otherwise-]unconditionally only because we are throwing an exception above if a non-force deployment gets activated
        // Note that this can probably also be removed entirely after the first BIP9 non-force deployment (ie, probably segwit) gets activated
        aMutable.push_back("version/force");
    }

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0]->GetValueOut());
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    int64_t nSigOpLimit = MAX_BLOCK_SIGOPS_COST;
    int64_t nSizeLimit = MAX_BLOCK_SERIALIZED_SIZE;
    if (fPreSegWit) {
        assert(nSigOpLimit % WITNESS_SCALE_FACTOR == 0);
        nSigOpLimit /= WITNESS_SCALE_FACTOR;
        assert(nSizeLimit % WITNESS_SCALE_FACTOR == 0);
        nSizeLimit /= WITNESS_SCALE_FACTOR;
    }
    result.pushKV("sigoplimit", nSigOpLimit);
    result.pushKV("sizelimit", nSizeLimit);
    if (!fPreSegWit) {
        result.pushKV("weightlimit", (int64_t)MAX_BLOCK_WEIGHT);
    }
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight+1));

    // Dash
    UniValue masternodeObj(UniValue::VOBJ);
    if(pblock->txoutMasternode != CTxOut()) {
        CTxDestination address1;
        ExtractDestination(pblock->txoutMasternode.scriptPubKey, address1);
        std::string address2 = EncodeDestination(address1);
        masternodeObj.pushKV("payee", address2.c_str());
        masternodeObj.pushKV("script", HexStr(pblock->txoutMasternode.scriptPubKey.begin(), pblock->txoutMasternode.scriptPubKey.end()));
        masternodeObj.pushKV("amount", pblock->txoutMasternode.nValue);
    }
    result.pushKV("masternode", masternodeObj);
    result.pushKV("masternode_payments_started", pindexPrev->nHeight + 1 > Params().GetConsensus().nMasternodePaymentsStartBlock);
    result.pushKV("masternode_payments_enforced", sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT));

    UniValue superblockObjArray(UniValue::VARR);
    if(pblock->voutSuperblock.size()) {
        for (const CTxOut& txout : pblock->voutSuperblock) {
            UniValue entry(UniValue::VOBJ);
            CTxDestination address1;
            ExtractDestination(txout.scriptPubKey, address1);
            std::string address2 = EncodeDestination(address1);
            entry.pushKV("payee", address2.c_str());
            entry.pushKV("script", HexStr(txout.scriptPubKey.begin(), txout.scriptPubKey.end()));
            entry.pushKV("amount", txout.nValue);
            superblockObjArray.push_back(entry);
        }
    }
    result.pushKV("superblock", superblockObjArray);
    result.pushKV("superblocks_started", pindexPrev->nHeight + 1 > Params().GetConsensus().nSuperblockStartBlock);
    result.pushKV("superblocks_enabled", sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED));
    //

    // FXTC BEGIN
    CAmount founderReward = GetFounderReward(pindexPrev->nHeight+1,pblock->vtx[0]->GetValueOut());
    if (founderReward > 0) {
        UniValue founderRewardObj(UniValue::VOBJ);
        founderRewardObj.pushKV("founderpayee", Params().FounderAddress().c_str());
        founderRewardObj.pushKV("amount", founderReward);
        result.pushKV("founderreward", founderRewardObj);
        result.pushKV("founder_reward_enforced", true);
    }
    //FXTC END

    if (!pblocktemplate->vchCoinbaseCommitment.empty()) {
        result.pushKV("default_witness_commitment", HexStr(pblocktemplate->vchCoinbaseCommitment.begin(), pblocktemplate->vchCoinbaseCommitment.end()));
    }

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    explicit submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    void BlockChecked(const CBlock& block, const CValidationState& stateIn) override {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    }
};

static UniValue submitblock(const JSONRPCRequest& request)
{
    // We allow 2 arguments for compliance with BIP22. Argument 2 is ignored.
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            RPCHelpMan{"submitblock",
                "\nAttempts to submit new block to network.\n"
                "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n",
                {
                    {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block data to submit"},
                    {"dummy", RPCArg::Type::STR, /* default */ "ignored", "dummy value, for compatibility with BIP22. This value is ignored."},
                },
                RPCResults{},
                RPCExamples{
                    HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
                },
            }.ToString());
    }

    std::shared_ptr<CBlock> blockptr = std::make_shared<CBlock>();
    CBlock& block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    uint256 hash = block.GetHash();
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(hash);
        if (pindex) {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
                return "duplicate";
            }
            if (pindex->nStatus & BLOCK_FAILED_MASK) {
                return "duplicate-invalid";
            }
        }
    }

    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(block.hashPrevBlock);
        if (pindex) {
            UpdateUncommittedBlockStructures(block, pindex, Params().GetConsensus());
        }
    }

    bool new_block;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool accepted = ProcessNewBlock(Params(), blockptr, /* fForceProcessing */ true, /* fNewBlock */ &new_block);
    UnregisterValidationInterface(&sc);
    if (!new_block && accepted) {
        return "duplicate";
    }
    if (!sc.found) {
        return "inconclusive";
    }
    return BIP22ValidationResult(sc.state);
}

static UniValue submitheader(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            RPCHelpMan{"submitheader",
                "\nDecode the given hexdata as a header and submit it as a candidate chain tip if valid."
                "\nThrows when the header is invalid.\n",
                {
                    {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block header data"},
                },
                RPCResult{
            "None"
                },
                RPCExamples{
                    HelpExampleCli("submitheader", "\"aabbcc\"") +
                    HelpExampleRpc("submitheader", "\"aabbcc\"")
                },
            }.ToString());
    }

    CBlockHeader h;
    if (!DecodeHexBlockHeader(h, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block header decode failed");
    }
    {
        LOCK(cs_main);
        if (!LookupBlockIndex(h.hashPrevBlock)) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Must submit previous header (" + h.hashPrevBlock.GetHex() + ") first");
        }
    }

    CValidationState state;
    ProcessNewBlockHeaders({h}, state, Params(), /* ppindex */ nullptr, /* first_invalid */ nullptr);
    if (state.IsValid()) return NullUniValue;
    if (state.IsError()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, FormatStateMessage(state));
    }
    throw JSONRPCError(RPC_VERIFY_ERROR, state.GetRejectReason());
}

static UniValue estimatesmartfee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            RPCHelpMan{"estimatesmartfee",
                "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                "confirmation within conf_target blocks if possible and return the number of blocks\n"
                "for which the estimate is valid. Uses virtual transaction size as defined\n"
                "in BIP 141 (witness data is discounted).\n",
                {
                    {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
                    {"estimate_mode", RPCArg::Type::STR, /* default */ "CONSERVATIVE", "The fee estimate mode.\n"
            "                   Whether to return a more conservative estimate which also satisfies\n"
            "                   a longer history. A conservative estimate potentially returns a\n"
            "                   higher feerate and is more likely to be sufficient for the desired\n"
            "                   target, but is not as responsive to short term drops in the\n"
            "                   prevailing fee market.  Must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\""},
                },
                RPCResult{
            "{\n"
            "  \"feerate\" : x.x,     (numeric, optional) estimate fee rate in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": [ str... ] (json array of strings, optional) Errors encountered during processing\n"
            "  \"blocks\" : n         (numeric) block number where estimate was found\n"
            "}\n"
            "\n"
            "The request target will be clamped between 2 and the highest target\n"
            "fee estimation is able to return based on how long it has been running.\n"
            "An error is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate for any number of blocks.\n"
                },
                RPCExamples{
                    HelpExampleCli("estimatesmartfee", "6")
                },
            }.ToString());

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VSTR});
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int conf_target = ParseConfirmTarget(request.params[0]);
    bool conservative = true;
    if (!request.params[1].isNull()) {
        FeeEstimateMode fee_mode;
        if (!FeeModeFromString(request.params[1].get_str(), fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
        if (fee_mode == FeeEstimateMode::ECONOMICAL) conservative = false;
    }

    UniValue result(UniValue::VOBJ);
    UniValue errors(UniValue::VARR);
    FeeCalculation feeCalc;
    CFeeRate feeRate = ::feeEstimator.estimateSmartFee(conf_target, &feeCalc, conservative);
    if (feeRate != CFeeRate(0)) {
        result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
    } else {
        errors.push_back("Insufficient data or no feerate found");
        result.pushKV("errors", errors);
    }
    result.pushKV("blocks", feeCalc.returnedTarget);
    return result;
}

static UniValue estimaterawfee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            RPCHelpMan{"estimaterawfee",
                "\nWARNING: This interface is unstable and may disappear or change!\n"
                "\nWARNING: This is an advanced API call that is tightly coupled to the specific\n"
                "         implementation of fee estimation. The parameters it can be called with\n"
                "         and the results it returns will change if the internal implementation changes.\n"
                "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                "confirmation within conf_target blocks if possible. Uses virtual transaction size as\n"
                "defined in BIP 141 (witness data is discounted).\n",
                {
                    {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
                    {"threshold", RPCArg::Type::NUM, /* default */ "0.95", "The proportion of transactions in a given feerate range that must have been\n"
            "               confirmed within conf_target in order to consider those feerates as high enough and proceed to check\n"
            "               lower buckets."},
                },
                RPCResult{
            "{\n"
            "  \"short\" : {            (json object, optional) estimate for short time horizon\n"
            "      \"feerate\" : x.x,        (numeric, optional) estimate fee rate in " + CURRENCY_UNIT + "/kB\n"
            "      \"decay\" : x.x,          (numeric) exponential decay (per block) for historical moving average of confirmation data\n"
            "      \"scale\" : x,            (numeric) The resolution of confirmation targets at this time horizon\n"
            "      \"pass\" : {              (json object, optional) information about the lowest range of feerates to succeed in meeting the threshold\n"
            "          \"startrange\" : x.x,     (numeric) start of feerate range\n"
            "          \"endrange\" : x.x,       (numeric) end of feerate range\n"
            "          \"withintarget\" : x.x,   (numeric) number of txs over history horizon in the feerate range that were confirmed within target\n"
            "          \"totalconfirmed\" : x.x, (numeric) number of txs over history horizon in the feerate range that were confirmed at any point\n"
            "          \"inmempool\" : x.x,      (numeric) current number of txs in mempool in the feerate range unconfirmed for at least target blocks\n"
            "          \"leftmempool\" : x.x,    (numeric) number of txs over history horizon in the feerate range that left mempool unconfirmed after target\n"
            "      },\n"
            "      \"fail\" : { ... },       (json object, optional) information about the highest range of feerates to fail to meet the threshold\n"
            "      \"errors\":  [ str... ]   (json array of strings, optional) Errors encountered during processing\n"
            "  },\n"
            "  \"medium\" : { ... },    (json object, optional) estimate for medium time horizon\n"
            "  \"long\" : { ... }       (json object) estimate for long time horizon\n"
            "}\n"
            "\n"
            "Results are returned for any horizon which tracks blocks up to the confirmation target.\n"
                },
                RPCExamples{
                    HelpExampleCli("estimaterawfee", "6 0.9")
                },
            }.ToString());

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VNUM}, true);
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int conf_target = ParseConfirmTarget(request.params[0]);
    double threshold = 0.95;
    if (!request.params[1].isNull()) {
        threshold = request.params[1].get_real();
    }
    if (threshold < 0 || threshold > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid threshold");
    }

    UniValue result(UniValue::VOBJ);

    for (const FeeEstimateHorizon horizon : {FeeEstimateHorizon::SHORT_HALFLIFE, FeeEstimateHorizon::MED_HALFLIFE, FeeEstimateHorizon::LONG_HALFLIFE}) {
        CFeeRate feeRate;
        EstimationResult buckets;

        // Only output results for horizons which track the target
        if (conf_target > ::feeEstimator.HighestTargetTracked(horizon)) continue;

        feeRate = ::feeEstimator.estimateRawFee(conf_target, threshold, horizon, &buckets);
        UniValue horizon_result(UniValue::VOBJ);
        UniValue errors(UniValue::VARR);
        UniValue passbucket(UniValue::VOBJ);
        passbucket.pushKV("startrange", round(buckets.pass.start));
        passbucket.pushKV("endrange", round(buckets.pass.end));
        passbucket.pushKV("withintarget", round(buckets.pass.withinTarget * 100.0) / 100.0);
        passbucket.pushKV("totalconfirmed", round(buckets.pass.totalConfirmed * 100.0) / 100.0);
        passbucket.pushKV("inmempool", round(buckets.pass.inMempool * 100.0) / 100.0);
        passbucket.pushKV("leftmempool", round(buckets.pass.leftMempool * 100.0) / 100.0);
        UniValue failbucket(UniValue::VOBJ);
        failbucket.pushKV("startrange", round(buckets.fail.start));
        failbucket.pushKV("endrange", round(buckets.fail.end));
        failbucket.pushKV("withintarget", round(buckets.fail.withinTarget * 100.0) / 100.0);
        failbucket.pushKV("totalconfirmed", round(buckets.fail.totalConfirmed * 100.0) / 100.0);
        failbucket.pushKV("inmempool", round(buckets.fail.inMempool * 100.0) / 100.0);
        failbucket.pushKV("leftmempool", round(buckets.fail.leftMempool * 100.0) / 100.0);

        // CFeeRate(0) is used to indicate error as a return value from estimateRawFee
        if (feeRate != CFeeRate(0)) {
            horizon_result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
            horizon_result.pushKV("pass", passbucket);
            // buckets.fail.start == -1 indicates that all buckets passed, there is no fail bucket to output
            if (buckets.fail.start != -1) horizon_result.pushKV("fail", failbucket);
        } else {
            // Output only information that is still meaningful in the event of error
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
            horizon_result.pushKV("fail", failbucket);
            errors.push_back("Insufficient data or no feerate found which meets threshold");
            horizon_result.pushKV("errors",errors);
        }
        result.pushKV(StringForFeeEstimateHorizon(horizon), horizon_result);
    }
    return result;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getnetworkhashps",       &getnetworkhashps,       {"nblocks","height"} },
    { "mining",             "getmininginfo",          &getmininginfo,          {} },
    // Veles
    { "mining",             "gethalvinginfo",         &gethalvinginfo,         {} },
    { "mining",             "getmultialgoinfo",       &getmultialgoinfo,       {} },
    { "mining",             "getminingstats",         &getminingstats,         {} },
    //
    { "mining",             "prioritisetransaction",  &prioritisetransaction,  {"txid","dummy","fee_delta"} },
    { "mining",             "getblocktemplate",       &getblocktemplate,       {"template_request"} },
    { "mining",             "submitblock",            &submitblock,            {"hexdata","dummy"} },
    { "mining",             "submitheader",           &submitheader,           {"hexdata"} },

    { "generating",         "generatetoaddress",      &generatetoaddress,      {"nblocks","address","maxtries"} },

    { "util",               "estimatesmartfee",       &estimatesmartfee,       {"conf_target", "estimate_mode"} },

    { "hidden",             "estimaterawfee",         &estimaterawfee,         {"conf_target", "threshold"} },
    // Veles
    // Backward-compatible calls
    { "hidden",             "gethalvingstatus",       &gethalvinginfo,         {} },    // DEPRECATE in 0.19
    { "hidden",             "getmultialgostatus",     &getmultialgoinfo,       {} }     // DEPRECATE in 0.19
    //
};
// clang-format on

void RegisterMiningRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
