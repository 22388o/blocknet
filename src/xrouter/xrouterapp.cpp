// Copyright (c) 2018-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterapp.h>

#include <xrouter/xroutererror.h>
#include <xrouter/xrouterlogger.h>

#include <addrman.h>
#include <bloom.h>
#include <keystore.h>
#include <net.h>
#include <script/standard.h>
#include <servicenode/servicenodemgr.h>
#include <shutdown.h>
#include <univalue.h>

#include <chrono>
#include <iostream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/chrono/chrono.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

extern void Misbehaving(NodeId nodeid, int howmuch, const std::string& message="") EXCLUSIVE_LOCKS_REQUIRED(cs_main); // declared in net_processing.cpp

//*****************************************************************************
//*****************************************************************************
namespace xbridge{
extern bool CanAffordFeePayment(const CAmount & fee);
}

namespace xrouter
{

template <typename T>
bool PushXRouterMessage(CNode *pnode, const T & message) {
    const CNetMsgMaker msgMaker(pnode->GetSendVersion());
    g_connman->PushMessage(pnode, msgMaker.Make(NetMsgType::XROUTER, message));
    return true;
}

/**
 * Return a copy of nodes, with incremented reference count.
 * @return
 */
static std::vector<CNode*> CopyNodes() {
    std::vector<CNode*> nodes;
    g_connman->ForEachNode([&nodes](CNode *pnode) {
        pnode->AddRef();
        nodes.push_back(pnode);
    });
    return std::move(nodes);
}

//*****************************************************************************
//*****************************************************************************
App::App() : timerThread(boost::bind(&boost::asio::io_service::run, &timerIo))
           , timer(timerIo, boost::posix_time::seconds(XROUTER_TIMER_SECONDS))
{
}

//*****************************************************************************
//*****************************************************************************
App::~App()
{
    stop(false);
}

//*****************************************************************************
//*****************************************************************************
// static
App& App::instance()
{
    static App app;
    return app;
}

//*****************************************************************************
//*****************************************************************************
// static
bool App::isEnabled()
{
    return gArgs.GetBoolArg("-xrouter", true);
}

// static
bool App::createConf()
{
    try {
        std::string eol = "\n";
#ifdef WIN32
        eol = "\r\n";
#endif
        auto p = GetDataDir(false) / "xrouter.conf";
        if (!boost::filesystem::exists(p)) {
            saveConf(p,
                "[Main]"                                                                                            + eol +
                "#! host is a mandatory field, this tells the XRouter network how to find your node."               + eol +
                "#! DNS and ip addresses are acceptable values."                                                    + eol +
                "#! host=mynode.example.com"                                                                        + eol +
                "#! host=208.67.222.222"                                                                            + eol +
                "host="                                                                                             + eol +
                ""                                                                                                  + eol +
                "#! port is the tcpip port on the host that accepts xrouter connections."                           + eol +
                "#! port will default to the default blockchain port (e.g. 41412), examples:"                       + eol +
                "#! port=41412"                                                                                     + eol +
                "#! port=80"                                                                                        + eol +
                "#! port=8080"                                                                                      + eol +
                ""                                                                                                  + eol +
                "#! maxfee is the maximum fee (in BLOCK) you're willing to pay on a single xrouter call"            + eol +
                "#! 0 means you only want free calls"                                                               + eol +
                "maxfee=0"                                                                                          + eol +
                ""                                                                                                  + eol +
                "#! consensus is the minimum number of nodes you want your xrouter calls to query (1 or more)"      + eol +
                "#! Paid calls will send a payment to each selected service node."                                  + eol +
                "consensus=1"                                                                                       + eol +
                ""                                                                                                  + eol +
                "#! timeout is the maximum time in seconds you're willing to wait for an XRouter response"          + eol +
                "timeout=30"                                                                                        + eol +
                ""                                                                                                  + eol +
                "#! Optionally set per-call config options:"                                                        + eol +
                "#! [xrGetBlockCount]"                                                                              + eol +
                "#! maxfee=0.01"                                                                                    + eol +
                ""                                                                                                  + eol +
                "#! [BLOCK::xrGetBlockCount]"                                                                       + eol +
                "#! maxfee=0.01"                                                                                    + eol +
                ""                                                                                                  + eol +
                "#! [SYS::xrGetBlockCount]"                                                                         + eol +
                "#! maxfee=0.01"                                                                                    + eol +
                ""                                                                                                  + eol +
                "#! It's possible to set config options for Custom XRouter services"                                + eol +
                "#! [xrs::GetBestBlockHashBTC]"                                                                     + eol +
                "#! maxfee=0.1"                                                                                     + eol
            );
        }

        // Create the plugins directory if it doesn't exist
        auto plugins = GetDataDir(false) / "plugins";
        if (!boost::filesystem::exists(plugins)) {
            boost::filesystem::create_directory(plugins);
            auto samplerpc = plugins / "ExampleRPC.conf";
            saveConf(samplerpc,
                "#! ExampleRPC is a sample rpc plugin. This entire plugin configuration is sent to the client."     + eol +
                "#! Any lines beginning with #! will not be sent to the client."                                    + eol +
                "#! Any config parameters beginning with private:: will not be sent to the client."                 + eol +
                "#! The name of the plugin file ExampleRPC will be the service name broadcasted to the XRouter"     + eol +
                "#! network. Acceptable plugin names may include the characters: a-z A-Z 0-9 -"                     + eol +
                ""                                                                                                  + eol +
//                "#! Optional host identifying the location of the service. this tells the XRouter network how "     + eol +
//                "#! to find your node. The default is xrouter.conf [Main].host"                                     + eol +
//                "#! DNS and ip address are acceptable values."                                                      + eol +
//                "#! host=mynode.example.com"                                                                        + eol +
//                "#! host=208.67.222.222"                                                                            + eol +
//                ""                                                                                                  + eol +
//                "#! Optionally specify the port on the host that accepts xrouter connections for this plugin."      + eol +
//                "#! The default is xrouter.conf [Main].port"                                                        + eol +
//                "#! port=80"                                                                                        + eol +
//                ""                                                                                                  + eol +
                "#! parameters that you need from the user, acceptable types: string,bool,int,double"               + eol +
                "#! Example parameters=string,bool if you want to accept a string parameter and boolean"            + eol +
                "#! parameter from an XRouter client."                                                              + eol +
                "parameters="                                                                                       + eol +
                ""                                                                                                  + eol +
                "#! Set the fee in BLOCK to how much you want to charge for requests to this custom plugin."        + eol +
                "#! Example fee=0.1 if you want to accept 0.1 BLOCK or 0 if you want the plugin to be free."        + eol +
                "fee=0"                                                                                             + eol +
                ""                                                                                                  + eol +
                "#! Set the client request limit in milliseconds. -1 means unlimited. 50 means that a client"       + eol +
                "#! can only request at most once per 50 milliseconds (i.e. 20 times per second). If client"        + eol +
                "#! requests exceed this value they will be penalized and eventually banned by your node."          + eol +
                "clientrequestlimit=-1"                                                                             + eol +
                ""                                                                                                  + eol +
                "#! This is a sample configuration for the RPC plugin type for a syscoin plugin."                   + eol +
                "#! private:: config entries will not be sent to XRouter clients. Below is a sample rpc"            + eol +
                "#! plugin for syscoin running on 127.0.0.1:8370. This plugin accepts 0 parameters, as"             + eol +
                "#! indicated by \"parameters=\" config above, and it will call the syscoin \"getblockcount\""      + eol +
                "#! rpc command. The result will be forwarded onto the client."                                     + eol +
                "private::type=rpc"                                                                                 + eol +
                "private::rpcip=127.0.0.1"                                                                          + eol +
                "private::rpcport=8370"                                                                             + eol +
                "private::rpcuser=sysuser"                                                                          + eol +
                "private::rpcpassword=sysuser_pass"                                                                 + eol +
                ""                                                                                                  + eol +
                "#! Disable this sample plugin"                                                                     + eol +
                "disabled=1"                                                                                        + eol
            );
            auto sampledocker = plugins / "ExampleDocker.conf";
            saveConf(sampledocker,
                "#! ExampleDocker is a sample docker plugin. This entire plugin configuration is sent to the client." + eol +
                "#! Any lines beginning with #! will not be sent to the client."                                      + eol +
                "#! Any config parameters beginning with private:: will not be sent to the client."                   + eol +
                "#! The name of the plugin file ExampleDocker will be the service name broadcasted to the XRouter"    + eol +
                "#! network. Acceptable plugin names may include the characters: a-z A-Z 0-9 -"                       + eol +
                ""                                                                                                    + eol +
//                "#! Optional host identifying the location of the service. this tells the XRouter network how "       + eol +
//                "#! to find your node. The default is xrouter.conf [Main].host"                                       + eol +
//                "#! DNS and ip address are acceptable values."                                                        + eol +
//                "#! host=mynode.example.com"                                                                          + eol +
//                "#! host=208.67.222.222"                                                                              + eol +
//                ""                                                                                                    + eol +
//                "#! Optionally specify the port on the host that accepts xrouter connections for this plugin."        + eol +
//                "#! The default is xrouter.conf [Main].port"                                                          + eol +
//                "#! port=80"                                                                                          + eol +
//                ""                                                                                                    + eol +
                "#! parameters that you need from the user, acceptable types: string,bool,int,double"                 + eol +
                "#! Example parameters=string,bool if you want to accept a string parameter and boolean"              + eol +
                "#! parameter from an XRouter client."                                                                + eol +
                "parameters=string"                                                                                   + eol +
                ""                                                                                                    + eol +
                "#! Set the fee in BLOCK to how much you want to charge for requests to this custom plugin."          + eol +
                "#! Example fee=0.1 if you want to accept 0.1 BLOCK or 0 if you want the plugin to be free."          + eol +
                "fee=0"                                                                                               + eol +
                ""                                                                                                    + eol +
                "#! Set the client request limit in milliseconds. -1 means unlimited. 50 means that a client"         + eol +
                "#! can only request at most once per 50 milliseconds (i.e. 20 times per second). If client"          + eol +
                "#! requests exceed this value they will be penalized and eventually banned by your node."            + eol +
                "clientrequestlimit=-1"                                                                               + eol +
                ""                                                                                                    + eol +
                "#! This is a sample configuration of a docker plugin running a syscoin container."                   + eol +
                "#! private:: config entries will not be sent to XRouter clients. Below is a sample rpc"              + eol +
                "#! plugin for syscoin running in docker container \"syscoin\". This plugin accepts 1 parameter"      + eol +
                "#! indicated by \"parameters=\" config above, and it will call the syscoin \"getblock\" rpc"         + eol +
                "#! command. The result will be forwarded onto the client. \"quoteargs\" puts \"$1\" around"          + eol +
                "#! user supplied arguments. \"command\" executed within the docker container. \"args\" can"          + eol +
                "#! include both user supplied arguments ($1, $2, $3 etc.) and explicit arguments. For example,"      + eol +
                "#! you can mix both user supplied and custom arguments:  private::args=some_api_key $1 $2"           + eol +
                "private::type=docker"                                                                                + eol +
                "private::containername=syscoin"                                                                      + eol +
                "private::quoteargs=1"                                                                                + eol +
                "private::command=syscoin-cli getblock"                                                               + eol +
                "private::args=$1"                                                                                    + eol +
                ""                                                                                                    + eol +
                "#! Disable this sample plugin"                                                                       + eol +
                "disabled=1"                                                                                          + eol
            );
        }

        return true;

    } catch (...) {
        ERR() << "XRouter failed to create default xrouter.conf and plugins directory";
    }
    return false;
}

//*****************************************************************************
//*****************************************************************************
bool App::init()
{
    if (!isEnabled())
        return false;

    // Load the xrouter configuration
    try {
        xrouterpath = GetDataDir(false) / "xrouter.conf";
        xrsettings = std::make_shared<XRouterSettings>(CPubKey{});
        if (!xrsettings->init(xrouterpath))
            return false;
    } catch (...) {
        return false;
    }

    // Check for a valid host parameter if we're a servicenode
    if (gArgs.GetBoolArg("-servicenode", false) && xrsettings->host(xrDefault).empty()) {
        ERR() << "Failed to read xrouter config, missing \"host\" entry " << xrouterpath.string();
        return false;
    }

    // Create xrouter server instance
    server = std::make_shared<XRouterServer>();

    LOG() << "Loading xrouter config from file " << xrouterpath.string();
    return true;
}

bool App::start()
{
    if (!isEnabled())
        return false;

    // Only start server mode if we're a servicenode
    if (gArgs.GetBoolArg("-servicenode", false)) {
        bool res = server->start();
        if (res) { // set outgoing xrouter requests to use snode key
            LOCK(mu);
            cpubkey = server->pubKey();
            cprivkey = server->privKey();
        }
        // Update the node's default payment address
        if (sn::ServiceNodeMgr::instance().hasActiveSn()) {
            const auto & snode = sn::ServiceNodeMgr::instance().getActiveSn();
            updatePaymentAddress(snode.address);
        }
    } else if (!initKeyPair()) // init on regular xrouter clients (non-snodes)
        return false;

    {
        LOCK(mu);
        xrouterIsReady = true;
    }

    stopped = false;
    return true;
}

/**
 * Returns true if XRouter is ready to receive packets.
 * @return
 */
bool App::isReady() {
    LOCK(mu);
    return xrouterIsReady;
}

/**
 * Returns true if xrouter is ready to accept packets.
 * @return
 */
bool App::canListen() {
    return isReady() && sn::ServiceNodeMgr::instance().hasActiveSn();
}

static std::vector<sn::ServiceNode> getServiceNodes() {
    return std::move(sn::ServiceNodeMgr::instance().list());
}

bool App::createConnectors() {
    if (gArgs.GetBoolArg("-servicenode", false) && isEnabled())
        return server->createConnectors();
    ERR() << "Failed to load wallets: Must be a servicenode with xrouter=1 specified in config";
    return false;
}

bool App::openConnections(enum XRouterCommand command, const std::string & service, const uint32_t & count,
                          const int & parameterCount, const std::vector<CNode*> & skipNodes,
                          std::vector<sn::ServiceNode> & nonWalletXRNodes, uint32_t & foundCount)
{
    const auto fqService = (command == xrService) ? pluginCommandKey(service) // plugin
                                                  : walletCommandKey(service, XRouterCommand_ToString(command)); // spv wallet
    // use top-level wallet key (e.g. xr::BLOCK)
    const auto fqServiceAdjusted = (command == xrService) ? fqService
                                                          : walletCommandKey(service);
    // Initially set to 0
    foundCount = 0;

    if (fqServiceAdjusted.empty())
        return false;
    if (count < 1 || count > 50) {
        WARN() << "Cannot open less than 1 connection or more than 50, attempted: " << std::to_string(count) << " "
               << fqService;
        return false;
    }

    std::vector<sn::ServiceNode> snodes;
    std::vector<CNode*> nodes;
    std::map<NodeAddr, sn::ServiceNode> snodec;
    std::map<NodeAddr, CNode*> nodec;
    getLatestNodeContainers(snodes, nodes, snodec, nodec);

    Mutex lu; // handle threaded access
    uint32_t connected{0};
    // Only select nodes with a fee smaller than the max fee we're willing to pay
    const auto maxfee = xrsettings->maxFee(command, service);
    const auto connwait = xrsettings->configSyncTimeout();

    std::set<NodeAddr> connectedSnodes;
    for (auto & pnode : skipNodes) // add skipped nodes to prevent connection attempts
        connectedSnodes.insert(pnode->GetAddrName());

    // Retain node and add to containers
    auto addNode = [&lu,&nodes,&nodec](CNode *pnode) {
        if (!pnode)
            return;
        LOCK(lu);
        pnode->AddRef();
        nodes.push_back(pnode);
        nodec[pnode->GetAddrName()] = pnode;
    };

    auto connectedCount = [&connected,&lu]() {
        LOCK(lu);
        return connected;
    };

    auto failedChecks = [this,maxfee,parameterCount,command,service,fqService](const NodeAddr & nodeAddr,
                                                                               XRouterSettingsPtr settings) -> bool
    {
        auto fee = settings->commandFee(command, service);
        if (fee > 0) {
            if (fee > maxfee) {
                LOG() << "Skipping node " << nodeAddr << " because its fee " << fee << " is higher than maxfee " << maxfee;
                return true;
            }
            if (!xbridge::CanAffordFeePayment(fee * COIN)) {
                LOG() << "Skipping node " << nodeAddr << " because there's not enough utxos to cover payment " << fee;
                return true;
            }
        }
        const int & fetchLimit = settings->commandFetchLimit(command, service);
        if (parameterCount > fetchLimit) {
            LOG() << "Skipping node " << nodeAddr << " because its fetch limit " << fetchLimit << " is lower than "
                  << parameterCount;
            return true; // fetch limit exceeded
        }
        auto rateLimit = settings->clientRequestLimit(command, service);
        if (rateLimitExceeded(nodeAddr, fqService, getLastRequest(nodeAddr, fqService), rateLimit)) {
            LOG() << "Skipping node " << nodeAddr << " because not enough time passed since the last call";
            return true;
        }
        return false;
    };

    auto addSelected = [this,&connected,&connectedSnodes,&lu,&failedChecks](const std::string & snodeAddr) -> bool {
        LOCK(lu);
        if (!hasConfig(snodeAddr) || connectedSnodes.count(snodeAddr))
            return false;
        auto settings = getConfig(snodeAddr);
        if (settings && !failedChecks(snodeAddr, settings)) {
            ++connected;
            connectedSnodes.insert(snodeAddr);
            return true;
        }
        return false;
    };

    // Manages fetching the config from specified nodes
    auto fetchConfig = [this,connwait,&addSelected](CNode *node, const sn::ServiceNode & snode)
    {
        const std::string & nodeAddr = node->GetAddrName();

        // fetch config
        updateSentRequest(nodeAddr, XRouterCommand_ToString(xrGetConfig));
        std::string uuid = sendXRouterConfigRequest(node);
        LOG() << "Requesting config from snode " << EncodeDestination(CTxDestination(snode.getPaymentAddress()))
              << " query " << uuid;

        auto qcond = queryMgr.queryCond(uuid, nodeAddr);
        if (qcond) {
            auto l = queryMgr.queryLock(uuid, nodeAddr);
            boost::mutex::scoped_lock lock(*l);
            if (qcond->timed_wait(lock, boost::posix_time::seconds(connwait),
                [this,&uuid,&nodeAddr]() {
                    return ShutdownRequested() || queryMgr.hasReply(uuid, nodeAddr)
                                               || boost::this_thread::interruption_requested();
                }))
            {
                if (queryMgr.hasReply(uuid, nodeAddr))
                    addSelected(nodeAddr);
            }
            queryMgr.purge(uuid); // clean up
        }
    };

    auto connect = [this,connwait,&lu,&connectedSnodes,&fetchConfig,&addSelected,&addNode](const std::string & snodeAddr,
                                                                                           const sn::ServiceNode & snode)
    {
        // If we have a pending connection proceed in this block and wait for it to complete, otherwise add a pending
        // connection if none found and then skip waiting here to avoid race conditions and proceed to open a connection
        PendingConnectionMgr::PendingConnection conn;
        if (!pendingConnMgr.addPendingConnection(snodeAddr, conn)) {
            auto l = pendingConnMgr.connectionLock(snodeAddr);
            auto cond = pendingConnMgr.connectionCond(snodeAddr);
            if (l && cond) {
                const int & connw = std::max(connwait, 1) * 1000;
                boost::mutex::scoped_lock lock(*l);
                if (cond->timed_wait(lock, boost::posix_time::milliseconds(connw),
                    [this,&snodeAddr]() {
                        return ShutdownRequested() || !pendingConnMgr.hasPendingConnection(snodeAddr)
                                                   || boost::this_thread::interruption_requested();
                    }))
                {
                    bool alreadyConnected{false};
                    {
                        LOCK(lu);
                        alreadyConnected = connectedSnodes.count(snodeAddr) > 0;
                    }
                    if (ShutdownRequested() || alreadyConnected)
                        return; // no need to connect

                    g_connman->ForEachNode([&addSelected,snodeAddr](CNode *pnode) {
                        if (snodeAddr == pnode->GetAddrName()) { // if we found a valid connection
                            addSelected(snodeAddr);
                            return; // done
                        }
                    });
                }
            }
        }

        boost::this_thread::interruption_point();

        bool alreadyConnected{false};
        {
            LOCK(lu);
            alreadyConnected = connectedSnodes.count(snodeAddr) > 0;
        }
        if (alreadyConnected) {
            pendingConnMgr.notify(snodeAddr);
            return; // already connected
        }

        // Connect to snode
        CAddress addr(snode.getHostAddr(), NODE_NONE);
        CNode *node = g_connman->OpenXRouterConnection(addr, snodeAddr.c_str()); // Filters out bad nodes (banned, etc)
        if (node) {
            const auto startTime = GetTime();
            while (!node->fSuccessfullyConnected) { // wait n seconds for node to become available
                const auto currentTime = GetTime();
                if (ShutdownRequested() || currentTime - startTime > 3) { // wait 3 seconds
                    updateScore(snodeAddr, -5);
                    pendingConnMgr.notify(snodeAddr);
                    return;
                }
                boost::this_thread::interruption_point();
                boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
            }
            LOG() << "Connected to servicenode " << EncodeDestination(CTxDestination(snode.getPaymentAddress()));
            addNode(node); // store the node connection
            if (!hasConfig(snodeAddr))
                fetchConfig(node, snode);
            else
                addSelected(snodeAddr);
        } else {
            LOG() << "Failed to connect to servicenode " << EncodeDestination(CTxDestination(snode.getPaymentAddress()));
            updateScore(snodeAddr, -10);
        }

        pendingConnMgr.notify(snodeAddr);
    };

    // Check if existing snode connections have what we need
    std::set<NodeAddr> snodesConnected;
    std::map<NodeAddr, sn::ServiceNode> snodesNeedConfig;
    for (auto & s : snodes) {
        const auto & snodeAddr = s.getHost();
        if (!nodec.count(snodeAddr)) // skip non-connected nodes
            continue;

        if (!s.running()) // skip non-running snodes
            continue;

        if (connectedSnodes.count(snodeAddr)) // skip already selected nodes
            continue;

        if (!s.hasService(xr)) // has xrouter
            continue;

        if (!s.hasService(fqServiceAdjusted)) // has the service
            continue;

        if (hasConfig(snodeAddr)) // has config, count it
            snodesConnected.insert(snodeAddr);
        else
            snodesNeedConfig[snodeAddr] = s;
    }

    // Check our snode configs and connect to any nodes with required services
    // that we're not already connected to
    std::map<NodeAddr, sn::ServiceNode> needConnectionsHaveConfigs;
    auto configs = getConfigs();
    for (const auto & item : configs) {
        const auto & snodeAddr = item.first;
        auto config = item.second;

        if (nodec.count(snodeAddr) || connectedSnodes.count(snodeAddr) || snodesNeedConfig.count(snodeAddr))
            continue; // already processed, skip

        // only connect if snode is in the list
        if (snodec.count(snodeAddr) && (config->hasWallet(service) || config->hasPlugin(service)))
            needConnectionsHaveConfigs[snodeAddr] = snodec[snodeAddr];
    }

    // At this point all remaining snodes are ones we don't have configs for.
    std::map<NodeAddr, sn::ServiceNode> needConnectionsNoConfigs;
    for (auto & s : snodes) {
        const auto & snodeAddr = s.getHost();
        if (snodeAddr.empty()) // Sanity check
            continue;

        if (!s.running()) // skip non-running snodes
            continue;

        if (nodec.count(snodeAddr) || connectedSnodes.count(snodeAddr) || snodesNeedConfig.count(snodeAddr)
            || needConnectionsHaveConfigs.count(snodeAddr))
            continue; // already processed, skip

        if (!s.hasService(xr)) // has xrouter
            continue;

        if (!s.hasService(fqServiceAdjusted)) // has the service
            continue;

        needConnectionsNoConfigs[snodeAddr] = s;
    }

    // Connect to already connected snodes that need configs
    std::vector<NodeAddr> all{snodesConnected.begin(), snodesConnected.end()};
    for (const auto & it : snodesNeedConfig)
        all.push_back(it.first);
    for (const auto & it : needConnectionsHaveConfigs)
        all.push_back(it.first);
    for (const auto & it : needConnectionsNoConfigs)
        all.push_back(it.first);

    // Remove all nodes that use non-default ports
    nonWalletXRNodes.clear();
    for (int i = (int)all.size()-1; i >= 0; --i) {
        const auto & snodeAddr = all[i];
        auto settings = getConfig(snodeAddr);
        if (!settings || settings->port(xrDefault) != Params().GetDefaultPort()) {
            nonWalletXRNodes.push_back(snodec[snodeAddr]);
            all.erase(all.begin()+i);
        }
    }

    const int adjustedCount = static_cast<int>(count) - nonWalletXRNodes.size();
    if (!all.empty() && adjustedCount > 0) {
        if (all.size() < adjustedCount) { // Check not enough nodes
            releaseNodes(nodes);
            foundCount = all.size();
            return false;
        }

        // Sort by existing config snodes first
        std::sort(all.begin(), all.end(), [this,command,service](const NodeAddr & a, const NodeAddr & b) {
            return bestNode(a, b, command, service);
        });

        boost::thread_group tg;
        std::set<NodeAddr> conns;
        const auto timeout = xrsettings->commandTimeout(command, service);
        const auto startTime = GetAdjustedTime();
        bool timedOut{false};

        // Make connections via threads (max 2 per cpu core)
        for (const auto & snodeAddr : all) {
            if (snodesConnected.count(snodeAddr) && addSelected(snodeAddr)) // record already connected nodes
                continue;

            if (adjustedCount - connectedCount() <= 0)
                break; // done, all connected!

            bool needConfig = snodesNeedConfig.count(snodeAddr) > 0;
            bool needConnectionHaveConfig = needConnectionsHaveConfigs.count(snodeAddr) > 0;
            bool needConnectionAndConfig = needConnectionsNoConfigs.count(snodeAddr) > 0;

            sn::ServiceNode s;
            if (needConfig)                    s = snodesNeedConfig[snodeAddr];
            else if (needConnectionHaveConfig) s = needConnectionsHaveConfigs[snodeAddr];
            else if (needConnectionAndConfig)  s = needConnectionsNoConfigs[snodeAddr];

            auto node = nodec[snodeAddr];
            if (needConfig) {
                needConnectionAndConfig = !node || node->fDisconnect;
                if (needConnectionAndConfig)
                    needConfig = false;
            }

            tg.create_thread([node,s,snodeAddr,needConfig,needConnectionHaveConfig,
                              needConnectionAndConfig,&lu,&conns,&fetchConfig,&connect]()
            {
                RenameThread("blocknet-xrconnection");
                boost::this_thread::interruption_point();

                if (needConnectionHaveConfig || needConnectionAndConfig) {
                    { LOCK(lu); conns.insert(snodeAddr); }
                    connect(snodeAddr, s);
                }

                if (needConfig)
                    fetchConfig(node, s);
            });

            // Wait until we have all required connections (adjustedCount - connected = 0)
            // Wait while thread group has more running threads than we need connections for -or-
            //            thread group has too many threads (2 per cpu core)
            auto waitTime = GetAdjustedTime();
            while (adjustedCount - connectedCount() > 0 && // only continue waiting for threads if we need more connections
                  (tg.size() > (adjustedCount - connectedCount()) || tg.size() >= boost::thread::hardware_concurrency() * 2)) {
                if (ShutdownRequested()) {
                    tg.interrupt_all();
                    tg.join_all();
                    releaseNodes(nodes);
                    foundCount = connectedCount();
                    return false;
                }
                const auto t = GetAdjustedTime();
                if (t - waitTime > std::max(connwait, 1) * tg.size() // stop waiting after config sync timeouts
                    || t - startTime > timeout) // stop waiting after timeout seconds
                {
                    if (t - startTime > timeout)
                        timedOut = true;
                    tg.interrupt_all();
                    break;
                }
                boost::this_thread::interruption_point();
                boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
            }

            if (timedOut || ShutdownRequested())
                break;
        }

        if (adjustedCount - connectedCount() <= 0 || timedOut)
            tg.interrupt_all();
        tg.join_all();

        // Clean up outstanding pending connections
        for (const auto & snodeAddr : conns) {
            if (pendingConnMgr.hasPendingConnection(snodeAddr))
                pendingConnMgr.removePendingConnection(snodeAddr);
        }
    }

    releaseNodes(nodes);
    foundCount = connected + nonWalletXRNodes.size();
    // Account for the non-wallet xrouter nodes
    return foundCount >= count;
}

std::string App::updateConfigs(bool force)
{
    if (!isEnabled() || !isReady())
        return "XRouter is turned off. Please set 'xrouter=1' in blocknet.conf to run XRouter.";
    
    std::vector<sn::ServiceNode> vServicenodes = getServiceNodes();
    std::vector<CNode*> nodes = CopyNodes();

    // Build snode cache
    std::map<std::string, sn::ServiceNode> snodes;
    for (sn::ServiceNode & s : vServicenodes) {
        if (!s.getHost().empty())
            snodes[s.getHost()] = s;
    }

    // Query servicenodes that haven't had configs updated recently
    for (CNode* pnode : nodes) {
        const auto nodeAddr = pnode->GetAddrName();
        // skip non-xrouter nodes, disconnecting nodes, and self
        if (!snodes.count(nodeAddr) || !snodes[nodeAddr].hasService(xr) || !pnode->fSuccessfullyConnected
            || pnode->fDisconnect || snodes[nodeAddr].getSnodePubKey() == sn::ServiceNodeMgr::instance().getActiveSn().key.GetPubKey())
            continue;

        // If not force check, rate limit config requests
        if (!force) {
            const auto & service = XRouterCommand_ToString(xrGetConfig);
            if (!needConfigUpdate(nodeAddr, service))
                continue;
        }

        // Request the config
        auto & snode = snodes[nodeAddr]; // safe here due to check above
        updateSentRequest(nodeAddr, XRouterCommand_ToString(xrGetConfig));
        std::string uuid = sendXRouterConfigRequest(pnode);
        LOG() << "Requesting config from snode " << EncodeDestination(CTxDestination(snode.getPaymentAddress()))
              << " query " << uuid;
    }

    releaseNodes(nodes);
    return "Config requests have been sent";
}

std::string App::printConfigs()
{
    LOCK(mu);
    Array result;

    for (const auto & it : snodeConfigs) {
        Object val;
        std::vector<unsigned char> spubkey; servicenodePubKey(it.second->getNode(), spubkey);
        val.emplace_back("nodepubkey", HexStr(spubkey));
        val.emplace_back("paymentaddress", it.second->paymentAddress(xrGetConfig));
        val.emplace_back("config", it.second->publicText());
        Object p_val;
        for (const auto & p : it.second->getPlugins()) {
            auto pp = it.second->getPluginSettings(p);
            if (pp)
                p_val.emplace_back(p, pp->publicText());
        }
        val.emplace_back("plugins", p_val);
        result.push_back(val);
    }
    
    return json_spirit::write_string(Value(result), true);
}

//*****************************************************************************
//*****************************************************************************
bool App::stop(const bool safeCleanup)
{
    if (stopped)
        return true;
    stopped = true;

    if (safeCleanup)
        LOG() << "stopping xrouter threads...";

    timer.cancel();
    timerIo.stop();
    timerThread.join();

    if (safeCleanup && (!isEnabled() || !isReady()))
        return false;

    // shutdown threads
    requestHandlers.interrupt_all();
    requestHandlers.join_all();

    if (server && !server->stop())
        return false;

    return true;
}
 
//*****************************************************************************
//*****************************************************************************
std::vector<CNode*> App::availableNodesRetained(enum XRouterCommand command, const std::string & service,
                                                const int & parameterCount, const int & count)
{
    std::vector<CNode*> selectedNodes;

    auto sconfigs = getConfigs();
    if (sconfigs.empty())
        return selectedNodes; // don't have any configs, return

    std::vector<sn::ServiceNode> snodes;
    std::vector<CNode*> nodes;
    std::map<NodeAddr, sn::ServiceNode> snodec;
    std::map<NodeAddr, CNode*> nodec;
    getLatestNodeContainers(snodes, nodes, snodec, nodec);

    // Max fee we're willing to pay to snodes
    auto maxfee = xrsettings->maxFee(command, service);

    // Look through known xrouter node configs and select those that support command/service/fees
    for (const auto & item : sconfigs) {
        const auto & nodeAddr = item.first;
        auto settings = item.second;

        // fully qualified command e.g. xr::ServiceName
        const auto & commandStr = XRouterCommand_ToString(command);
        const auto & fqCmd = (command == xrService) ? pluginCommandKey(service) // plugin
                                                    : walletCommandKey(service, commandStr); // spv wallet

        if (command != xrService && !settings->hasWallet(service))
            continue;
        if (!settings->isAvailableCommand(command, service))
            continue;
        if (!snodec.count(nodeAddr)) // Ignore if not a snode
            continue;
        if (!snodec[nodeAddr].running()) // skip if not running
            continue;
        if (!snodec[nodeAddr].hasService(command == xrService ? fqCmd : walletCommandKey(service))) // use top-level wallet key (e.g. xr::BLOCK)
            continue; // Ignore snodes that don't have the service

        // This is the node whose config we are looking at now
        CNode *node = nullptr;
        if (nodec.count(nodeAddr))
            node = nodec[nodeAddr];

        // If the service node is not among peers
        if (!node)
            continue; // skip

        // Only select nodes with a fee smaller than the max fee we're willing to pay
        auto fee = settings->commandFee(command, service);
        if (fee > 0) {
            if (fee > maxfee) {
                const auto & snodeAddr = EncodeDestination(CTxDestination(snodec[nodeAddr].getPaymentAddress()));
                LOG() << "Skipping node " << snodeAddr << " because its fee " << fee << " is higher than maxfee " << maxfee;
                continue;
            }
            if (!xbridge::CanAffordFeePayment(fee * COIN)) {
                const auto & snodeAddr = EncodeDestination(CTxDestination(snodec[nodeAddr].getPaymentAddress()));
                LOG() << "Skipping node " << snodeAddr << " because there's not enough utxos to cover payment " << fee;
                continue;
            }
        }

        // Only select nodes who's fetch limit is acceptable
        const auto & fetchLimit = settings->commandFetchLimit(command, service);
        if (parameterCount > fetchLimit) {
            const auto & snodeAddr = EncodeDestination(CTxDestination(snodec[nodeAddr].getPaymentAddress()));
            LOG() << "Skipping node " << snodeAddr << " because its fetch limit " << fetchLimit << " is lower than "
                  << parameterCount;
            continue;
        }

        auto rateLimit = settings->clientRequestLimit(command, service);
        if (rateLimitExceeded(nodeAddr, fqCmd, getLastRequest(nodeAddr, fqCmd), rateLimit)) {
            const auto & snodeAddr = EncodeDestination(CTxDestination(snodec[nodeAddr].getPaymentAddress()));
            LOG() << "Skipping node " << snodeAddr << " because not enough time passed since the last call";
            continue;
        }
        
        selectedNodes.push_back(node);
    }

    // Sort selected nodes descending by score and lowest price first
    std::sort(selectedNodes.begin(), selectedNodes.end(), [this,command,service](const CNode *a, const CNode *b) {
        const auto & a_addr = a->GetAddrName();
        const auto & b_addr = b->GetAddrName();
        return bestNode(a_addr, b_addr, command, service);
    });

    // Retain selected nodes
    for (auto & pnode : selectedNodes)
        pnode->AddRef();

    // Release other nodes
    releaseNodes(nodes);

    return selectedNodes;
}

std::string App::parseConfig(XRouterSettingsPtr cfg)
{
    Object result;
    result.emplace_back("config", cfg->publicText());
    Object plugins;
    for (const std::string & s : cfg->getPlugins())
        plugins.emplace_back(s, cfg->getPluginSettings(s)->publicText());
    result.emplace_back("plugins", plugins);
    return json_spirit::write_string(Value(result), true);
}

//*****************************************************************************
//*****************************************************************************

bool App::processInvalid(CNode *node, XRouterPacketPtr packet, CValidationState & state)
{
    const auto & uuid = packet->suuid();
    const auto & nodeAddr = node->GetAddrName();

    // Do not process if we aren't expecting a result
    if (!queryMgr.hasNodeQuery(nodeAddr))
        return false; // done, nothing found

    // Verify servicenode response
    std::vector<unsigned char> spubkey;
    if (!servicenodePubKey(nodeAddr, spubkey) || !packet->verify(spubkey)) {
        state.DoS(20, error("XRouter: unsigned packet or signature error"), REJECT_INVALID, "xrouter-error");
        return false;
    }

    uint32_t offset = 0;
    std::string reply((const char *)packet->data()+offset);
    offset += reply.size() + 1;

    // Log the invalid reply
    LOG() << "Received error reply to query " << uuid << "\n" << reply;

    return true;
}

bool App::processReply(CNode *node, XRouterPacketPtr packet, CValidationState & state)
{
    const auto & uuid = packet->suuid();
    const auto & nodeAddr = node->GetAddrName();

    // Do not process if we aren't expecting a result. Also prevent reply malleability (only first reply is accepted)
    if (!queryMgr.hasQuery(uuid, nodeAddr) || queryMgr.hasReply(uuid, nodeAddr))
        return false; // done, nothing found

    // Verify servicenode response
    std::vector<unsigned char> spubkey;
    if (!servicenodePubKey(nodeAddr, spubkey) || !packet->verify(spubkey)) {
        state.DoS(20, error("XRouter: unsigned packet or signature error"), REJECT_INVALID, "xrouter-error");
        return false;
    }

    uint32_t offset = 0;
    std::string reply((const char *)packet->data()+offset);
    offset += reply.size() + 1;

    // Store the reply
    queryMgr.addReply(uuid, nodeAddr, reply);
    queryMgr.purge(uuid, nodeAddr);

    LOG() << "Received reply to query " << uuid << "\n" << reply;

    return true;
}

bool App::processConfigReply(CNode *node, XRouterPacketPtr packet, CValidationState & state)
{
    const auto & uuid = packet->suuid();
    const auto & nodeAddr = node->GetAddrName();

    // Do not process if we aren't expecting a result. Also prevent reply malleability (only first reply is accepted)
    if (!queryMgr.hasQuery(uuid, nodeAddr) || queryMgr.hasReply(uuid, nodeAddr))
        return false; // done, nothing found

    // Verify servicenode response
    std::vector<unsigned char> spubkey;
    if (!servicenodePubKey(nodeAddr, spubkey) || !packet->verify(spubkey)) {
        state.DoS(20, error("XRouter: unsigned packet or signature error"), REJECT_INVALID, "xrouter-error");
        return false;
    }

    uint32_t offset = 0;
    std::string reply((const char *)packet->data()+offset);
    offset += reply.size() + 1;

    try {
        Value reply_val;
        read_string(reply, reply_val);
        Object reply_obj = reply_val.get_obj();
        std::string config = find_value(reply_obj, "config").get_str();
        Object plugins = find_value(reply_obj, "plugins").get_obj();

        auto settings = std::make_shared<XRouterSettings>(CPubKey(spubkey.begin(), spubkey.end()), false); // not our config
        auto configInit = settings->init(config);
        if (!configInit) {
            ERR() << "Failed to read config on query " << uuid << " from node " << nodeAddr;
            updateScore(nodeAddr, -10);
            reply = "Failed to parse config from XRouter node " + nodeAddr + "\n" + reply;
            queryMgr.addReply(uuid, nodeAddr, reply);
            queryMgr.purge(uuid, nodeAddr);
            return false;
        }

        for (const auto & plugin : plugins) {
            try {
                auto psettings = std::make_shared<XRouterPluginSettings>(false); // not our config
                psettings->read(plugin.value_.get_str());
                settings->addPlugin(plugin.name_, psettings);
            } catch (...) {
                ERR() << "Failed to read plugin " << plugin.name_ << " on query " << uuid << " from node " << nodeAddr;
                updateScore(nodeAddr, -2);
            }
        }

        // Update settings for node
        updateConfig(sn::ServiceNodeMgr::instance().getSn(nodeAddr), settings);
        queryMgr.addReply(uuid, nodeAddr, reply);
        queryMgr.purge(uuid, nodeAddr);

    } catch (...) {
        ERR() << "Failed to read config on query " << uuid << " from node " << nodeAddr;
        updateScore(nodeAddr, -10);
        reply = "Failed to parse config from XRouter node " + nodeAddr + "\n" + reply;
        queryMgr.addReply(uuid, nodeAddr, reply);
        queryMgr.purge(uuid, nodeAddr);
        return false;
    }

    LOG() << "Received reply to query " << uuid << " from node " << nodeAddr << "\n" << reply;

    return true;
}

bool App::processConfigMessage(const sn::ServiceNode & snode) {
    auto & smgr = sn::ServiceNodeMgr::instance();
    if (smgr.hasActiveSn() && smgr.getActiveSn().key.GetPubKey() == snode.getSnodePubKey())
        return false; // do not process own config

    const auto & rawconfig = snode.getConfig("xrouter");
    UniValue uv;
    if (!uv.read(rawconfig))
        return false;

    auto settings = std::make_shared<XRouterSettings>(snode.getSnodePubKey(), false); // not our config
    try {
        const auto uvconf = find_value(uv, "config");
        if (uvconf.isNull() || !uvconf.isStr())
            return false;
        if (!settings->init(uvconf.get_str()))
            return false;
    } catch (...) {
        return false;
    }

    const auto & plugins = find_value(uv, "plugins");
    if (!plugins.isNull()) {
        std::map<std::string, UniValue> kv; plugins.getObjMap(kv);
        for (const auto & item : kv) {
            const auto & plugin = item.first;
            const auto & config = item.second.write();
            try {
                auto psettings = std::make_shared<XRouterPluginSettings>(false); // not our config
                psettings->read(config);
                settings->addPlugin(plugin, psettings);
            } catch (...) { }
        }
    }

    // Update settings for node
    updateConfig(snode, settings);
    return true;
}

//*****************************************************************************
//*****************************************************************************
void App::onMessageReceived(CNode* node, const std::vector<unsigned char> & message)
{
    // If xrouter == 0, xrouter is turned off on this node
    if (!isEnabled() || !isReady())
        return;

    auto retainNode = [](CNode *pnode) {
        pnode->AddRef();
    };

    retainNode(node); // retain for thread below

    // Handle the xrouter request
    requestHandlers.create_thread([this, node, message]() {
        RenameThread("blocknet-xrrequest");
        boost::this_thread::interruption_point();
        CValidationState state;

        bool released{false};
        auto releaseNode = [&released](CNode *pnode) {
            if (!released) {
                released = true;
                pnode->Release();
            }
        };

        try {
            XRouterPacketPtr packet(new XRouterPacket);
            if (!packet->copyFrom(message)) {
                if (server->isStarted()) { // Send error back to client
                    try {
                        Object error;
                        error.emplace_back("error", "XRouter Node reported a protocol error on a received packet. "
                                                    "Unable to deserialize packet, possible bad packet header");
                        error.emplace_back("code", xrouter::BAD_REQUEST);
                        const std::string reply = json_spirit::write_string(Value(error), true);
                        XRouterPacket packet(xrInvalid, "protocol_error");
                        packet.append(reply);
                        packet.sign(server->pubKey(), server->privKey());
                        PushXRouterMessage(node, packet.body());
                    } catch (std::exception & e) { // catch json errors
                        ERR() << "Failed to send error reply to client " << node->GetAddrName() << " error: "
                              << e.what();
                    }
                }

                updateScore(node->GetAddrName(), -10);
                state.DoS(10, error("XRouter: invalid packet received"), REJECT_INVALID, "xrouter-error");
                checkDoS(state, node);
                releaseNode(node);

                return;
            }

            const auto & command = packet->command();
            const auto & uuid = packet->suuid();
            const auto & nodeAddr = node->GetAddrName();
            const auto & commandStr = XRouterCommand_ToString(command);

            if (command == xrService) {
                auto service = packet->service();
                if (service.size() > 100) // truncate service name
                    service = service.substr(0, 100);
                LOG() << "XRouter command: " << commandStr << xrdelimiter + service << " query: " << uuid << " node: " << nodeAddr;
            }
            else
                LOG() << "XRouter command: " << commandStr << " query: " << uuid << " node: " << nodeAddr;

            if (command == xrInvalid) { // Process invalid packets (protocol error packets)
                processInvalid(node, packet, state);
            } else if (command == xrReply) { // Process replies
                processReply(node, packet, state);
            } else if (command == xrConfigReply) { // Process config replies
                processConfigReply(node, packet, state);
            } else if (canListen() && server->isStarted()) { // Process server requests
                server->addInFlightQuery(nodeAddr, uuid);
                try {
                    server->onMessageReceived(node, packet, state);
                    server->removeInFlightQuery(nodeAddr, uuid);
                } catch (...) { // clean up on error
                    server->removeInFlightQuery(nodeAddr, uuid);
                }
            }

            // Done with request, process DoS and release node
            checkDoS(state, node);
            releaseNode(node);

        } catch (...) {
            ERR() << strprintf("xrouter query from %s processed with error: ", node->GetAddrName());
            checkDoS(state, node);
            releaseNode(node);
        }
    });
}

//*****************************************************************************
//*****************************************************************************
std::string App::xrouterCall(enum XRouterCommand command, std::string & uuidRet, const std::string & fqServiceName,
                             const int & confirmations, const std::vector<std::string> & params)
{
    const std::string & uuid = generateUUID();
    uuidRet = uuid; // set uuid
    std::map<std::string, std::string> feePaymentTxs;
    std::vector<CNode*> selectedNodes;
    std::vector<std::pair<std::string, int> > nodeErrors;

    try {
        if (!isEnabled() || !isReady())
            throw XRouterError("XRouter is turned off. Please set 'xrouter=1' in blocknet.conf", xrouter::UNAUTHORIZED);

        std::string cleaned;
        if (!removeNamespace(fqServiceName, cleaned))
            throw XRouterError("Bad service name: " + fqServiceName, xrouter::INVALID_PARAMETERS);

        const auto & service = cleaned;

        if (command != xrService) {
            // Check param1
            switch (command) {
                case xrGetBlockHash:
                    if (params.size() > 0 && !is_number(params[0]))
                        throw XRouterError("Incorrect block number: " + params[0], xrouter::INVALID_PARAMETERS);
                    break;
                case xrGetTxBloomFilter:
                    if (params.size() > 0 && (!is_hash(params[0]) || (params[0].size() % 10 != 0)))
                        throw XRouterError("Incorrect bloom filter: " + params[0], xrouter::INVALID_PARAMETERS);
                    break;
                case xrGetBlock:
                case xrGetTransaction:
                    if (params.size() > 0 && !is_hash(params[0]))
                        throw XRouterError("Incorrect hash: " + params[0], xrouter::INVALID_PARAMETERS);
                    break;
                case xrGetBlocks:
                case xrGetTransactions: {
                    if (params.empty())
                        throw XRouterError("Missing parameters for " + fqServiceName, xrouter::INVALID_PARAMETERS);
                    for (const auto & p : params) {
                        if (!is_hash(p))
                            throw XRouterError("Incorrect hash " + p + " for " + fqServiceName, xrouter::INVALID_PARAMETERS);
                    }
                    break;
                }
                default:
                    break;
            }

            // Check param2
            switch (command) {
                case xrGetTxBloomFilter:
                    if (params.size() > 1 && !is_number(params[1]))
                        throw XRouterError("Incorrect block number: " + params[1], xrouter::INVALID_PARAMETERS);
                    break;
                default:
                    break;
            }
        }

        auto confs = xrsettings->confirmations(command, service, confirmations); // Confirmations
        const auto & commandStr = XRouterCommand_ToString(command);
        const auto & fqService = (command == xrService) ? pluginCommandKey(service) // plugin
                                                        : walletCommandKey(service, commandStr); // spv wallet

        // Open connections (at least number equal to how many confirmations we want)
        std::vector<sn::ServiceNode> nonWalletSnodes;
        uint32_t found{0};
        if (!openConnections(command, service, confs, params.size(), {}, nonWalletSnodes, found)) {
            std::string err("Failed to find " + std::to_string(confs) + " service node(s) supporting " + fqService +
                            " with config limits, found " + std::to_string(found));
            throw XRouterError(err, xrouter::NOT_ENOUGH_NODES);
        }

        // Reselect available nodes
        selectedNodes = availableNodesRetained(command, service, params.size(), confs);
        const auto & selected = static_cast<int>(selectedNodes.size());

        // Check if we have enough nodes
        if (selected + nonWalletSnodes.size() < confs)
            throw XRouterError("Failed to find " + std::to_string(confs) + " service node(s) supporting " +
                               fqService + " with config limits, found " + std::to_string(selected), xrouter::NOT_ENOUGH_NODES);

        std::vector<sn::ServiceNode> queryNodes;
        int snodeCount = 0;
        std::string fundErr{"Could not create payments. Please check that your wallet "
                            "is fully unlocked and you have at least " + std::to_string(confs) +
                            " available unspent transaction."};

        // helper container to use with node lookups on addr name
        std::map<NodeAddr, CNode*> mapSelectedNodes;
        for (auto & pnode : selectedNodes)
            mapSelectedNodes[pnode->GetAddrName()] = pnode;
        // helper container to use with node lookups on addr name
        std::map<NodeAddr, sn::ServiceNode> mapSelectedSnodes;
        for (auto & pnode : selectedNodes)
            mapSelectedSnodes[pnode->GetAddrName()] = sn::ServiceNodeMgr::instance().getSn(pnode->GetAddrName());
        for (auto & snode : nonWalletSnodes)
            mapSelectedSnodes[snode.getHostAddr().ToStringIPPort()] = snode;

        // Compose a final list of snodes to request. selectedNodes here should be sorted
        // ascending best to worst
        for (auto & item : mapSelectedSnodes) {
            const auto & addr = item.first;
            if (!hasConfig(addr))
                continue; // skip nodes that do not have configs

            auto config = getConfig(addr);

            // Create the fee payment
            CAmount fee = to_amount(config->commandFee(command, service));
            if (fee > 0) {
                try {
                    const auto paymentAddress = config->paymentAddress(command, service);
                    std::string feePayment;
                    if (!generatePayment(addr, paymentAddress, fee, feePayment))
                        throw XRouterError(fundErr, xrouter::INSUFFICIENT_FUNDS);
                    if (!feePayment.empty()) // record fee if it's not empty
                        feePaymentTxs[addr] = feePayment;
                } catch (XRouterError & e) {
                    ERR() << "Failed to create payment to node " << addr << " " << e.msg;
                    nodeErrors.emplace_back(e.msg, e.code);
                    continue;
                }
            }

            queryNodes.push_back(item.second);
            ++snodeCount;
            if (snodeCount == confs)
                break;
        }

        // Do we have enough snodes? If not unlock utxos
        if (snodeCount < confs) {
            const auto msg = strprintf("Found %u service node(s), however, %u meet your requirements. %u service "
                                       "node(s) are required to process the request", selected, snodeCount, confs);
            throw XRouterError(msg, xrouter::NOT_ENOUGH_NODES);
        }

        const int timeout = xrsettings->commandTimeout(command, service);
        boost::thread_group tg;

        // Send xrouter request to each selected node
        for (auto & snode : queryNodes) {
            const std::string & addr = snode.getHost();
            std::string feetx;
            if (feePaymentTxs.count(addr))
                feetx = feePaymentTxs[addr];

            // Record the node sending request to
            addQuery(uuid, addr);
            queryMgr.addQuery(uuid, addr);

            if (mapSelectedNodes.count(addr)) { // query via the blocknet network
                auto pnode = mapSelectedNodes[addr];
                // Send packet to xrouter node
                XRouterPacket packet(command, uuid);
                packet.append(service);
                packet.append(feetx); // feetx
                packet.append(static_cast<uint32_t>(params.size()));
                for (const std::string & p : params)
                    packet.append(p);
                packet.sign(cpubkey, cprivkey);
                PushXRouterMessage(pnode, packet.body());
                updateSentRequest(addr, fqService);
            } else { // query via external ip specified in config
                // Set the fully qualified service url to the form /xr/BLOCK/xrGetBlockCount
                const auto & fqUrl = fqServiceToUrl((command == xrService) ? pluginCommandKey(service) // plugin
                                                       : walletCommandKey(service, commandStr, true)); // spv wallet
                try {
                    tg.create_thread([uuid,addr,snode,fqUrl,params,feetx,timeout,this]() {
                        RenameThread("blocknet-xrclientrequest");
                        if (ShutdownRequested())
                            return;
                        json_spirit::Array jparams;
                        for (const std::string & p : params)
                            jparams.push_back(p);

                        CKey clientKey; clientKey.Set(cprivkey.begin(), cprivkey.end(), true);
                        XRouterReply xrresponse;
                        try {
                            std::string data;
                            if (!jparams.empty())
                                data = json_spirit::write_string(Value(jparams), json_spirit::none, 8);
                            xrresponse = xrouter::CallXRouterUrl(snode.getHostAddr().ToStringIP(),
                                    snode.getHostAddr().GetPort(), fqUrl, data, timeout, clientKey,
                                    snode.getSnodePubKey(), feetx);
                        } catch (std::exception & e) {
                            return; // failed to connect
                        }

                        // Do not process if we aren't expecting a result. Also prevent reply malleability (only first reply is accepted)
                        if (!queryMgr.hasQuery(uuid, addr) || queryMgr.hasReply(uuid, addr))
                            return; // done, nothing found

                        // Verify servicenode response
                        CHashWriter hw(SER_GETHASH, 0);
                        hw << std::vector<unsigned char>(xrresponse.result.begin(), xrresponse.result.end());
                        const auto hash = hw.GetHash();
                        CPubKey sigPubKey;
                        if (snode.getSnodePubKey() != xrresponse.hdrpubkey
                        || !sigPubKey.RecoverCompact(hash, xrresponse.hdrsignature)
                        || snode.getSnodePubKey() != sigPubKey) {
                            json_spirit::Object obj;
                            obj.emplace_back("error", "Unable to verify if the service node is valid. Received bad signature on this request.");
                            obj.emplace_back("code", xrouter::Error::BAD_SIGNATURE);
                            obj.emplace_back("reply", xrresponse.result);
                            queryMgr.addReply(uuid, addr, json_spirit::write_string(Value(obj)));
                            queryMgr.purge(uuid, addr);
                            return;
                        }

                        // Store the reply
                        queryMgr.addReply(uuid, addr, xrresponse.result);
                        queryMgr.purge(uuid, addr);
                    });
                } catch (...) { }
                updateSentRequest(addr, fqService);
            }
            LOG() << "Sent command " << fqService << " query " << uuid << " to node " << addr;
        }

        // At this point we need to wait for responses
        int confirmation_count = 0;
        auto queryCheckStart = GetAdjustedTime();
        auto queries = queryMgr.allLocks(uuid);
        std::vector<NodeAddr> review;
        for (auto & query : queries)
            review.push_back(query.first);

        // Check that all replies have arrived, only run as long as timeout
        while (!ShutdownRequested() && confirmation_count < confs
            && GetAdjustedTime() - queryCheckStart < timeout)
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(5));
            for (int i = review.size() - 1; i >= 0; --i)
                if (queryMgr.hasReply(uuid, review[i])) {
                    ++confirmation_count;
                    review.erase(review.begin()+i);
                }
        }

