#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/signals2.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/copy.hpp>

#include <stdio.h>
#include <atomic>
#include <numeric>
#include <math.h>
#include "util/settings.h"
#include "util/logger.h"
#include "util/xbridgeerror.h"
#include "util/xutil.h"
#include "xbridgeapp.h"
#include "xbridgeexchange.h"
#include "xbridgetransaction.h"
#include "xbridgetransactiondescr.h"
#include "xuiconnector.h"
#include "rpcserver.h"

using namespace json_spirit;
using namespace std;
using namespace boost;
using namespace boost::asio;

using TransactionMap    = std::map<uint256, xbridge::TransactionDescrPtr>;

using TransactionPair   = std::pair<uint256, xbridge::TransactionDescrPtr>;

using RealVector        = std::vector<double>;

using TransactionVector = std::vector<xbridge::TransactionDescrPtr>;
namespace bpt           = boost::posix_time;



//******************************************************************************
//******************************************************************************
Value dxGetLocalTokens(const Array & params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetLocalTokens\nList currencies supported by the wallet.");

    }
    if (params.size() > 0) {

        Object error;
        error.emplace_back(Pair("error",
                                "This function does not accept any parameter"));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        return  error;

    }

    Array r;

    std::vector<std::string> currencies = xbridge::App::instance().availableCurrencies();
    for (std::string currency : currencies) {

        r.emplace_back(currency);

    }
    return r;
}

//******************************************************************************
//******************************************************************************
Value dxGetNetworkTokens(const Array & params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetNetworkTokens\nList coins supported by the network.");

    }
    if (params.size() > 0) {

        Object error;
        error.emplace_back(Pair("error",
                                "This function does not accept any parameters"));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        return  error;

    }

    Array r;

    std::vector<std::string> currencies = xbridge::App::instance().networkCurrencies();
    for (std::string currency : currencies) {

        r.emplace_back(currency);

    }
    return r;
}

//******************************************************************************
//******************************************************************************

/** \brief Returns the list of open and pending transactions
  * \param params A list of input params.
  * \param fHelp For debug purposes, throw the exception describing parameters.
  * \return A list of open(they go first) and pending transactions.
  *
  * Returns the list of open and pending transactions as JSON structures.
  * The open transactions go first.
  */

Value dxGetOrders(const Array & params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetOrders returns the list of open orders.");

    }
    if (!params.empty()) {

        Object error;
        const auto statusCode = xbridge::INVALID_PARAMETERS;
        error.emplace_back(Pair("error",
                                xbridge::xbridgeErrorText(statusCode, "This function does not accept parameters")));
        error.emplace_back(Pair("code", statusCode));
        error.emplace_back(Pair("name", __FUNCTION__));
        return  error;

    }

    auto &xapp = xbridge::App::instance();
    TransactionMap trlist = xapp.transactions();

    Array result;
    for (const auto& trEntry : trlist) {

        const auto &tr = trEntry.second;

        xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(tr->fromCurrency);
        xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(tr->toCurrency);
        if (!connFrom || !connTo) {

            continue;

        }

        Object jtr;
        jtr.emplace_back(Pair("id",             tr->id.GetHex()));
        jtr.emplace_back(Pair("maker",          tr->fromCurrency));
        jtr.emplace_back(Pair("maker_size",     util::xBridgeValueFromAmount(tr->fromAmount)));
        jtr.emplace_back(Pair("taker",          tr->toCurrency));
        jtr.emplace_back(Pair("taker_size",     util::xBridgeValueFromAmount(tr->toAmount)));
        jtr.emplace_back(Pair("updated_at",     util::iso8601(tr->txtime)));
        jtr.emplace_back(Pair("created_at",     util::iso8601(tr->created)));
        jtr.emplace_back(Pair("status",         tr->strState()));
        result.emplace_back(jtr);

    }


    return result;
}

//*****************************************************************************
//*****************************************************************************

Value dxGetOrderFills(const Array & params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetOrderFills Returns all the recent trades by trade pair that have been filled \n"
                            "(i.e. completed). Maker symbol is always listed first. The [combined] flag defaults \n"
                            "to true. When set to false [combined] will return only maker trades, switch maker \n"
                            "and taker to get the reverse.\n"
                            "(maker) (taker) [optional](combined, default = true)"
                            );

    }

    bool invalidParams = ((params.size() != 2) &&
                          (params.size() != 3));
    if (invalidParams) {

        Object error;
        const auto statusCode = xbridge::INVALID_PARAMETERS;
        error.emplace_back(Pair("error",
                                xbridge::xbridgeErrorText(statusCode)));
        error.emplace_back(Pair("code", statusCode));
        error.emplace_back(Pair("name", __FUNCTION__ ));
        return  error;

    }

    bool combined = params.size() == 3 ? params[2].get_bool() : true;

    const auto maker = params[0].get_str();
    const auto taker = params[1].get_str();



    TransactionMap history = xbridge::App::instance().history();



    TransactionVector result;

    for (auto &item : history) {
        const xbridge::TransactionDescrPtr &ptr = item.second;
        if ((ptr->state == xbridge::TransactionDescr::trFinished) &&
            (combined ? (ptr->fromCurrency == maker && ptr->toCurrency == taker) : (ptr->fromCurrency == maker))) {
            result.push_back(ptr);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const xbridge::TransactionDescrPtr &a,  const xbridge::TransactionDescrPtr &b)
    {
         return (a->txtime) > (b->txtime);
    });

    Array arr;
    for(const auto &transaction : result) {

        Object tmp;
        tmp.emplace_back(Pair("id",         transaction->id.GetHex()));
        tmp.emplace_back(Pair("time",       util::iso8601(transaction->txtime)));
        tmp.emplace_back(Pair("maker",      transaction->fromCurrency));
        tmp.emplace_back(Pair("maker_size", util::xBridgeValueFromAmount(transaction->fromAmount)));
        tmp.emplace_back(Pair("taker",      transaction->toCurrency));
        tmp.emplace_back(Pair("taker_size", util::xBridgeValueFromAmount(transaction->toAmount)));
        arr.emplace_back(tmp);

    }
    return arr;
}

