// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 FXTC developers
// Copyright (c) 2018-2019 The Veles Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Hello broken world!";
    const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 2000;           // initial halving interval starting from VCIP01 activation block
        consensus.nMinimumSubsidy = 0.00100000 * COIN;
        consensus.nMasternodeMinimumConfirmations = 15;
	    consensus.nMasternodePaymentsStartBlock = 50;
//      consensus.nMasternodePaymentsIncreaseBlock = 50;    // In Veles is VCIP01 block
        consensus.nMasternodePaymentsIncreasePeriod = 365 * 576 * 5; // 5 years, activated with VCIP01
        consensus.nMasternodeCollateralMinimum = 2000; // starting MN collateral
        consensus.nMasternodeCollateralMaximum = 2000; // MN collateral at infinity
        consensus.nMasternodeCollateral201908  = 500;  // MN collateral
        // VELES BEGIN
        consensus.nMasternodePaymentsStartPercent =  5;     // at VCIP01 this should equal to approx same VLS amount as before
        consensus.nMasternodePaymentsFinalPercent =  60;    // will be reached in ca 5 years
        consensus.nMasternodePaymentsLegacyPercent = 40;    // bootstrap stage default until VCIP01
        consensus.nDevFundPaymentsDecreasePeriod =   365 * 576 * 5;   // 5 years of decreasing dev reward
        consensus.nDevFundPaymentsStartPercent =  5;        // 5% dev rewards at VCIP01
        consensus.nDevFundPaymentsFinalPercent =  0;        // no dev reward at all 5 years after VCIP01
        consensus.nDevFundPaymentsLegacyPercent = 5;        // bootstrap stage default until VCIP01
        consensus.nVlsRewardsAlphaMultiplier = 10;          // correction factor for block rewards since VCIP01
        // VELES END
        consensus.nInstantSendKeepLock = 24;
        consensus.nBudgetPaymentsStartBlock = 999 * 365 * 576;  // 999 common years
        consensus.nBudgetPaymentsCycleBlocks = 10958;           // weekly
        consensus.nBudgetPaymentsWindowBlocks = 100;
        consensus.nBudgetProposalEstablishingTime = 86400;      // 1 day
        consensus.nSuperblockStartBlock = 999 * 365 * 576;      // 999 common years
        consensus.nSuperblockCycle = 10958;                     // weekly
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        // VELES BEGIN
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 20;
        consensus.BIP34Hash = uint256S("0x001");
        consensus.BIP65Height = 30;
        consensus.BIP66Height = 30;
        // VELES END
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 4 * 60 * 60;     // 4 hours
        consensus.nPowTargetSpacing = 2 * 60;           // 2 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // FXTC TODO:
        // Dash
        // Deployment of DIP0001
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 4032;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThreshold = 3226; // 80% of 4032
        //
        //

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000"); // block 0
        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00000000e5ee4aaea9990a29990842f813ad353642bbfad16444478cbd6333e9"); // block 1000000

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xa4;
        pchMessageStart[1] = 0xc8;
        pchMessageStart[2] = 0xac;
        pchMessageStart[3] = 0x5b;
        nDefaultPort = 21337;
        nPruneAfterHeight = 100;

        genesis = CreateGenesisBlock(1541515949, 493360, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xd7d274ccc1d4f6ff6e5533442ff7d734a25357de4a5a4e4c01c39823cba68831"));
        assert(genesis.hashMerkleRoot == uint256S("0xbdcdbf2e9eaf16abf1422df3406902eb22d0314ab7eda1cd44d1da75b73a4bc3"));
        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the
	vSeeds.emplace_back("35.228.223.14");
        vSeeds.emplace_back("130.211.101.120");
	vSeeds.emplace_back("35.233.103.92");
        vSeeds.emplace_back("89.36.216.53");
	vSeeds.emplace_back("80.211.97.48");

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        //vSeeds.emplace_back("list.velesdnsseed.veles.network"); // VELES founder seeds
        //vSeeds.emplace_back("velesdnsseed.midnightminer.net"); // VELES MidnightMiner seeds
        vSeeds.emplace_back("seed.veles.network");
        vSeeds.emplace_back("seed1.veles.network");
        vSeeds.emplace_back("seed2.veles.network");
        vSeeds.emplace_back("seed3.veles.network");
        vSeeds.emplace_back("seed4.veles.network");
        vSeeds.emplace_back("seed5.veles.network");
        vSeeds.emplace_back("seed6.veles.network");
        vSeeds.emplace_back("seed7.veles.network");
        vSeeds.emplace_back("seed8.veles.network");
        vSeeds.emplace_back("seed9.veles.network");
        //vSeeds.emplace_back("seeds.on.another.domain"); // dns seeds template

        // VELES prefix 'B'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,70);
        // VELES prefix '3'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,30);
        // VELES prefix '2'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,212);
        // BIP32 prefixes
        base58Prefixes[EXT_PUBLIC_KEY] = {0xA4, 0x0C, 0x86, 0xFA};
        base58Prefixes[EXT_SECRET_KEY] = {0xA4, 0x0B, 0x91, 0xBD};

        bech32_hrp = "veles";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "045ba3e4d94368136f2cb66b873221a172f0e95d5e67e9da828e90f40dc4e4b962c19f621c6491749f3c235ab0e6c0cfbc4511a6e5569c9b18857345da589eb2e6";

        founderAddress = "VKtig9w7wfx6Ep9B4NnbwxLj1bkY2kHXBj";

        checkpointData = {
            {
                {  0, uint256S("0xd7d274ccc1d4f6ff6e5533442ff7d734a25357de4a5a4e4c01c39823cba68831")}

            }
        };
        // FXTC BEGIN
        consensus.nlastValidPowHashHeight = 100;
        // FXTC

        chainTxData = ChainTxData{
            1541515949, // * UNIX timestamp of last known number of transactions
            856096,     // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            960         // * estimated number of transactions per second after that timestamp
        };

        // FXTC TODO: we need to resolve fee calculation bug and disable fallback
        ///* disable fallback fee on mainnet */
        //m_fallback_fee_enabled = false;
        m_fallback_fee_enabled = true;
        //
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 2 * 365 * 576; // 2 common years
        consensus.nMinimumSubsidy = 0.00100000 * COIN;

        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.nMasternodePaymentsStartBlock = 10; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
//      consensus.nMasternodePaymentsIncreaseBlock = 10;
        consensus.nMasternodePaymentsIncreasePeriod = 25;
        consensus.nMasternodeCollateralMinimum = 10; // starting MN collateral
        consensus.nMasternodeCollateralMaximum = 1000; // MN collateral at infinity
        consensus.nMasternodeCollateral201908  = 500; // MN collateral
        // VELES BEGIN
        consensus.nMasternodePaymentsStartPercent =  5;
        consensus.nMasternodePaymentsFinalPercent =  60; // will be reached in ca 5 yrs
        consensus.nMasternodePaymentsLegacyPercent = 40; // value used until VCIP01
        consensus.nDevFundPaymentsDecreasePeriod =   365 * 576 * 5;   // 5 years
        consensus.nDevFundPaymentsStartPercent =  5;
        consensus.nDevFundPaymentsFinalPercent =  0; // no more dev rewards after 5 years
        consensus.nDevFundPaymentsLegacyPercent = 5;
        // VELES END
        consensus.nInstantSendKeepLock = 6;

        consensus.nBudgetPaymentsStartBlock = 999 * 365 * 576;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;

        consensus.nSuperblockStartBlock = 999 * 365 * 576;
        consensus.nSuperblockCycle = 24;

        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;

        consensus.BIP16Exception = uint256(); // inactive
        consensus.BIP34Height = -1; // inactive
        consensus.BIP34Hash = uint256S(""); // inactive
        consensus.BIP65Height = -1; // inactive
        consensus.BIP66Height = -1; // inactive
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60; // 3.5 days
        consensus.nPowTargetSpacing = 2.5 * 60; // 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // FXTC TODO:
        // Dash
        // Deployment of DIP0001
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThreshold = 50; // 50% of 100
        //
        //

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // VLS BEGIN
        consensus.nVlsRewardsAlphaMultiplier = 10;
        // VLS END

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xbc;
        pchMessageStart[1] = 0xa3;
        pchMessageStart[2] = 0x4f;
        pchMessageStart[3] = 0xc5;
        nDefaultPort = 21335;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1541515957, 271643, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        //assert(consensus.hashGenesisBlock == uint256S("0x3141b4039623ff0f86391d69394bc03c43a23a277b194980b21810e924349ccf"));
        //assert(genesis.hashMerkleRoot == uint256S("0x4a8775a67c499b83a1c9b46b185094c8a975ee79ab8cac49e8e27a8669484875"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        //vSeeds.emplace_back("testnet-seed.veles.network");
        //vSeeds.emplace_back("testnet-veles.midnightminer.net");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // BIP32 prefixes
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "testnet1veles";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "045ba3e4d94368136f2cb66b873221a172f0e95d5e67e9da828e90f40dc4e4b962c19f621c6491749f3c235ab0e6c0cfbc4511a6e5569c9b18857345da589eb2e6";

        founderAddress = "";

        checkpointData = {
            {
                {0, uint256S("0x3141b4039623ff0f86391d69394bc03c43a23a277b194980b21810e924349ccf")},
            }
        };
        // FXTC BEGIN
        consensus.nlastValidPowHashHeight = 1;
        // FXTC

        chainTxData = ChainTxData{
            // Data as of block 00000706ff7091032d187cf1c2515a5d7891b6797f414526ef8a277491117d08 (height 0)
            1541515957,
            2,
            960
        };

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMinimumSubsidy = 0.00100000 * COIN;

        consensus.nMasternodeMinimumConfirmations = 15;
	    consensus.nMasternodePaymentsStartBlock = 240;
//      consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nMasternodeCollateralMinimum = 1; // starting MN collateral
        consensus.nMasternodeCollateralMaximum = 100; // MN collateral at infinity
        consensus.nMasternodeCollateral201908  = 500; // MN collateral
        // VELES BEGIN
        consensus.nMasternodePaymentsStartPercent =  5;
        consensus.nMasternodePaymentsFinalPercent =  60; // will be reached in ca 5 yrs
        consensus.nMasternodePaymentsLegacyPercent = 40; // value used until VCIP01
        consensus.nDevFundPaymentsDecreasePeriod =   365 * 576 * 5;   // 5 years
        consensus.nDevFundPaymentsStartPercent =  5;
        consensus.nDevFundPaymentsFinalPercent =  0; // no more dev rewards after 5 years
        consensus.nDevFundPaymentsLegacyPercent = 5;
        // VELES END
        consensus.nInstantSendKeepLock = 6;

        consensus.nBudgetPaymentsStartBlock = 99999999;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nBudgetProposalEstablishingTime = 60*20;

        consensus.nSuperblockStartBlock = 99999999;
        consensus.nSuperblockCycle = 10;

        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;

        consensus.BIP16Exception = uint256(); // inactive
        consensus.BIP34Height = -1; // inactive
        consensus.BIP34Hash = uint256S("0x00"); // inactive
        consensus.BIP65Height = -1; // inactive
        consensus.BIP66Height = -1; // inactive
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60;  // VELES: 3.5 days
        consensus.nPowTargetSpacing = 2.5 * 60;             // VELES: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // FXTC TODO:
        // Dash
        // Deployment of DIP0001
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 0;
        //consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        //
        //

        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // VLS BEGIN
        consensus.nVlsRewardsAlphaMultiplier = 10;
        // VLS END

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 33814;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 0, 0x207fffff, 1, consensus.nMinimumSubsidy);
        consensus.hashGenesisBlock = genesis.GetHash();
        //assert(consensus.hashGenesisBlock == uint256S("0x00000f72bba40f458c48d44c842cee04ff91a2ae833fea4c179fe62170714681"));
        //assert(genesis.hashMerkleRoot == uint256S("0x4edd1885129d59d0276ef7ea548a77394837f2136f4dbb22d0acb3a7c4b2246c"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        nPoolMaxTransactions = 3;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        founderAddress = "";

        checkpointData = {
            {
//                {0, uint256S("0x00000f72bba40f458c48d44c842cee04ff91a2ae833fea4c179fe62170714681")},
            }
        };
        // FXTC BEGIN
        consensus.nlastValidPowHashHeight = 0;
        // FXTC

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        // Bitcoin defaults
        // FXTC prefix 'c'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,88);
        // FXTC prefix 'C'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,28);
        // FXTC prefix 'Z'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,221);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "regtest1veles";

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