        // Clean up
        queryMgr.purge(uuid);

        std::set<NodeAddr> failed;

        if (confirmation_count < confs) {
            failed.insert(review.begin(), review.end());

            auto snodes = getServiceNodes();
            std::map<NodeAddr, sn::ServiceNode*> snodec;
            for (auto & s : snodes) {
                if (!s.getHost().empty())
                    snodec[s.getHost()] = &s;
            }

            // Penalize the snodes that didn't respond
            for (const auto & addr : review) {
                if (!snodec.count(addr))
                    continue;
                updateScore(addr, -25);
            }

            std::set<std::string> snodeAddresses;
            for (const auto & addr : review) {
                if (!snodec.count(addr))
                    continue;
                auto s = snodec[addr];
                snodeAddresses.insert(EncodeDestination(CTxDestination(s->getPaymentAddress())));
            }

            // Unlock failed txs
            for (const auto & addr : failed) { // unlock any fee txs
                const auto & tx = feePaymentTxs[addr];
                unlockOutputs(tx);
            }

            const auto & nodes = boost::algorithm::join(snodeAddresses, ",");
            ERR() << "Failed to get response in time for query " << uuid << " Nodes failed to respond, penalizing: " << nodes;
        }

        // Handle the results
        std::map<NodeAddr, std::string> replies;
        std::set<NodeAddr> diff;
        std::set<NodeAddr> agree;
        std::string rawResult;
        int c = queryMgr.mostCommonReply(uuid, rawResult, replies, agree, diff);
        for (const auto & addr : diff) // penalize nodes that didn't match consensus
            updateScore(addr, -5);
        if (c > 1) { // only update score if there's consensus
            for (const auto & addr : agree) {
                if (!failed.count(addr))
                    updateScore(addr, c > 1 ? c * 2 : 0); // boost majority consensus nodes
            }
        }