Value dxGetOrderHistory(const json_spirit::Array& params, bool fHelp)
{

    if (fHelp) {

        throw runtime_error("dxGetOrderHistory "
                            "(maker) (taker) (start time) (end time) (granularity) [optional](order_ids, default = false) ");

    }
    if (params.size() < 5) {

        Object error;
        error.emplace_back(Pair("error",
                                "Invalid number of parameters"));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        return  error;

    }

    Array arr;
    TransactionMap history = xbridge::App::instance().history();

    if(history.empty()) {

        LOG() << "empty history transactions list";
        return arr;

    }

    const auto fromCurrency     = params[0].get_str();
    const auto toCurrency       = params[1].get_str();
    const auto startTimeFrame   = params[2].get_int();
    const auto endTimeFrame     = params[3].get_int();
    const auto granularity      = params[4].get_int();

    // Validate granularity
    switch (granularity) {
        case 60:
        case 300:
        case 900:
        case 3600:
        case 21600:
        case 86400:
            break;
        default:
            Object error;
            error.emplace_back(Pair("error", "granularity must be one of: 60,300,900,3600,21600,86400"));
            error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
            return  error;
    }

    bool isShowTxids = params.size() == 6 ? params[5].get_bool() : false;

    TransactionMap trList;
    std::vector<xbridge::TransactionDescrPtr> trVector;

    //copy all transactions between startTimeFrame and endTimeFrame
    std::copy_if(history.begin(), history.end(), std::inserter(trList, trList.end()),
                 [&startTimeFrame, &endTimeFrame, &toCurrency, &fromCurrency](const TransactionPair &transaction){
        return  ((transaction.second->created)      <   bpt::from_time_t(endTimeFrame)) &&
                ((transaction.second->created)      >   bpt::from_time_t(startTimeFrame)) &&
                (transaction.second->toCurrency     ==  toCurrency) &&
                (transaction.second->fromCurrency   ==  fromCurrency) &&
                (transaction.second->state          ==  xbridge::TransactionDescr::trFinished);
    });

    if(trList.empty()) {

        LOG() << "No orders for the specified period " << __FUNCTION__;
        return  arr;

    }

    //copy values into vector
    for (const auto &trEntry : trList) {
        const auto &tr = trEntry.second;
        trVector.push_back(tr);
    }

    // Sort ascending by updated time
    std::sort(trVector.begin(), trVector.end(),
              [](const xbridge::TransactionDescrPtr &a, const xbridge::TransactionDescrPtr &b)
    {
         return (a->txtime) < (b->txtime);
    });

    // Setup intervals. Each time period (interval) has a high,low,open,close,volume. This code is
    // responsible for calculating the results for each interval.
    int jstart = 0;
    for (int timeInterval = startTimeFrame; timeInterval < endTimeFrame; timeInterval += granularity) {
        Array interval;

        double volume = 0;
        const xbridge::TransactionDescrPtr open = trVector[0]; // first order in interval
        const xbridge::TransactionDescrPtr close = trVector[trVector.size()-1]; // last order in interval
        xbridge::TransactionDescrPtr high = nullptr;
        xbridge::TransactionDescrPtr low = nullptr;
        Array orderIds;

        // start searching from point of last checked order (since orders are only processed once)
        for (int j = jstart; j < trVector.size(); j++) {
            const auto &tr = trVector[j];
            uint64_t t = util::timeToInt(tr->txtime)/1000/1000; // need seconds, timeToInt is in microseconds
            // only check orders within boundaries (time interval)
            if (t >= timeInterval && t < timeInterval + granularity) {
                // volume is based in "to amount" (track volume of what we're priced in, in this case orders are priced in "to amount")
                volume += util::xBridgeValueFromAmount(high->toAmount);

                // defaults
                if (high == nullptr)
                    high = tr;
                if (low == nullptr)
                    low = tr;

                // calc prices, algo: to/from = price (in terms of to). e.g. LTC-SYS price = SYS-size / LTC-size = SYS per unit priced in LTC
                double high_price = util::price(high);
                double low_price = util::price(low);
                double current_price = util::price(tr);

                // record highest if current price larger than highest price
                if (current_price > high_price)
                    high = tr;
                // record lowest if current price is lower than lowest price
                if (current_price < low_price)
                    low = tr;

                // store order id if necessary
                if (isShowTxids)
                    orderIds.emplace_back(tr->id.GetHex());

                jstart = j; // want to skip already searched orders
            }
        }

        // latest prices
        double open_price = util::price(open);
        double close_price = util::price(close);
        double high_price = util::price(high);
        double low_price = util::price(low);

        // format: [ time, low, high, open, close, volume ]
        interval.emplace_back(util::iso8601(open->txtime));
        interval.emplace_back(low_price);
        interval.emplace_back(high_price);
        interval.emplace_back(open_price);
        interval.emplace_back(close_price);
        interval.emplace_back(volume);

        if (isShowTxids)
            interval.emplace_back(orderIds);

        arr.emplace_back(interval);
    }

    return arr;
}

