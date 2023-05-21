#include "market_maker.h"

#include "trade_engine.h"

namespace Trading {
  MarketMaker::MarketMaker(Common::Logger *logger, TradeEngine *trade_engine, const FeatureEngine *feature_engine,
                           OrderManager *order_manager, const TradeEngineCfgHashMap &ticker_cfg)
      : feature_engine_(feature_engine), order_manager_(order_manager), logger_(logger),
        ticker_cfg_(ticker_cfg) {
    trade_engine->algoOnOrderBookUpdate_ = [this](auto ticker_id, auto price, auto side, auto book) {
      onOrderBookUpdate(ticker_id, price, side, book);
    };
    trade_engine->algoOnTradeUpdate_ = [this](auto market_update, auto book) { onTradeUpdate(market_update, book); };
    trade_engine->algoOnOrderUpdate_ = [this](auto client_response) { onOrderUpdate(client_response); };
  }
}