        // Check for errors
        for (const auto & rp : replies) {
            try {
                Value resultVal; read_string(rp.second, resultVal);
                if (resultVal.type() == obj_type) {
                    const auto & err_code = find_value(resultVal.get_obj(), "code");
                    const auto & rv = find_value(resultVal.get_obj(), "result");
                    if (err_code.type() == int_type && err_code.get_int() == INTERNAL_SERVER_ERROR)
                        updateScore(rp.first, -2); // penalize server errors
                    else if (rv.type() == obj_type) {
                        const auto & err_code = find_value(rv.get_obj(), "code");
                        if (err_code.type() == int_type && err_code.get_int() == INTERNAL_SERVER_ERROR)
                            updateScore(rp.first, -2); // penalize server errors
                    }
                }
            } catch (...) { } // do not report on non-error objs
        }

        // Show all replies in the response (along with the majority consensus reply)
        if (replies.size() > 1) {
            Object r;

            // By default we parse the {"result": } field of any response. XRouter integrations will
            // ideally return any json responses in a result field. If not we return the raw response
            // in json object form if possible, or a raw string.
            Value resultVal; read_string(rawResult, resultVal);
            if (resultVal.type() == obj_type) {
                const Value & rv = find_value(resultVal.get_obj(), "result");
                if (rv.type() == null_type)
                    r.emplace_back("result", resultVal);
                else
                    r.emplace_back("result", rv);
            } else
                r.emplace_back("result", resultVal);

            Array allr;
            for (const auto & item : replies) {
                const auto & nodeAddr = item.first;
                const auto & reply = item.second;

                Object ar;
                std::vector<unsigned char> spubkey; servicenodePubKey(nodeAddr, spubkey);
                ar.emplace_back("nodepubkey", HexStr(spubkey));
                ar.emplace_back("score", getScore(nodeAddr));

                Value replyVal; read_string(reply, replyVal);
                ar.emplace_back("reply", replyVal);

                allr.push_back(ar);
            }
            r.emplace_back("allreplies", allr);

            rawResult = json_spirit::write_string(Value(r));
        }