//*****************************************************************************
//*****************************************************************************

Value dxGetOrder(const Array & params, bool fHelp)
{
    if (fHelp) {

         throw runtime_error("dxGetOrder (id) Get order info by id.	.");

    }
    if (params.size() != 1) {

        Object error;
        const auto statusCode = xbridge::INVALID_PARAMETERS;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        error.emplace_back(Pair("code",  statusCode));
        error.emplace_back(Pair("name",  __FUNCTION__));
        return  error;

    }

    uint256 id(params[0].get_str());

    auto &xapp = xbridge::App::instance();

    const xbridge::TransactionDescrPtr order = xapp.transaction(uint256(id));

    if(order == nullptr) {

        Object error;
        const auto statusCode = xbridge::Error::TRANSACTION_NOT_FOUND;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        error.emplace_back(Pair("code",  statusCode));
        error.emplace_back(Pair("name",  __FUNCTION__));
        return  error;

    }

    xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(order->fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(order->toCurrency);
    if(!connFrom) {

        Object error;
        auto statusCode = xbridge::Error::NO_SESSION;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, order->fromCurrency)));
        error.emplace_back(Pair("code",  statusCode));
        error.emplace_back(Pair("name",  __FUNCTION__));
        return  error;

    }
    if (!connTo) {

        Object error;
        auto statusCode = xbridge::Error::NO_SESSION;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, order->toCurrency)));
        error.emplace_back(Pair("code",  statusCode));
        error.emplace_back(Pair("name",  __FUNCTION__));
        return  error;

    }

    Object result;
    result.emplace_back(Pair("id",          order->id.GetHex()));
    result.emplace_back(Pair("maker",       order->fromCurrency));
    result.emplace_back(Pair("maker_size",  util::xBridgeValueFromAmount(order->fromAmount)));
    result.emplace_back(Pair("taker",       order->toCurrency));
    result.emplace_back(Pair("taker_size",  util::xBridgeValueFromAmount(order->toAmount)));
    result.emplace_back(Pair("updated_at",  util::iso8601(order->txtime)));
    result.emplace_back(Pair("created_at",  util::iso8601(order->created)));
    result.emplace_back(Pair("status",      order->strState()));
    return result;
}

//******************************************************************************
//******************************************************************************
Value dxMakeOrder(const Array &params, bool fHelp)
{

    if (fHelp) {

        throw runtime_error("dxMakeOrder "
                            "(maker) (maker size) (maker address) "
                            "(taker) (taker size) (taker address) (type) (dryrun)[optional]\n"
                            "Create a new order. dryrun will validate the order without submitting the order to the network.");

    }
    if (params.size() < 7) {

        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS)));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxMakeOrder"));

        return error;
    }


    std::string fromAddress     = params[2].get_str();
    std::string fromCurrency    = params[0].get_str();
    double      fromAmount      = params[1].get_real();
    std::string toAddress       = params[5].get_str();
    std::string toCurrency      = params[3].get_str();
    double      toAmount        = params[4].get_real();
    std::string type            = params[6].get_str();

    // Validate the order type
    if (type != "exact") {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS, "Only the exact type is supported at this time.")));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxMakeOrder"));
        return error;
    }

    auto statusCode = xbridge::SUCCESS;

    xbridge::App &app = xbridge::App::instance();
    if (!app.isValidAddress(fromAddress)) {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_ADDRESS)));
        error.emplace_back(Pair("code", xbridge::INVALID_ADDRESS));
        error.emplace_back(Pair("name", "dxMakeOrder"));

        return error;
    }
    if (!app.isValidAddress(toAddress)) {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_ADDRESS)));
        error.emplace_back(Pair("code", xbridge::INVALID_ADDRESS));
        error.emplace_back(Pair("name", "dxMakeOrder"));

        return error;
    }
    // Perform explicit check on dryrun to avoid executing order on bad spelling
    bool dryrun = false;
    if (params.size() == 8) {
        std::string dryrunParam = params[7].get_str();
        if (dryrunParam != "dryrun") {
            Object error;
            error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS, dryrunParam)));
            error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
            error.emplace_back(Pair("name", "dxMakeOrder"));
            return error;
        }
        dryrun = true;
    }


    Object result;
    statusCode = app.checkCreateParams(fromCurrency, toCurrency,
                                       util::xBridgeAmountFromReal(fromAmount));
    switch (statusCode) {
    case xbridge::SUCCESS:{
        // If dryrun
        if (dryrun) {
            result.emplace_back(Pair("id", uint256().GetHex()));
            result.emplace_back(Pair("maker", fromCurrency));
            result.emplace_back(Pair("maker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(util::xBridgeAmountFromReal(fromAmount)))));
            result.emplace_back(Pair("maker_address", fromAddress));
            result.emplace_back(Pair("taker", toCurrency));
            result.emplace_back(Pair("taker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(util::xBridgeAmountFromReal(toAmount)))));
            result.emplace_back(Pair("taker_address", toAddress));
            result.emplace_back(Pair("status", "created"));
            return result;
        }
        break;
    }

    case xbridge::INVALID_CURRENCY: {

        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, fromCurrency)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxMakeOrder"));

        return result;

    }
    case xbridge::NO_SESSION:{

        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, fromCurrency)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxMakeOrder"));

        return result;

    }
    case xbridge::INSIFFICIENT_FUNDS:{

        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, toAddress)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxMakeOrder"));

        return result;
    }

    default:
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxMakeOrder"));

        return result;
    }


    uint256 id = uint256();
    uint256 blockHash = uint256();
    statusCode = xbridge::App::instance().sendXBridgeTransaction
          (fromAddress, fromCurrency, util::xBridgeAmountFromReal(fromAmount),
           toAddress, toCurrency, util::xBridgeAmountFromReal(toAmount), id, blockHash);

    if (statusCode == xbridge::SUCCESS) {
        xuiConnector.NotifyXBridgeTransactionReceived(xbridge::App::instance().transaction(id));
        Object obj;
        obj.emplace_back(Pair("id",             id.GetHex()));
        obj.emplace_back(Pair("maker_address",  fromAddress));
        obj.emplace_back(Pair("maker",          fromCurrency));
        obj.emplace_back(Pair("maker_size",     boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(util::xBridgeAmountFromReal(fromAmount)))));
        obj.emplace_back(Pair("taker_address",  toAddress));
        obj.emplace_back(Pair("taker",          toCurrency));
        obj.emplace_back(Pair("taker_size",     boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(util::xBridgeAmountFromReal(toAmount)))));
        const auto &createdTime = xbridge::App::instance().transaction(id)->created;
        obj.emplace_back(Pair("created_at",     util::iso8601(createdTime)));
        obj.emplace_back(Pair("updated_at",     util::iso8601(bpt::microsec_clock::universal_time())));
        obj.emplace_back(Pair("block_id",       blockHash.GetHex()));
        obj.emplace_back(Pair("status",         "created"));
        return obj;

    } else {

        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        error.emplace_back(Pair("code", statusCode));
        error.emplace_back(Pair("name", "dxMakeOrder"));

        return error;

    }
}

