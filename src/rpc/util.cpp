// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <keystore.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <tinyformat.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <boost/variant/static_visitor.hpp>

InitInterfaces *g_rpc_interfaces = nullptr;

// Converts a hex string to a public key if possible
CPubKey HexToPubKey(const std::string &hex_in) {
    if (!IsHex(hex_in)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Invalid public key: " + hex_in);
    }
    CPubKey vchPubKey(ParseHex(hex_in));
    if (!vchPubKey.IsFullyValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Invalid public key: " + hex_in);
    }
    return vchPubKey;
}

// Retrieves a public key for an address from the given CKeyStore
CPubKey AddrToPubKey(const CChainParams &chainparams, CKeyStore *const keystore,
                     const std::string &addr_in) {
    CTxDestination dest = DecodeDestination(addr_in, chainparams);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Invalid address: " + addr_in);
    }
    CKeyID key = GetKeyForDestination(*keystore, dest);
    if (key.IsNull()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           strprintf("%s does not refer to a key", addr_in));
    }
    CPubKey vchPubKey;
    if (!keystore->GetPubKey(key, vchPubKey)) {
        throw JSONRPCError(
            RPC_INVALID_ADDRESS_OR_KEY,
            strprintf("no full public key for address %s", addr_in));
    }
    if (!vchPubKey.IsFullyValid()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR,
                           "Wallet contains an invalid public key");
    }
    return vchPubKey;
}

// Creates a multisig redeemscript from a given list of public keys and number
// required.
CScript CreateMultisigRedeemscript(const int required,
                                   const std::vector<CPubKey> &pubkeys) {
    // Gather public keys
    if (required < 1) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "a multisignature address must require at least one key to redeem");
    }
    if ((int)pubkeys.size() < required) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           strprintf("not enough keys supplied (got %u keys, "
                                     "but need at least %d to redeem)",
                                     pubkeys.size(), required));
    }
    if (pubkeys.size() > 16) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Number of keys involved in the multisignature "
                           "address creation > 16\nReduce the number");
    }

    CScript result = GetScriptForMultisig(required, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            (strprintf("redeemScript exceeds size limit: %d > %d",
                       result.size(), MAX_SCRIPT_ELEMENT_SIZE)));
    }

    return result;
}

class DescribeAddressVisitor : public boost::static_visitor<UniValue> {
public:
    explicit DescribeAddressVisitor() {}

    UniValue operator()(const CNoDestination &dest) const {
        return UniValue(UniValue::VOBJ);
    }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("isscript", false);
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("isscript", true);
        return obj;
    }
};

UniValue DescribeAddress(const CTxDestination &dest) {
    return boost::apply_visitor(DescribeAddressVisitor(), dest);
}

std::string RPCHelpMan::ToString() const {
    std::string ret;

    ret += m_name;
    bool is_optional{false};
    for (const auto &arg : m_args) {
        ret += " ";
        if (arg.m_optional) {
            if (!is_optional) {
                ret += "( ";
            }
            is_optional = true;
        } else {
            // Currently we still support unnamed arguments, so any argument
            // following an optional argument must also be optional If support
            // for positional arguments is deprecated in the future, remove this
            // line
            assert(!is_optional);
        }
        ret += arg.ToString();
    }
    if (is_optional) {
        ret += " )";
    }
    ret += "\n";

    return ret;
}

std::string RPCArg::ToStringObj() const {
    std::string res = "\"" + m_name + "\":";
    switch (m_type) {
        case Type::STR:
            return res + "\"str\"";
        case Type::STR_HEX:
            return res + "\"hex\"";
        case Type::NUM:
            return res + "n";
        case Type::AMOUNT:
            return res + "amount";
        case Type::BOOL:
            return res + "bool";
        case Type::ARR:
            res += "[";
            for (const auto &i : m_inner) {
                res += i.ToString() + ",";
            }
            return res + "...]";
        case Type::OBJ:
        case Type::OBJ_USER_KEYS:
            // Currently unused, so avoid writing dead code
            assert(false);

            // no default case, so the compiler can warn about missing cases
    }
    assert(false);
}

std::string RPCArg::ToString() const {
    switch (m_type) {
        case Type::STR_HEX:
        case Type::STR: {
            return "\"" + m_name + "\"";
        }
        case Type::NUM:
        case Type::AMOUNT:
        case Type::BOOL: {
            return m_name;
        }
        case Type::OBJ:
        case Type::OBJ_USER_KEYS: {
            std::string res;
            for (size_t i = 0; i < m_inner.size();) {
                res += m_inner[i].ToStringObj();
                if (++i < m_inner.size()) {
                    res += ",";
                }
            }
            if (m_type == Type::OBJ) {
                return "{" + res + "}";
            } else {
                return "{" + res + ",...}";
            }
        }
        case Type::ARR: {
            std::string res;
            for (const auto &i : m_inner) {
                res += i.ToString() + ",";
            }
            return "[" + res + "...]";
        }

            // no default case, so the compiler can warn about missing cases
    }
    assert(false);
}