        // Unlock any utxos associated with replies that returned an error
        if (!feePaymentTxs.empty()) {
            for (const auto & item : replies) {
                const auto & nodeAddr = item.first;
                const auto & reply = item.second;
                Value replyVal; read_string(reply, replyVal);
                if (replyVal.type() == obj_type) {
                    const auto & err = find_value(replyVal.get_obj(), "error");
                    if (err.type() != null_type) {
                        const auto & tx = feePaymentTxs[nodeAddr];
                        unlockOutputs(tx);
                    }
                }
            }
        }

        releaseNodes(selectedNodes);
        return rawResult;

    } catch (XRouterError & e) {
        LOG() << e.msg;

        for (const auto & item : feePaymentTxs) { // unlock any fee txs
            const std::string & tx = item.second;
            unlockOutputs(tx);
        }

        std::string errmsg = e.msg;
        if (!nodeErrors.empty()) {
            for (int i = 0; i < nodeErrors.size(); ++i) {
                const auto & em = nodeErrors[i];
                errmsg += strprintf(" | %s code %u", em.first, em.second);
            }
        }

        Object error;
        error.emplace_back(Pair("error", errmsg));
        error.emplace_back(Pair("code", e.code));
        error.emplace_back(Pair("uuid", uuid));

        releaseNodes(selectedNodes);
        return json_spirit::write_string(Value(error), true);

    } catch (std::exception & e) {
        LOG() << e.what();

        for (const auto & item : feePaymentTxs) { // unlock any fee txs
            const std::string & tx = item.second;
            unlockOutputs(tx);
        }

        Object error;
        error.emplace_back(Pair("error", "Internal Server Error"));
        error.emplace_back(Pair("code", INTERNAL_SERVER_ERROR));
        error.emplace_back(Pair("uuid", uuid));

        releaseNodes(selectedNodes);
        return json_spirit::write_string(Value(error), true);
    }
}