//******************************************************************************
//******************************************************************************
Value dxTakeOrder(const Array & params, bool fHelp)
{

    if (fHelp)
    {
        throw runtime_error("dxTakeOrder (id) "
                            "(address from) (address to) [optional](dryrun)\n"
                            "Accepts the order. dryrun will evaluate input without accepting the order.");
    }

    auto statusCode = xbridge::SUCCESS;

    if ((params.size() != 3) && (params.size() != 4))
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS)));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxTakeOrder"));
        return  error;
    }

    uint256 id(params[0].get_str());
    std::string fromAddress    = params[1].get_str();
    std::string toAddress      = params[2].get_str();

    xbridge::App &app = xbridge::App::instance();

    if (!app.isValidAddress(fromAddress))
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_ADDRESS, fromAddress)));
        error.emplace_back(Pair("code", xbridge::INVALID_ADDRESS));
        error.emplace_back(Pair("name", "dxTakeOrder"));
        return  error;
    }

    if (!app.isValidAddress(toAddress))
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_ADDRESS, toAddress)));
        error.emplace_back(Pair("code", xbridge::INVALID_ADDRESS));
        error.emplace_back(Pair("name", "dxTakeOrder"));
        return  error;
    }

    // Perform explicit check on dryrun to avoid executing order on bad spelling
    bool dryrun = false;
    if (params.size() == 4) {
        std::string dryrunParam = params[3].get_str();
        if (dryrunParam != "dryrun") {
            Object error;
            error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS, dryrunParam)));
            error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
            error.emplace_back(Pair("name", "dxTakeOrder"));
            return error;
        }
        dryrun = true;
    }

    Object result;
    xbridge::TransactionDescrPtr txDescr;
    statusCode = app.checkAcceptParams(id, txDescr);

    switch (statusCode)
    {
    case xbridge::SUCCESS: {
        if(dryrun)
        {
            result.emplace_back(Pair("id", uint256().GetHex()));

            result.emplace_back(Pair("maker", txDescr->fromCurrency));
            result.emplace_back(Pair("maker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(txDescr->fromAmount))));

            result.emplace_back(Pair("taker", txDescr->toCurrency));
            result.emplace_back(Pair("taker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(txDescr->toAmount))));

            result.emplace_back(Pair("updated_at", util::iso8601(bpt::microsec_clock::universal_time())));
            result.emplace_back(Pair("created_at", util::iso8601(txDescr->created)));

            result.emplace_back(Pair("status", "filled"));
            return result;
        }

        break;
    }
    case xbridge::TRANSACTION_NOT_FOUND:
    {
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, id.GetHex())));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxTakeOrder"));
        return result;
    }

    case xbridge::NO_SESSION:
    {
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, txDescr->toCurrency)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxTakeOrder"));
        return result;
    }

    case xbridge::INSIFFICIENT_FUNDS:
    {
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode, txDescr->to)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxTakeOrder"));
        return result;
    }

    default:
    {
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxTakeOrder"));
        return result;
    }
    }

    std::swap(txDescr->fromCurrency, txDescr->toCurrency);
    std::swap(txDescr->fromAmount, txDescr->toAmount);

    statusCode = app.acceptXBridgeTransaction(id, fromAddress, toAddress);
    if (statusCode == xbridge::SUCCESS)
    {
        result.emplace_back(Pair("id", id.GetHex()));

        result.emplace_back(Pair("maker", txDescr->fromCurrency));
        result.emplace_back(Pair("maker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(txDescr->fromAmount))));

        result.emplace_back(Pair("taker", txDescr->toCurrency));
        result.emplace_back(Pair("taker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(txDescr->toAmount))));

        result.emplace_back(Pair("updated_at", util::iso8601(bpt::microsec_clock::universal_time())));
        result.emplace_back(Pair("created_at", util::iso8601(txDescr->created)));

        result.emplace_back(Pair("status", txDescr->strState()));
        return result;
    }
    else
    {
        result.emplace_back(Pair("error", xbridge::xbridgeErrorText(statusCode)));
        result.emplace_back(Pair("code", statusCode));
        result.emplace_back(Pair("name", "dxTakeOrder"));
        return result;
    }
}

//******************************************************************************
//******************************************************************************
Value dxCancelOrder(const Array &params, bool fHelp)
{
    if(fHelp)
    {
        throw runtime_error("dxCancelOrder (id)\n"
                            "Cancel xbridge order.");
    }

    if (params.size() != 1)
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS)));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxCancelOrder"));
        return error;
    }

    LOG() << "rpc cancel order " << __FUNCTION__;
    uint256 id(params[0].get_str());

    xbridge::TransactionDescrPtr tx = xbridge::App::instance().transaction(id);
    if (!tx)
    {
        Object obj;
        obj.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::Error::TRANSACTION_NOT_FOUND, id.GetHex())));
        obj.emplace_back(Pair("code", xbridge::Error::TRANSACTION_NOT_FOUND));
        obj.emplace_back(Pair("name", "dxCancelOrder"));
        return obj;
    }

    if (tx->state >= xbridge::TransactionDescr::trCreated)
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_STATE)));
        error.emplace_back(Pair("code", xbridge::INVALID_STATE));
        error.emplace_back(Pair("name", "dxCancelOrder"));
        return error;
    }

    const auto res = xbridge::App::instance().cancelXBridgeTransaction(id, crRpcRequest);
    if (res != xbridge::SUCCESS)
    {
        Object obj;
        obj.emplace_back(Pair("error", xbridge::xbridgeErrorText(res)));
        obj.emplace_back(Pair("code", res));
        obj.emplace_back(Pair("name", "dxCancelOrder"));
        return obj;
    }

    xbridge::WalletConnectorPtr connFrom = xbridge::App::instance().connectorByCurrency(tx->fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xbridge::App::instance().connectorByCurrency(tx->toCurrency);
    if (!connFrom || !connTo)
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::NO_SESSION)));
        error.emplace_back(Pair("code", xbridge::NO_SESSION));
        error.emplace_back(Pair("name", "dxCancelOrder"));
        return error;
    }

    Object obj;
    obj.emplace_back(Pair("id", id.GetHex()));

    obj.emplace_back(Pair("maker", tx->fromCurrency));
    obj.emplace_back(Pair("maker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tx->fromAmount))));
    obj.emplace_back(Pair("maker_address", connFrom->fromXAddr(tx->from)));

    obj.emplace_back(Pair("taker", tx->toCurrency));
    obj.emplace_back(Pair("taker_size", boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tx->toAmount))));
    obj.emplace_back(Pair("taker_address", connTo->fromXAddr(tx->to)));

    obj.emplace_back(Pair("updated_at", util::iso8601(tx->txtime)));
    obj.emplace_back(Pair("created_at", util::iso8601(tx->created)));

    obj.emplace_back(Pair("status", tx->strState()));
    return obj;
}

