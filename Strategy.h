#pragma once
#include "DataBuffer.h"
#include <string>
#include <map>
#include <vector>
#include "TdSpi.h"
#include <atomic>
#include "Indicator.h"
using namespace std;

enum Stratey_Step { Strategy_OpenCondition, Strategy_OpenLeg1_Waiting, Strategy_OpenLeg1_Canceling, Strategy_OpenLeg2_Waiting, Strategy_OpenLeg2_Canceling, Strategy_CloseCondition, Strategy_CloseLeg1_Waiting, Strategy_CloseLeg1_Canceling, Strategy_CloseLeg2_Waiting, Strategy_CloseLeg2_Canceling };

//最重要的结构体，所有的下单和成交都通过这个结构体管理
typedef struct tagStructOrderTrades
{
	bool bMatched;
	double dAveragePrice;
	CThostFtdcOrderField stOrder;
	vector<CThostFtdcTradeField> vctTrades;
}StructOrderTrades;

class CSimpleStrategy
{
public:
	CSimpleStrategy(CDataBuffer* pDataBuffer, string strLeg1, string strLeg2, CThostFtdcTraderApi* pTDUserApi, TThostFtdcBrokerIDType szBrokerID, TThostFtdcInvestorIDType szInvestorID);
	~CSimpleStrategy();
	// 定义了定时器类
	class Timer {
	public:
		void start(int interval);
	private:
		void OnTimer(int interval);
	};
private:
	Timer timer;
	CRITICAL_SECTION m_lockStrategyStep;
	CDataBuffer* m_pDataBuffer;
	CThostFtdcTraderApi* m_pTDUserApi;

	Stratey_Step m_eStrategy_Step;
	CThostFtdcOrderField m_stCurrentOrder;
	string m_strCurrentOrderRef;
	int m_nCurrentOrderFrontID;
	int m_nCurrentOrderSessionID;

	string m_strLeg1;
	string m_strLeg2;
	string m_strLeg1OpenOrderTag;
	string m_strLeg2OpenOrderTag;
	string m_strLeg1CloseOrderTag;
	string m_strLeg2CloseOrderTag;
	double m_dBuyOpen;
	double m_dBuyClose;
	double m_dSellOpen;
	double m_dSellClose;

	map<string, StructOrderTrades> m_mapOrderTrades;
	map<string, string> m_mapOrderSysIDTag;
	vector<CThostFtdcTradeField> m_vctReservedTrades;
	map<string, CThostFtdcInstrumentField> m_mapInstruments;

	TThostFtdcFrontIDType	FRONT_ID;	//前置编号
	TThostFtdcSessionIDType	SESSION_ID;	//会话编号
	TThostFtdcOrderRefType	ORDER_REF;
	TThostFtdcBrokerIDType	BROKER_ID;
	TThostFtdcInvestorIDType INVESTOR_ID;
	int iRequestID;
	int m_timerInterval; //设置定时器的等待时间
	std::atomic<bool> mPauseTimer;  //定时器的开关
	int timerInterval;
	int m_meanWindow = 30; // 默认滑动窗口为30


public:
	void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData);
	void OnRtnOrder(CThostFtdcOrderField* pOrder);
	void OnRtnTrade(CThostFtdcTradeField* pTrade);
	void SetCondition(double dBuyOpen, double dBuyClose, double dSellOpen, double dSellClose);
	void PushInstrument(CThostFtdcInstrumentField stInst);
	void SetConnectionInfo(TThostFtdcFrontIDType szFrontID, TThostFtdcSessionIDType nSessionID, TThostFtdcOrderRefType nOrderRef);
	Stratey_Step GetStrateyStep();
	void SetMeanWindow(int w); //{ m_meanWindow = w; }
	int GetMeanWindow() const; //{ return m_meanWindow; }

private:
	bool PushNewOrder(CThostFtdcOrderField stNewOrder, double& dAveragePrice);
	bool PushNewTrade(CThostFtdcTradeField stNewTrade, double& dAveragePrice);
	bool PushNewOrderSysID(CThostFtdcOrderField stNewOrder);
	static bool CheckOrderTradesMatched(CThostFtdcOrderField stOrder, vector<CThostFtdcTradeField> vctTrades);
	static double GetAveragePrice(vector<CThostFtdcTradeField> vctTrades, int& nTradedVolume);
	bool GetOrder(string strOrderSysID, CThostFtdcOrderField& stOrder, double& dAveragePrice, bool& bMatched);
	bool GetOrderByTag(string strOrderTag, CThostFtdcOrderField& stOrder, double& dAveragePrice, bool& bMatched);
	static bool IsCancelableOrder(TThostFtdcOrderStatusType OrderStatus);
	static bool IsNewOrder(CThostFtdcOrderField* pCurrentOrder, CThostFtdcOrderField* pNewOrder);
	bool UpdateOrderStatus(CThostFtdcOrderField* pNewOrder);
	void ReqOrderInsert(string strExchangeID, string strInstrumentID, char cOffect, char cDirection, double dLimitPrice, int nVolume, bool bLeg1 = true);
	void ReqOrderAction(string strExchangeID, string strInstrumentID, int nFront, int nSessionID, string strOrderRef);
	void CheckCurrentOrder();
};