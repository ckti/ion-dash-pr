// Copyright (c) 2014-2020 The Ion Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "staking-manager.h"

#include "init.h"
#include "masternode/masternode-sync.h"
#include "miner.h"
#include "net.h"
#include "policy/policy.h"
#include "pos/blocksignature.h"
#include "pos/kernel.h"
#include "pos/stakeinput.h"
#include "script/sign.h"
#include "validation.h"
#include "wallet/wallet.h"

// fix windows build
#include <boost/thread.hpp>

std::shared_ptr<CStakingManager> stakingManager;

CStakingManager::CStakingManager(CWallet * const pwalletIn) :
        nMintableLastCheck(0), fMintableCoins(false), fLastLoopOrphan(false), nExtraNonce(0), // Currently unused
        fEnableStaking(false), fEnableIONStaking(false), nReserveBalance(0), pwallet(pwalletIn),
        nHashInterval(22), nLastCoinStakeSearchInterval(0), nLastCoinStakeSearchTime(GetAdjustedTime()) {}

bool CStakingManager::MintableCoins()
{
    if (pwallet == nullptr) return false;

    LOCK2(cs_main, pwallet->cs_wallet);

    int blockHeight = chainActive.Height();

    std::vector<COutput> vCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::STAKABLE_COINS;
    int nMinDepth = blockHeight >= Params().GetConsensus().nBlockStakeModifierV2 ? Params().GetConsensus().nStakeMinDepth : 1;
    pwallet->AvailableCoins(vCoins, true, &coin_control, 1, MAX_MONEY, MAX_MONEY, 0, nMinDepth);
    CAmount nAmountSelected = 0;

    for (const COutput &out : vCoins) {
        if (out.tx->tx->vin[0].IsZerocoinSpend() && !out.tx->IsInMainChain())
            continue;

        CBlockIndex* utxoBlock = mapBlockIndex.at(out.tx->hashBlock);
        //check for maturity (min age/depth)
        if (HasStakeMinAgeOrDepth(blockHeight, GetAdjustedTime(), utxoBlock->nHeight, utxoBlock->GetBlockTime()))
            return true;
    }
    return false;
}

bool CStakingManager::SelectStakeCoins(std::list<std::unique_ptr<CStakeInput> >& listInputs, CAmount nTargetAmount, int blockHeight)
{
    if (pwallet == nullptr) return false;

    LOCK2(cs_main, pwallet->cs_wallet);
    //Add ION
    std::vector<COutput> vCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::STAKABLE_COINS;
    int nMinDepth = blockHeight >= Params().GetConsensus().nBlockStakeModifierV2 ? Params().GetConsensus().nStakeMinDepth : 1;
    pwallet->AvailableCoins(vCoins, true, &coin_control, 1, MAX_MONEY, MAX_MONEY, 0, nMinDepth);
    CAmount nAmountSelected = 0;

    for (const COutput &out : vCoins) {
        //make sure not to outrun target amount
        if (nAmountSelected + out.tx->tx->vout[out.i].nValue > nTargetAmount)
            continue;

        if (out.tx->tx->vin[0].IsZerocoinSpend() && !out.tx->IsInMainChain())
            continue;

        CBlockIndex* utxoBlock = mapBlockIndex.at(out.tx->hashBlock);
        //check for maturity (min age/depth)
        if (!HasStakeMinAgeOrDepth(blockHeight, GetAdjustedTime(), utxoBlock->nHeight, utxoBlock->GetBlockTime()))
            continue;

        //add to our stake set
        nAmountSelected += out.tx->tx->vout[out.i].nValue;

        std::unique_ptr<CIonStake> input(new CIonStake());
        input->SetInput(out.tx->tx, out.i);
        listInputs.emplace_back(std::move(input));
    }
    return true;
}