//******************************************************************************
//******************************************************************************
Value dxGetOrderBook(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error("dxGetOrderBook "
                            "(detail level) (maker) (taker) "
                            "(max orders)[optional, default=50) ");
    }

    if ((params.size() < 3 || params.size() > 4))
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS)));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxGetOrderBook"));
        return  error;
    }

    Object res;
    TransactionMap trList = xbridge::App::instance().transactions();
    {
        /**
         * @brief detaiLevel - Get a list of open orders for a product.
         * The amount of detail shown can be customized with the level parameter.
         */
        const auto detailLevel  = params[0].get_int();
        const auto fromCurrency = params[1].get_str();
        const auto toCurrency   = params[2].get_str();

        std::size_t maxOrders = 50;

        if (detailLevel == 2 && params.size() == 4)
            maxOrders = params[3].get_int();

        if (maxOrders < 1)
            maxOrders = 1;

        if (detailLevel < 1 || detailLevel > 4)
        {
            Object error;
            error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_DETAIL_LEVEL)));
            error.emplace_back(Pair("code", xbridge::INVALID_DETAIL_LEVEL));
            error.emplace_back(Pair("name", "dxGetOrderBook"));
            return error;
        }

        res.emplace_back(Pair("detail", detailLevel));
        res.emplace_back(Pair("maker", fromCurrency));
        res.emplace_back(Pair("taker", toCurrency));

        /**
         * @brief bids - array with bids
         */
        Array bids;
        /**
         * @brief asks - array with asks
         */
        Array asks;

        if(trList.empty())
        {
            LOG() << "empty transactions list";
            res.emplace_back(Pair("asks", asks));
            res.emplace_back(Pair("bids", bids));
            return res;
        }

        TransactionMap asksList;
        TransactionMap bidsList;

        //copy all transactions in currencies specified in the parameters

        // ask orders are based in the first token in the trading pair
        std::copy_if(trList.begin(), trList.end(), std::inserter(asksList, asksList.end()),
                     [&toCurrency, &fromCurrency](const TransactionPair &transaction)
        {
            if(transaction.second == nullptr)
                return false;

            return  ((transaction.second->toCurrency == toCurrency) &&
                    (transaction.second->fromCurrency == fromCurrency));
        });

        // bid orders are based in the second token in the trading pair (inverse of asks)
        std::copy_if(trList.begin(), trList.end(), std::inserter(bidsList, bidsList.end()),
                     [&toCurrency, &fromCurrency](const TransactionPair &transaction)
        {
            if(transaction.second == nullptr)
                return false;

            return  ((transaction.second->toCurrency == fromCurrency) &&
                    (transaction.second->fromCurrency == toCurrency));
        });

        std::vector<xbridge::TransactionDescrPtr> asksVector;
        std::vector<xbridge::TransactionDescrPtr> bidsVector;

        for (const auto &trEntry : asksList)
            asksVector.emplace_back(trEntry.second);

        for (const auto &trEntry : bidsList)
            bidsVector.emplace_back(trEntry.second);

        // sort asks descending
        std::sort(asksVector.begin(), asksVector.end(),
                  [](const xbridge::TransactionDescrPtr &a, const xbridge::TransactionDescrPtr &b)
        {
            const auto priceA = util::price(a);
            const auto priceB = util::price(b);
            return priceA > priceB;
        });

        //sort bids descending
        std::sort(bidsVector.begin(), bidsVector.end(),
                  [](const xbridge::TransactionDescrPtr &a, const xbridge::TransactionDescrPtr &b)
        {
            const auto priceA = util::priceBid(a);
            const auto priceB = util::priceBid(b);
            return priceA > priceB;
        });

        // floating point comparisons
        // see Knuth 4.2.2 Eq 36
        auto floatCompare = [](const double a, const double b) -> bool
        {
            auto epsilon = std::numeric_limits<double>::epsilon();
            return (fabs(a - b) / fabs(a) <= epsilon) && (fabs(a - b) / fabs(b) <= epsilon);
        };

        switch (detailLevel)
        {
        case 1:
        {
            //return only the best bid and ask
            const auto bidsItem = std::max_element(bidsList.begin(), bidsList.end(),
                                       [](const TransactionPair &a, const TransactionPair &b)
            {
                //find transaction with best bids
                const auto &tr1 = a.second;
                const auto &tr2 = b.second;

                if(tr1 == nullptr)
                    return true;

                if(tr2 == nullptr)
                    return false;

                const auto priceA = util::priceBid(tr1);
                const auto priceB = util::priceBid(tr2);

                return priceA < priceB;
            });

            const auto bidsCount = std::count_if(bidsList.begin(), bidsList.end(),
                                                 [bidsItem, floatCompare](const TransactionPair &a)
            {
                const auto &tr = a.second;

                if(tr == nullptr)
                    return false;

                const auto price = util::priceBid(tr);

                const auto &bestTr = bidsItem->second;
                if (bestTr != nullptr)
                {
                    const auto bestBidPrice = util::priceBid(bestTr);
                    return floatCompare(price, bestBidPrice);
                }

                return false;
            });

            {
                const auto &tr = bidsItem->second;
                if (tr != nullptr)
                {
                    const auto bidPrice = util::priceBid(tr);
                    bids.emplace_back(Array{boost::lexical_cast<std::string>(bidPrice),
                                            boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tr->toAmount)),
                                            static_cast<int64_t>(bidsCount)});
                }
            }

            const auto asksItem = std::min_element(asksList.begin(), asksList.end(),
                                                   [](const TransactionPair &a, const TransactionPair &b)
            {
                //find transactions with best asks
                const auto &tr1 = a.second;
                const auto &tr2 = b.second;

                if(tr1 == nullptr)
                    return true;

                if(tr2 == nullptr)
                    return false;

                const auto priceA = util::price(tr1);
                const auto priceB = util::price(tr2);
                return priceA < priceB;
            });

            const auto asksCount = std::count_if(asksList.begin(), asksList.end(),
                                                 [asksItem, floatCompare](const TransactionPair &a)
            {
                const auto &tr = a.second;

                if(tr == nullptr)
                    return false;

                const auto price = util::price(tr);

                const auto &bestTr = asksItem->second;
                if (bestTr != nullptr)
                {
                    const auto bestAskPrice = util::price(bestTr);
                    return floatCompare(price, bestAskPrice);
                }

                return false;
            });

            {
                const auto &tr = asksItem->second;
                if (tr != nullptr)
                {
                    const auto askPrice = util::price(tr);
                    asks.emplace_back(Array{boost::lexical_cast<std::string>(askPrice),
                                            boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tr->fromAmount)),
                                            static_cast<int64_t>(asksCount)});
                }
            }

            res.emplace_back(Pair("asks", asks));
            res.emplace_back(Pair("bids", bids));
            return res;
        }
        case 2:
        {
            //Top X bids and asks (aggregated)

            /**
             * @brief bound - calculate upper bound
             */
            auto bound = std::min(maxOrders, bidsVector.size());

            for (size_t i = 0; i < bound; i++)
            {
                if(bidsVector[i] == nullptr)
                    continue;

                Array bid;
                //calculate bids and push to array
                const auto bidAmount    = bidsVector[i]->toAmount;
                const auto bidPrice     = util::priceBid(bidsVector[i]);
                const auto bidSize      = util::xBridgeValueFromAmount(bidAmount);
                const auto bidsCount    = std::count_if(bidsList.begin(), bidsList.end(),
                                                     [bidPrice, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = util::priceBid(tr);

                    return floatCompare(price, bidPrice);
                });

                bid.emplace_back(boost::lexical_cast<std::string>(bidPrice));
                bid.emplace_back(boost::lexical_cast<std::string>(bidSize));
                bid.emplace_back((int64_t)bidsCount);

                bids.emplace_back(bid);
            }

            bound = std::min(maxOrders, asksVector.size());

            for (size_t i = 0; i < bound; i++)
            {
                if(asksVector[i] == nullptr)
                    continue;

                Array ask;
                //calculate asks and push to array
                const auto bidAmount    = asksVector[i]->fromAmount;
                const auto askPrice     = util::price(asksVector[i]);
                const auto bidSize      = util::xBridgeValueFromAmount(bidAmount);
                const auto asksCount    = std::count_if(asksList.begin(), asksList.end(),
                                                     [askPrice, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = util::price(tr);

                    return floatCompare(price, askPrice);
                });

                ask.emplace_back(boost::lexical_cast<std::string>(askPrice));
                ask.emplace_back(boost::lexical_cast<std::string>(bidSize));
                ask.emplace_back(static_cast<int64_t>(asksCount));

                asks.emplace_back(ask);
            }

            res.emplace_back(Pair("asks", asks));
            res.emplace_back(Pair("bids", bids));
            return  res;
        }
        case 3:
        {
            //Full order book (non aggregated)
            auto bound = std::min(maxOrders, bidsVector.size());

            for (size_t i = 0; i < bound; i++)
            {
                if(bidsVector[i] == nullptr)
                    continue;

                Array bid;
                const auto bidAmount   = bidsVector[i]->toAmount;
                const auto bidPrice    = util::priceBid(bidsVector[i]);
                bid.emplace_back(boost::lexical_cast<std::string>(bidPrice));
                bid.emplace_back(boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(bidAmount)));
                bid.emplace_back(bidsVector[i]->id.GetHex());

                bids.emplace_back(bid);
            }

            bound = std::min(maxOrders, asksVector.size());

            for (size_t i = 0; i < bound; i++)
            {
                if(asksVector[i] == nullptr)
                    continue;

                Array ask;
                const auto bidAmount    = asksVector[i]->fromAmount;
                const auto askPrice     = util::price(asksVector[i]);
                ask.emplace_back(boost::lexical_cast<std::string>(askPrice));
                ask.emplace_back(boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(bidAmount)));
                ask.emplace_back(asksVector[i]->id.GetHex());

                asks.emplace_back(ask);
            }

            res.emplace_back(Pair("asks", asks));
            res.emplace_back(Pair("bids", bids));
            return  res;
        }
        case 4:
        {
            //return Only the best bid and ask
            const auto bidsItem = std::max_element(bidsList.begin(), bidsList.end(),
                                       [](const TransactionPair &a, const TransactionPair &b)
            {
                //find transaction with best bids
                const auto &tr1 = a.second;
                const auto &tr2 = b.second;

                if(tr1 == nullptr)
                    return true;

                if(tr2 == nullptr)
                    return false;

                const auto priceA = util::priceBid(tr1);
                const auto priceB = util::priceBid(tr2);

                return priceA < priceB;
            });

            {
                const auto &tr = bidsItem->second;
                if (tr != nullptr)
                {
                    const auto bidPrice = util::priceBid(tr);
                    bids.emplace_back(boost::lexical_cast<std::string>(bidPrice));
                    bids.emplace_back(boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tr->toAmount)));

                    Array bidsIds;
                    bidsIds.emplace_back(tr->id.GetHex());

                    for(const TransactionPair &tp : bidsList)
                    {
                        const auto &otherTr = tp.second;

                        if(otherTr == nullptr)
                            continue;

                        if(tr->id == otherTr->id)
                            continue;

                        const auto otherTrBidPrice = util::priceBid(otherTr);

                        if(!floatCompare(bidPrice, otherTrBidPrice))
                            continue;

                        bidsIds.emplace_back(otherTr->id.GetHex());
                    }

                    bids.emplace_back(bidsIds);
                }
            }

            const auto asksItem = std::min_element(asksList.begin(), asksList.end(),
                                                   [](const TransactionPair &a, const TransactionPair &b)
            {
                //find transactions with best asks
                const auto &tr1 = a.second;
                const auto &tr2 = b.second;

                if(tr1 == nullptr)
                    return true;

                if(tr2 == nullptr)
                    return false;

                const auto priceA = util::price(tr1);
                const auto priceB = util::price(tr2);
                return priceA < priceB;
            });

            {
                const auto &tr = asksItem->second;
                if (tr != nullptr)
                {
                    const auto askPrice = util::price(tr);
                    asks.emplace_back(boost::lexical_cast<std::string>(askPrice));
                    asks.emplace_back(boost::lexical_cast<std::string>(util::xBridgeValueFromAmount(tr->fromAmount)));

                    Array asksIds;
                    asksIds.emplace_back(tr->id.GetHex());

                    for(const TransactionPair &tp : asksList)
                    {
                        const auto &otherTr = tp.second;

                        if(otherTr == nullptr)
                            continue;

                        if(tr->id == otherTr->id)
                            continue;

                        const auto otherTrAskPrice = util::price(otherTr);

                        if(!floatCompare(askPrice, otherTrAskPrice))
                            continue;

                        asksIds.emplace_back(otherTr->id.GetHex());
                    }

                    asks.emplace_back(asksIds);
                }
            }

            res.emplace_back(Pair("asks", asks));
            res.emplace_back(Pair("bids", bids));
            return res;
        }

        default:
            Object error;
            error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                                                                       "detail level needs to be [1-4]")));
            error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
            error.emplace_back(Pair("name", "dxGetOrderBook"));
            return error;
        }
    }
}