std::string App::getBlockCount(std::string & uuidRet, const std::string & currency, const int & confirmations)
{
    return this->xrouterCall(xrGetBlockCount, uuidRet, currency, confirmations, {});
}

std::string App::getBlockHash(std::string & uuidRet, const std::string & currency, const int & confirmations, const unsigned int & block)
{
    return this->xrouterCall(xrGetBlockHash, uuidRet, currency, confirmations, { std::to_string(block) });
}

std::string App::getBlock(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & blockHash)
{
    return this->xrouterCall(xrGetBlock, uuidRet, currency, confirmations, { blockHash });
}

std::string App::getBlocks(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::vector<std::string> & blockHashes)
{
    return this->xrouterCall(xrGetBlocks, uuidRet, currency, confirmations, { blockHashes.begin(), blockHashes.end() });
}

std::string App::getTransaction(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & hash)
{
    return this->xrouterCall(xrGetTransaction, uuidRet, currency, confirmations, { hash });
}

std::string App::getTransactions(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::vector<std::string> & txs)
{
    return this->xrouterCall(xrGetTransactions, uuidRet, currency, confirmations, std::vector<std::string>(txs.begin(), txs.end()));
}

std::string App::decodeRawTransaction(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & rawtx)
{
    return this->xrouterCall(xrDecodeRawTransaction, uuidRet, currency, confirmations, { rawtx });
}