bool CStakingManager::Stake(const CBlockIndex* pindexPrev, CStakeInput* stakeInput, unsigned int nBits, unsigned int& nTimeTx, uint256& hashProofOfStake)
{
    int prevHeight = pindexPrev->nHeight;

    // get stake input pindex
    CBlockIndex* pindexFrom = stakeInput->GetIndexFrom();
    if (!pindexFrom || pindexFrom->nHeight < 1) return error("%s : no pindexfrom", __func__);

    const uint32_t nTimeBlockFrom = pindexFrom->nTime;
    const int nHeightBlockFrom = pindexFrom->nHeight;

    // check for maturity (min age/depth) requirements
    if (!HasStakeMinAgeOrDepth(prevHeight + 1, nTimeTx, nHeightBlockFrom, nTimeBlockFrom))
        return error("%s : min age violation - height=%d - nTimeTx=%d, nTimeBlockFrom=%d, nHeightBlockFrom=%d",
                         __func__, prevHeight + 1, nTimeTx, nTimeBlockFrom, nHeightBlockFrom);

    // iterate the hashing
    bool fSuccess = false;
    const unsigned int nHashDrift = 60;
    const unsigned int nFutureTimeDriftPoS = 180;
    unsigned int nTryTime = nTimeTx - 1;
    // iterate from nTimeTx up to nTimeTx + nHashDrift
    // but not after the max allowed future blocktime drift (3 minutes for PoS)
    const unsigned int maxTime = std::min(nTimeTx + nHashDrift, (uint32_t)GetAdjustedTime() + nFutureTimeDriftPoS);

    while (nTryTime < maxTime)
    {
        //new block came in, move on
        if (chainActive.Height() != prevHeight)
            break;

        ++nTryTime;

        // if stake hash does not meet the target then continue to next iteration
        if (!CheckStakeKernelHash(pindexPrev, nBits, stakeInput, nTryTime, hashProofOfStake))
            continue;

        // if we made it this far, then we have successfully found a valid kernel hash
        fSuccess = true;
        nTimeTx = nTryTime;
        break;
    }

    mapHashedBlocks.clear();
    mapHashedBlocks[chainActive.Tip()->nHeight] = GetTime(); //store a time stamp of when we last hashed on this block
    return fSuccess;
}

bool CStakingManager::CreateCoinStake(const CBlockIndex* pindexPrev, std::shared_ptr<CMutableTransaction>& coinstakeTx, std::shared_ptr<CStakeInput>& coinstakeInput) {
    // Needs wallet
    if (pwallet == nullptr || pindexPrev == nullptr)
        return false;

    coinstakeTx->vin.clear();
    coinstakeTx->vout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    coinstakeTx->vout.push_back(CTxOut(0, scriptEmpty));

    // Choose coins to use
    CAmount nBalance = pwallet->GetBalance();

    if (nBalance > 0 && nBalance <= nReserveBalance)
        return false;

    // Get the list of stakable inputs
    std::list<std::unique_ptr<CStakeInput> > listInputs;
    if (!SelectStakeCoins(listInputs, nBalance - nReserveBalance, pindexPrev->nHeight + 1)) {
        LogPrint(BCLog::STAKING, "CreateCoinStake(): selectStakeCoins failed\n");
        return false;
    }

    if (GetAdjustedTime() - chainActive.Tip()->GetBlockTime() < 60) {
        if (Params().NetworkIDString() == CBaseChainParams::REGTEST) {
//            MilliSleep(1000);
        }
    }

    CScript scriptPubKeyKernel;
    bool fKernelFound = false;
    int nAttempts = 0;

    // Block time.
    unsigned int nTxNewTime = GetAdjustedTime();
    // If the block time is in the future, then starts there.
    if (pindexPrev->nTime > nTxNewTime) {
        nTxNewTime = pindexPrev->nTime;
    }

    for (std::unique_ptr<CStakeInput>& stakeInput : listInputs) {
        // Make sure the wallet is unlocked and shutdown hasn't been requested
        if (pwallet->IsLocked(true) || ShutdownRequested())
            return false;

        boost::this_thread::interruption_point();

        unsigned int stakeNBits = GetNextWorkRequired(pindexPrev, Params().GetConsensus(), false);
        uint256 hashProofOfStake = uint256();
        nAttempts++;
        //iterates each utxo inside of CheckStakeKernelHash()
        if (Stake(pindexPrev, stakeInput.get(), stakeNBits, nTxNewTime, hashProofOfStake)) {
            coinstakeTx->nTime = nTxNewTime;

            // Found a kernel
            LogPrint(BCLog::STAKING, "CreateCoinStake : kernel found\n");

            // Stake output value is set to stake input value.
            // Adding stake rewards and potentially splitting outputs is performed in BlockAssembler::CreateNewBlock()
            if (!stakeInput->CreateTxOuts(pwallet, coinstakeTx->vout, stakeInput->GetValue())) {
                LogPrint(BCLog::STAKING, "%s : failed to get scriptPubKey\n", __func__);
                return false;
            }

            // Limit size
            unsigned int nBytes = ::GetSerializeSize(*coinstakeTx, SER_NETWORK, CTransaction::CURRENT_VERSION);
            if (nBytes >= MAX_STANDARD_TX_SIZE)
                return error("CreateCoinStake : exceeded coinstake size limit");

            {
                uint256 hashTxOut = coinstakeTx->GetHash();
                CTxIn in;
                if (!stakeInput->CreateTxIn(pwallet, in, hashTxOut)) {
                    LogPrint(BCLog::STAKING, "%s : failed to create TxIn\n", __func__);
                    coinstakeTx->vin.clear();
                    coinstakeTx->vout.clear();
                    continue;
                }
                coinstakeTx->vin.emplace_back(in);
            }
            coinstakeInput = std::move(stakeInput);
            fKernelFound = true;
            break;
        }
    }
    LogPrint(BCLog::STAKING, "%s: attempted staking %d times\n", __func__, nAttempts);

    if (!fKernelFound)
        return false;

    // Successfully generated coinstake
    return true;
}