//******************************************************************************
//******************************************************************************
json_spirit::Value dxGetMyOrders(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetMyOrders");

    }

    if (!params.empty()) {

        Object error;
        error.emplace_back(Pair("error",
                                "This function does not accept any parameters"));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        return  error;

    }

    xbridge::App & xapp = xbridge::App::instance();

    Array r;

    TransactionMap trList = xbridge::App::instance().transactions();
    {
        if(trList.empty()) {

            LOG() << "empty  transactions list ";
            return r;

        }

        for(auto i : trList)
        {

            const auto& t = *i.second;

            if(!t.isLocal())
                continue;

            xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(t.fromCurrency);
            xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(t.toCurrency);

            Object o;

            o.emplace_back(Pair("id", t.id.GetHex()));

            // maker data
            o.emplace_back(Pair("maker", t.fromCurrency));
            o.emplace_back(Pair("maker_size", util::xBridgeValueFromAmount(t.fromAmount)));
            o.emplace_back(Pair("maker_address", connFrom->fromXAddr(t.from)));
            // taker data
            o.emplace_back(Pair("taker", t.toCurrency));
            o.emplace_back(Pair("taker_size", util::xBridgeValueFromAmount(t.toAmount)));
            o.emplace_back(Pair("taker_address", connFrom->fromXAddr(t.to)));
            // dates
            o.emplace_back(Pair("updated_at", util::iso8601(t.txtime)));
            o.emplace_back(Pair("created_at", util::iso8601(t.created)));

            // should we make returning value correspond to the description or vice versa?
            // Order status: created|open|pending|filled|canceled
            o.emplace_back(Pair("status", t.strState()));

            r.emplace_back(o);
        }


    }

    return r;
}