std::string App::getTransactionsBloomFilter(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & filter, const int & number)
{
    return this->xrouterCall(xrGetTxBloomFilter, uuidRet, currency, confirmations, { filter, std::to_string(number) });
}

std::string App::sendTransaction(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & transaction)
{
    return this->xrouterCall(xrSendTransaction, uuidRet, currency, confirmations, { transaction });
}

std::string App::convertTimeToBlockCount(std::string & uuidRet, const std::string & currency, const int & confirmations, const int64_t & time)
{
    return this->xrouterCall(xrGetBlockAtTime, uuidRet, currency, confirmations, { std::to_string(time) });
}

std::string App::getBalance(std::string & uuidRet, const std::string & currency, const int & confirmations, const std::string & address)
{
    return this->xrouterCall(xrGetBalance, uuidRet, currency, confirmations, { address });
}

std::string App::getReply(const std::string & id)
{
    auto replies = queryMgr.allReplies(id);
    std::vector<sn::ServiceNode> snodes = getServiceNodes();
    std::map<NodeAddr, sn::ServiceNode*> snodec;
    for (auto & s : snodes) {
        if (s.getHost().empty())
            continue;
        const auto & snodeAddr = s.getHost();
        snodec[snodeAddr] = &s;
    }

    if (replies.empty()) {
        Object result;
        result.emplace_back("error", "No replies found");
        result.emplace_back("code", xrouter::NO_REPLIES);
        return json_spirit::write_string(Value(result), json_spirit::pretty_print);
    }

    if (replies.size() == 1) {
        return replies.begin()->second;
    } else {
        Array arr;
        for (const auto & it : replies) {
            const auto & reply = it.second;
            const auto & snodeAddr = it.first;
            const auto & s = snodec[snodeAddr];

            Object o;
            try {
                Value reply_val; read_string(reply, reply_val);
                if (reply_val.type() == obj_type)
                    o = reply_val.get_obj();
                else
                    o.emplace_back("result", reply_val);
            } catch (...) {
                o = Object();
                o.emplace_back("result", "");
            }

            std::vector<unsigned char> spubkey;
            servicenodePubKey(snodeAddr, spubkey);

            o.emplace_back("nodepubkey", HexStr(spubkey));
            o.emplace_back("score", getScore(snodeAddr));
            o.emplace_back("address", EncodeDestination(CTxDestination(s->getPaymentAddress())));
            arr.emplace_back(o);
        }
        return json_spirit::write_string(Value(arr), json_spirit::pretty_print);
    }

}

