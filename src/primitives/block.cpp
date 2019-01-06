// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018 FXTC developers
// Copyright (c) 2018-2019 Veles Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <crypto/common.h>

#include <crypto/Lyra2Z.h>
#include <crypto/nist5.h>
#include <crypto/scrypt.h>
#include <crypto/x11.h>
#include <crypto/x16r.h>

// VELES BEGIN
#include <versionbits.h>
// VELES END

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash() const
{
    uint256 powHash = uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

  // VELES BEGIN
  if ((nVersion & VERSIONBITS_TOP_MASK) != VERSIONBITS_TOP_BITS)
      scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(powHash));
  else
  {
  // VELES END

    switch (nVersion & ALGO_VERSION_MASK)
    {
        case ALGO_SHA256D: powHash = GetHash(); break;
        case ALGO_SCRYPT:  scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(powHash)); break;
        case ALGO_NIST5:   powHash = NIST5(BEGIN(nVersion), END(nNonce)); break;
        case ALGO_LYRA2Z:  lyra2z_hash(BEGIN(nVersion), BEGIN(powHash)); break;
        case ALGO_X11:     powHash = HashX11(BEGIN(nVersion), END(nNonce)); break;
        case ALGO_X16R:    powHash = HashX16R(BEGIN(nVersion), END(nNonce), hashPrevBlock); break;
        default:           break; // FXTC TODO: we should not be here
    }

  // VELES BEGIN
  }
  // VELES END
    return powHash;
}

unsigned int CBlockHeader::GetAlgoEfficiency(int nBlockHeight) const
{
    switch (nVersion & ALGO_VERSION_MASK)
    {
        // VELES BEGIN
        //case ALGO_SHA256D: return       1;
        case ALGO_SHA256D: return       1;
        //case ALGO_SCRYPT:  return   13747;
        case ALGO_SCRYPT:  return   12984;
        //case ALGO_NIST5:   return    2631;
        case ALGO_NIST5:   return     513;     // 298735;
        //case ALGO_LYRA2Z:  return 2014035;
        case ALGO_LYRA2Z:  return 1973648;
        //case ALGO_X11:     return     477;
        case ALGO_X11:     return     513;
        //case ALGO_X16R:    return  263100;
        case ALGO_X16R:    return  257849;
        // VELES END
        default:           return       1; // FXTC TODO: we should not be here
    }

    return 1; // FXTC TODO: we should not be here
}

// VELES BEGIN
// Used for Veles Alpha rewards upgrade.
double CBlockHeader::GetAlgoCostFactor()
{
    CAmount totalAdjustements = 18.25;   // must match the sum of constants below
    double factor = 1;

    switch (nVersion & ALGO_VERSION_MASK)
    {
        case ALGO_SHA256D: factor = 10.00;  break;
        case ALGO_SCRYPT:  factor = 3.00;   break;
        case ALGO_NIST5:   factor = 1.00;   break;
        case ALGO_LYRA2Z:  factor = 0.50;   break;
        case ALGO_X11:     factor = 1.25;   break;
        case ALGO_X16R:    factor = 1.50;   break;
    }

    return factor / (totalAdjustements / 6);
}
// VELES END

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