bool CStakingManager::IsStaking() {
    bool nStaking = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        nStaking = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        nStaking = true;
}

void CStakingManager::UpdatedBlockTip(const CBlockIndex* pindex)
{
    LOCK(cs);

    tipIndex = pindex;

    LogPrint(BCLog::STAKING, "CStakingManager::UpdatedBlockTip -- height: %d\n", pindex->nHeight);
}

void CStakingManager::DoMaintenance(CConnman& connman)
{
    if (!fEnableStaking) return; // Should never happen

    CBlockIndex* pindexPrev = chainActive.Tip();
    bool fHaveConnections = !g_connman ? false : g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) > 0;
    if (pwallet->IsLocked(true) || !pindexPrev || !masternodeSync.IsSynced() || !fHaveConnections || nReserveBalance >= pwallet->GetBalance()) {
        nLastCoinStakeSearchInterval = 0;
        MilliSleep(1 * 60 * 1000); // Wait 1 minute
        return;
    }

    const int nStakeHeight = pindexPrev->nHeight + 1;
    const Consensus::Params& params = Params().GetConsensus();
    const bool fPosPhase = (nStakeHeight >= params.POSStartHeight) || (nStakeHeight >= params.POSPOWStartHeight);

    if (!fPosPhase) {
        // no POS for at least 1 block
        nLastCoinStakeSearchInterval = 0;
        MilliSleep(1 * 60 * 1000); // Wait 1 minute
        return;
    }

    //search our map of hashed blocks, see if bestblock has been hashed yet
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight) && !fLastLoopOrphan) {
        // wait max 5 seconds if recently hashed
        int nTimePast = GetTime() - mapHashedBlocks[chainActive.Tip()->nHeight];
        if (nTimePast < nHashInterval && nTimePast >= 0) {
            MilliSleep(std::min(nHashInterval - nTimePast, (unsigned int)5) * 1000);
            return;
        }
    }
    fLastLoopOrphan = false;

   //control the amount of times the client will check for mintable coins
    if (!MintableCoins()) {
        // No mintable coins
        nLastCoinStakeSearchInterval = 0;
        LogPrint(BCLog::STAKING, "%s: No mintable coins, waiting..\n", __func__);
        MilliSleep(5 * 60 * 1000); // Wait 5 minutes
        return;
    }

    int64_t nSearchTime = GetAdjustedTime();
    if (nSearchTime < nLastCoinStakeSearchTime) {
        MilliSleep((nLastCoinStakeSearchTime - nSearchTime) * 1000); // Wait
        return;
    } else {
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    // Create new block
    std::shared_ptr<CMutableTransaction> coinstakeTxPtr = std::shared_ptr<CMutableTransaction>(new CMutableTransaction);
    std::shared_ptr<CStakeInput> coinstakeInputPtr = nullptr;
    std::unique_ptr<CBlockTemplate> pblocktemplate = nullptr;
    if (CreateCoinStake(chainActive.Tip(), coinstakeTxPtr, coinstakeInputPtr)) {
        // Coinstake found. Extract signing key from coinstake
        try {
            pblocktemplate = BlockAssembler(Params()).CreateNewBlock(CScript(), coinstakeTxPtr, coinstakeInputPtr);
        } catch (const std::exception& e) {
            LogPrint(BCLog::STAKING, "%s: error creating block, waiting.. - %s", __func__, e.what());
            MilliSleep(1 * 60 * 1000); // Wait 1 minute
            return;
        }
    } else {
        return;
    }

    if (!pblocktemplate.get())
        return;
    CBlock *pblock = &pblocktemplate->block;

    // Sign block
    CKeyID keyID;
    if (!GetKeyIDFromUTXO(pblock->vtx[1]->vout[1], keyID)) {
        LogPrint(BCLog::STAKING, "%s: failed to find key for PoS", __func__);
        return;
    }
    CKey key;
    if (!pwallet->GetKey(keyID, key)) {
        LogPrint(BCLog::STAKING, "%s: failed to get key from keystore", __func__);
        return;
    }
    if (!key.Sign(pblock->GetHash(), pblock->vchBlockSig)) {
        LogPrint(BCLog::STAKING, "%s: failed to sign block hash with key", __func__);
        return;
    }

    /// Process block
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
        fLastLoopOrphan = true;
        LogPrint(BCLog::STAKING, "%s: ProcessNewBlock, block not accepted", __func__);
        MilliSleep(10 * 1000); // Wait 10 seconds
    }
}