bool App::generatePayment(const NodeAddr & nodeAddr, const std::string & paymentAddress,
        const CAmount & fee, std::string & payment)
{
    if (!hasConfig(nodeAddr))
        throw std::runtime_error("No config found for servicenode: " + nodeAddr);

    if (fee <= 0)
        return true;

    // Get payment address from snode list if the default is empty
    std::string snodeAddress = paymentAddress;
    if (snodeAddress.empty()) {
        if (!getPaymentAddress(nodeAddr, snodeAddress))
            return false;
    }

    bool res = createAndSignTransaction(snodeAddress, fee, payment);
    if (!res)
        return false;

    return true;
}

bool App::getPaymentAddress(const NodeAddr & nodeAddr, std::string & paymentAddress)
{
    // Payment address = pubkey Collateral address of snode
    std::vector<sn::ServiceNode> snodes = getServiceNodes();
    for (sn::ServiceNode & s : snodes) {
        if (s.getHost() == nodeAddr) {
            paymentAddress = EncodeDestination(CTxDestination(s.getPaymentAddress()));
            return true;
        }
    }
    
    return false;
}

CPubKey App::getPaymentPubkey(CNode* node)
{
    // Payment address = pubkey Collateral address of snode
    std::vector<sn::ServiceNode> vServicenodeRanks = getServiceNodes();
    for (sn::ServiceNode & s : vServicenodeRanks) {
        if (s.getHost() == node->GetAddrName()) {
            return s.getSnodePubKey();
        }
    }
    
    return CPubKey();
}

std::map<NodeAddr, XRouterSettingsPtr> App::xrConnect(const std::string & fqService, const int & count, uint32_t & foundCount) {
    std::map<NodeAddr, XRouterSettingsPtr> selectedConfigs;
    std::vector<std::string> nparts;
    if (!xrsplit(fqService, xrdelimiter, nparts))
        throw XRouterError("Bad service name, acceptable characters [a-z A-Z 0-9 $], sample format: xrs::ExampleServiceName123", BAD_REQUEST);

    const auto msg = "Missing top-level namespace (xr:: or xrs::) Example xr::BLOCK or xrs::CustomServiceName";
    if (nparts.size() <= 1)
        throw XRouterError(msg, BAD_REQUEST);

    std::string ns = nparts[0];
    if (ns != xr && ns != xrs)
        throw XRouterError(msg, BAD_REQUEST);

    XRouterCommand command = ns == xrs ? xrService
                                       : (nparts.size() == 2 ? xrGetBlockCount : XRouterCommand_FromString(nparts.back()));
    std::string commandStr = XRouterCommand_ToString(command);

    if (command == xrInvalid)
        throw XRouterError("Unknown xr:: command", BAD_REQUEST);

    const std::string wallet = nparts[1];
    const std::string plugin = boost::algorithm::join(std::vector<std::string>{nparts.begin()+1, nparts.end()}, xrdelimiter);
    const std::string service = command != xrService ? wallet : plugin;

    std::vector<sn::ServiceNode> nonWalletSnodes;
    uint32_t found{0};
    openConnections(command, service, count, -1, { }, nonWalletSnodes, found); // open connections to snodes that have our service

    const auto configs = getNodeConfigs(); // get configs and store matching ones
    for (const auto & item : configs) {
        const auto & snode = sn::ServiceNodeMgr::instance().getSn(item.first);
        if (g_banman->IsBanned(static_cast<CNetAddr>(snode.getHostAddr()))) // exclude banned
            continue;
        if (command != xrService && item.second->hasWallet(service)) {
            selectedConfigs.insert(item);
            continue;
        }
        if (item.second->hasPlugin(service))
            selectedConfigs.insert(item);
    }

    foundCount = static_cast<uint32_t>(selectedConfigs.size());
    return selectedConfigs;
}