//******************************************************************************
//******************************************************************************
json_spirit::Value  dxGetTokenBalances(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp) {

        throw runtime_error("dxGetTokenBalances");

    }

    if (params.size() != 0) {

        Object error;
        error.emplace_back(Pair("error",
                                "This function does not accept any parameters"));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        return  error;

    }
    Object res;
    const auto &connectors = xbridge::App::instance().connectors();
    for(const auto &connector : connectors) {

        res.emplace_back(connector->currency, boost::lexical_cast<std::string>(connector->getWalletBalance()));

    }

    return res;
}

//******************************************************************************
//******************************************************************************
Value dxGetLockedUtxos(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error("dxGetLockedUtxos (id)\n"
                            "Return list of locked utxo of an order.");
    }

    if (params.size() != 1)
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS)));
        error.emplace_back(Pair("code", xbridge::INVALID_PARAMETERS));
        error.emplace_back(Pair("name", "dxGetLockedUtxos"));
        return error;
    }

    xbridge::Exchange & e = xbridge::Exchange::instance();
    if (!e.isStarted())
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::NOT_EXCHANGE_NODE)));
        error.emplace_back(Pair("code", xbridge::NOT_EXCHANGE_NODE));
        error.emplace_back(Pair("name", "dxGetLockedUtxos"));
        return error;
    }

    uint256 id(params[0].get_str());

    xbridge::TransactionPtr pendingTx = e.pendingTransaction(id);
    xbridge::TransactionPtr acceptedTx = e.transaction(id);

    if (!pendingTx->isValid() && !acceptedTx->isValid())
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::Error::TRANSACTION_NOT_FOUND, id.GetHex())));
        error.emplace_back(Pair("code", xbridge::Error::TRANSACTION_NOT_FOUND));
        error.emplace_back(Pair("name", "dxGetLockedUtxos"));
        return error;
    }

    std::vector<xbridge::wallet::UtxoEntry> items;

    if(!e.getUtxoItems(id, items))
    {
        Object error;
        error.emplace_back(Pair("error", xbridge::xbridgeErrorText(xbridge::Error::TRANSACTION_NOT_FOUND, id.GetHex())));
        error.emplace_back(Pair("code", xbridge::Error::TRANSACTION_NOT_FOUND));
        error.emplace_back(Pair("name", "dxGetLockedUtxos"));
        return error;
    }

    Array utxo;

    for(const xbridge::wallet::UtxoEntry & entry : items)
        utxo.emplace_back(entry.toString());

    Object obj;
    obj.emplace_back(Pair("id", id.GetHex()));

    if(pendingTx->isValid())
        obj.emplace_back(Pair(pendingTx->a_currency(), utxo));
    else if(acceptedTx->isValid())
        obj.emplace_back(Pair(acceptedTx->a_currency() + " and " + acceptedTx->b_currency(), utxo));

    return obj;
}