void App::snodeConfigJSON(const std::map<NodeAddr, XRouterSettingsPtr> & configs, json_spirit::Array & data) {
    if (configs.empty()) // no configs
        return;

    for (const auto & item : configs) { // Iterate over all node configs
        if (item.second == nullptr)
            continue;

        Object o;

        // pubkey
        std::vector<unsigned char> spubkey;
        servicenodePubKey(item.second->getNode(), spubkey);
        o.emplace_back("nodepubkey", HexStr(spubkey));

        // score
        o.emplace_back("score", getScore(item.first));
        // banned
        o.emplace_back("banned", g_banman->IsBanned(item.second->getAddr()));
        // payment address
        o.emplace_back("paymentaddress", item.second->paymentAddress(xrGetConfig));

        // wallets
        const auto & wallets = item.second->getWallets();
        o.emplace_back("spvwallets", Array(wallets.begin(), wallets.end()));

        // wallet configs
        Array wc;
        for (const auto & w : wallets) {
            Object wlg;
            wlg.emplace_back("spvwallet", w);
            Array cmds;
            const auto xrcommands = XRouterCommands();
            for (const auto & cmd : xrcommands) {
                Object co;
                co.emplace_back("command", XRouterCommand_ToString(cmd));
                co.emplace_back("fee", item.second->commandFee(cmd, w));
                co.emplace_back("paymentaddress", item.second->paymentAddress(cmd, w));
                co.emplace_back("requestlimit", item.second->clientRequestLimit(cmd, w));
                co.emplace_back("fetchlimit", item.second->commandFetchLimit(cmd, w));
                co.emplace_back("timeout", item.second->commandTimeout(cmd, w));
                co.emplace_back("disabled", !item.second->isAvailableCommand(cmd, w));
                cmds.push_back(co);
            }
            wlg.emplace_back("commands", cmds);
            wc.push_back(wlg);
        }
        o.emplace_back("spvconfigs", wc);

        // fees
        o.emplace_back("feedefault", item.second->defaultFee());
        Object ofs;
        const auto & schedule = item.second->feeSchedule();
        for (const auto & s : schedule)
            ofs.emplace_back(s.first,  s.second);
        o.emplace_back("fees", ofs);

        // plugins
        Object plugins;
        for (const auto & plugin : item.second->getPlugins()) {
            auto pls = item.second->getPluginSettings(plugin);
            if (pls) {
                Object plg;
                plg.emplace_back("parameters", boost::algorithm::join(pls->parameters(), ","));
                plg.emplace_back("fee", item.second->commandFee(xrService, plugin));
                plg.emplace_back("paymentaddress", item.second->paymentAddress(xrService, plugin));
                plg.emplace_back("requestlimit", item.second->clientRequestLimit(xrService, plugin));
                plg.emplace_back("fetchlimit", item.second->commandFetchLimit(xrService, plugin));
                plg.emplace_back("timeout", item.second->commandTimeout(xrService, plugin));
                plg.emplace_back("disabled", !item.second->isAvailableCommand(xrService, plugin));
                plugins.emplace_back(plugin, plg);
            }
        }
        o.emplace_back("services", plugins);

        data.emplace_back(o);
    }
}

std::string App::sendXRouterConfigRequest(CNode* node, const std::string & addr) {
    const auto & uuid = generateUUID();
    const auto & nodeAddr = node->GetAddrName();
    addQuery(uuid, nodeAddr);
    queryMgr.addQuery(uuid, nodeAddr);

    XRouterPacket packet(xrGetConfig, uuid);
    packet.append(addr);
    packet.sign(cpubkey, cprivkey);
    PushXRouterMessage(node, packet.body());

    return uuid;
}

std::string App::sendXRouterConfigRequestSync(CNode* node) {
    const auto & uuid = generateUUID();
    const auto & nodeAddr = node->GetAddrName();
    addQuery(uuid, nodeAddr);
    queryMgr.addQuery(uuid, nodeAddr);

    XRouterPacket packet(xrGetConfig, uuid);
    packet.sign(cpubkey, cprivkey);
    PushXRouterMessage(node, packet.body());

    // Wait for response
    auto qcond = queryMgr.queryCond(uuid, nodeAddr);
    if (qcond) {
        int timeout = xrsettings->configSyncTimeout();
        auto l = queryMgr.queryLock(uuid, nodeAddr);
        boost::mutex::scoped_lock lock(*l);
        if (!qcond->timed_wait(lock, boost::posix_time::seconds(timeout),
                [this,&uuid,&nodeAddr]() { return ShutdownRequested() || queryMgr.hasReply(uuid, nodeAddr); }))
        {
            queryMgr.purge(uuid); // clean up
            return "Could not get XRouter config";
        }
    }

    std::string reply;
    queryMgr.reply(uuid, nodeAddr, reply);

    return reply;
}

void App::reloadConfigs() {
    LOG() << "Reloading xrouter config from file " << xrouterpath.string();
    xrsettings->init(xrouterpath);
    createConnectors();
}

std::string App::getStatus() {
    Object result;

    if (sn::ServiceNodeMgr::instance().hasActiveSn()) {
        const auto & entry = sn::ServiceNodeMgr::instance().getActiveSn();
        const auto & snode = sn::ServiceNodeMgr::instance().getSn(entry.key.GetPubKey());
        std::map<NodeAddr, XRouterSettingsPtr> my;
        my[snode.getHost()] = xrsettings;

        Array data;
        snodeConfigJSON(my, data);

        if (data.empty() || data.size() > 1)
            return "";

        result = data[0].get_obj();
    }

    result.emplace_back("xrouter", isEnabled());
    result.emplace_back("servicenode", sn::ServiceNodeMgr::instance().hasActiveSn());
    result.emplace_back("config", xrsettings->rawText());

    Object plugins;
    for (const auto & p : xrsettings->getPlugins()) {
        auto pp = xrsettings->getPluginSettings(p);
        if (pp)
            plugins.emplace_back(p, pp->rawText());
    }
    result.emplace_back("plugins", plugins);

    return json_spirit::write_string(Value(result), json_spirit::pretty_print, 8);
}

void App::getLatestNodeContainers(std::vector<sn::ServiceNode> & snodes, std::vector<CNode*> & nodes,
                                  std::map<NodeAddr, sn::ServiceNode> & snodec, std::map<NodeAddr, CNode*> & nodec)
{
    snodes.clear(); nodes.clear(); snodec.clear(); nodec.clear();

    snodes = getServiceNodes();
    nodes = CopyNodes();
    auto & smgr = sn::ServiceNodeMgr::instance();

    // Build snode cache
    for (sn::ServiceNode & s : snodes) {
        const auto & snodeAddr = s.getHost();
        if (!snodeAddr.empty() && !g_banman->IsBanned(s.getHostAddr())
            && !(smgr.hasActiveSn() && smgr.getSn(smgr.getActiveSn().key.GetPubKey()).getHost() == snodeAddr)) // skip banned snodes and self
            snodec[snodeAddr] = s;
    }

    // Build node cache
    for (auto & pnode : nodes) {
        const auto & addr = pnode->GetAddrName();
        if (!addr.empty())
            nodec[addr] = pnode;
    }
}

void App::runTests() {
    server->runPerformanceTests();
}

std::string App::getMyPaymentAddress() {
    return server->getMyPaymentAddress();
}

bool App::servicenodePubKey(const NodeAddr & node, std::vector<unsigned char> & pubkey)  {
    std::vector<sn::ServiceNode> vServicenodes = getServiceNodes();
    for (const auto & snode : vServicenodes) {
        if (node != snode.getHost())
            continue;
        auto key = snode.getSnodePubKey();
        if (!key.IsCompressed() && !key.Compress())
            return false;
        pubkey = std::vector<unsigned char>{key.begin(), key.end()};
        return true;
    }
    return false;
}

void App::checkDoS(CValidationState & state, CNode *pnode) {
    int dos = 0;
    if (state.IsInvalid(dos)) {
        LogPrint(BCLog::XROUTER, "invalid xrouter packet from peer=%d %s : %s\n", pnode->GetId(), pnode->cleanSubVer,
                 state.GetRejectReason());
        if (dos > 0) {
            LOCK(cs_main);
            Misbehaving(pnode->GetId(), dos);
        }
    } else if (state.IsError()) {
        LogPrint(BCLog::XROUTER, "xrouter packet from peer=%d %s processed with error: %s\n", pnode->GetId(), pnode->cleanSubVer,
                 state.GetRejectReason());
    }
};

void App::updateScore(const NodeAddr & node, const int score) {
    LOCK(mu);
    if (!snodeScore.count(node))
        snodeScore[node] = 0;
    snodeScore[node] += score;
    const auto scr = snodeScore[node];
    int banscore = gArgs.GetArg("-xrouterbanscore", -200);
    if (scr <= banscore) {
        g_connman->ForEachNode([&node,scr,this](CNode *pnode) {
            if (node == pnode->GetAddrName()) {
                LOG() << strprintf("Banning XRouter Node %s because score is too low: %i", node, scr);
                snodeScore[node] = -30; // default score when ban expires
                LOCK(cs_main);
                Misbehaving(pnode->GetId(), 100);
            }
        });
    }
}

} // namespace xrouter
