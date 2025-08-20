#include <assert.h>
#include "Strategy.h"
#include "ThostFtdcTraderApi.h"
#include <iostream>
#include <windows.h>
#include <thread>
#include <functional>
#include "Indicator.h"
#include "DataBuffer.h"

using namespace std;
extern CSimpleStrategy* g_pSimpleStrategy;
string Strategy_Step_Name[] = { "OpenCondition", "OpenLeg1Waiting", "OpenLeg1Canceling", "OpenLeg2Waiting", "OpenLeg2Canceling", "CloseCondition", "CloseLeg1Waiting", "CloseLeg1Canceling", "CloseLeg2Waiting", "CloseLeg2Canceling" };

CSimpleStrategy::CSimpleStrategy(CDataBuffer* pDataBuffer, string strLeg1, string strLeg2, CThostFtdcTraderApi* pTDUserApi, TThostFtdcBrokerIDType szBrokerID, TThostFtdcInvestorIDType szInvestorID)
{
	assert(pDataBuffer);
	assert(pTDUserApi);
	m_pDataBuffer = pDataBuffer;
	m_eStrategy_Step = Strategy_OpenCondition;
	m_strCurrentOrderRef = "";
	m_nCurrentOrderFrontID = 0;
	m_nCurrentOrderSessionID = 0;
	m_strLeg1 = strLeg1;
	m_strLeg2 = strLeg2;
	m_dBuyOpen = m_dBuyClose = m_dSellOpen = m_dSellClose = 0;
	m_pTDUserApi = pTDUserApi;

	strcpy_s(BROKER_ID, szBrokerID);
	strcpy_s(INVESTOR_ID, szInvestorID);
	iRequestID = 0;
	InitializeCriticalSection(&m_lockStrategyStep);
	mPauseTimer = true; //初始化定时器，开始时候设置为关闭定时器
	timer.start(8000);
}

CSimpleStrategy::~CSimpleStrategy()
{
	DeleteCriticalSection(&m_lockStrategyStep);
}
void CSimpleStrategy::Timer::start(int interval) {
	std::thread t(std::bind(&Timer::OnTimer, this, interval)); // 绑定成员函数和对象实例
	t.detach();
}
void CSimpleStrategy::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData)
{
	if (strcmp(pDepthMarketData->InstrumentID, m_strLeg1.c_str()) != 0 &&
		strcmp(pDepthMarketData->InstrumentID, m_strLeg2.c_str()) != 0)
		return;

	map<string, CThostFtdcDepthMarketDataField>::iterator iter, iter2;
	map<string, CThostFtdcInstrumentField>::iterator iterInst;
	string strInstrumentID = pDepthMarketData->InstrumentID;
	string strExchangeID;

	iterInst = m_mapInstruments.find(strInstrumentID);
	if (iterInst != m_mapInstruments.end())
	{
		strExchangeID = iterInst->second.ExchangeID;
	}

	m_pDataBuffer->PushNewTick(*pDepthMarketData);

	// 获取Leg1和Leg2的tick序列
	vector<CThostFtdcDepthMarketDataField> vctLeg1, vctLeg2;
	m_pDataBuffer->GetSeries(m_strLeg1, vctLeg1);
	m_pDataBuffer->GetSeries(m_strLeg2, vctLeg2);


	// 计算30tick滑动均值&&20tick
	double meanLeg1 = 0.0;
	double meanLeg2 = 0.0;
	double meanLeg3 = 0.0;
	double meanLeg4 = 0.0;
	//auto vctMeanLeg1 = CIndicator::LegMean(vctLeg1, 30);
	//auto vctMeanLeg2 = CIndicator::LegMean(vctLeg2, 30);
	auto vctMeanLeg1 = CIndicator::LegMean(vctLeg1, m_meanWindow);
	auto vctMeanLeg2 = CIndicator::LegMean(vctLeg2, m_meanWindow);

	//计算阈值
	if (!vctMeanLeg1.empty() && !vctMeanLeg2.empty()) {
		meanLeg1 = vctMeanLeg1.back().dValue;
		meanLeg2 = vctMeanLeg2.back().dValue;
		m_dBuyOpen = meanLeg1 - meanLeg2;
		m_dSellClose = meanLeg1 - meanLeg2;
	}

	double dSpreadLL, dSpreadBB, dSpreadBA, dSpreadAB, dSpreadAA;
	if (m_pDataBuffer->GetSpread(m_strLeg1, m_strLeg2, dSpreadLL, dSpreadBB, dSpreadBA, dSpreadAB, dSpreadAA))
	{
		CThostFtdcDepthMarketDataField stLeg1Tick;
		m_pDataBuffer->GetTick(m_strLeg1, stLeg1Tick);

		EnterCriticalSection(&m_lockStrategyStep);
		if (m_eStrategy_Step == Strategy_OpenCondition)
		{
			//IF远月-近月基差大于阈值
			if (dSpreadLL > m_dBuyOpen)
			{
				cerr << "IF远月-近月基差大于阈值" << endl;
				m_eStrategy_Step = Strategy_OpenLeg1_Waiting;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				m_strCurrentOrderRef = ORDER_REF;
				m_nCurrentOrderFrontID = FRONT_ID;
				m_nCurrentOrderSessionID = SESSION_ID;
				ReqOrderInsert(pDepthMarketData->ExchangeID, m_strLeg1, THOST_FTDC_OF_Open, THOST_FTDC_D_Sell, stLeg1Tick.AskPrice1, 1, true);
				//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
				mPauseTimer = false;
			}
		}
		else if (m_eStrategy_Step == Strategy_CloseCondition)
		{
			//IF远月-近月基差小于阈值
			if (dSpreadLL < m_dSellClose)
			{
				cerr << "IF远月-近月基差小于阈值" << endl;
				m_eStrategy_Step = Strategy_CloseLeg1_Waiting;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				m_strCurrentOrderRef = ORDER_REF;
				m_nCurrentOrderFrontID = FRONT_ID;
				m_nCurrentOrderSessionID = SESSION_ID;
				ReqOrderInsert(pDepthMarketData->ExchangeID, m_strLeg1, THOST_FTDC_OF_Close, THOST_FTDC_D_Buy, stLeg1Tick.BidPrice1, 1, true);
				//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
				mPauseTimer = false;
			}
		}
		LeaveCriticalSection(&m_lockStrategyStep);
	}
}

void CSimpleStrategy::OnRtnOrder(CThostFtdcOrderField* pOrder)//没问题
{
	char szLog[1024];
	memset(szLog, 0, 1024);
	bool bMatched = false;
	double dAveragePrice;
	bMatched = PushNewOrder(*pOrder, dAveragePrice);
	if (UpdateOrderStatus(pOrder))
	{
		EnterCriticalSection(&m_lockStrategyStep);
		if (!IsCancelableOrder(pOrder->OrderStatus))
		{
			if (m_eStrategy_Step == Strategy_OpenLeg1_Waiting || m_eStrategy_Step == Strategy_OpenLeg1_Canceling)
			{
				//KillTimer(m_hDlg, 2); //改造定时器
				mPauseTimer = true;
				if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
				{
					if (bMatched)
					{
						CThostFtdcDepthMarketDataField stLeg2Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(pOrder->ExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else
				{
					m_eStrategy_Step = Strategy_OpenCondition;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
			else if (m_eStrategy_Step == Strategy_OpenLeg2_Waiting || m_eStrategy_Step == Strategy_OpenLeg2_Canceling)
			{
				//KillTimer(m_hDlg, 2); //改造定时器
				mPauseTimer = true;
				if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
				{
					if (bMatched)
					{
						if (m_strLeg1OpenOrderTag != "" && m_strLeg2OpenOrderTag != "")
						{
							CThostFtdcOrderField stLeg1Order;
							CThostFtdcOrderField stLeg2Order;
							double dAveragePriceLeg1;
							double dAveragePriceLeg2;
							bool bMatchedLeg1;
							bool bMatchedLeg2;
							if (GetOrderByTag(m_strLeg1OpenOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
								GetOrderByTag(m_strLeg2OpenOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
							{
								sprintf_s(szLog, "开仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
								cerr << szLog << endl;
							}
						}

						m_eStrategy_Step = Strategy_CloseCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else
				{
					CThostFtdcDepthMarketDataField stLeg2Tick;
					m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick);
					m_strCurrentOrderRef = ORDER_REF;
					m_nCurrentOrderFrontID = FRONT_ID;
					m_nCurrentOrderSessionID = SESSION_ID;
					ReqOrderInsert(pOrder->ExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1, 1, false);
					//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
					mPauseTimer = false;
					m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
			else if (m_eStrategy_Step == Strategy_CloseLeg1_Waiting || m_eStrategy_Step == Strategy_CloseLeg1_Canceling)
			{
				//KillTimer(m_hDlg, 2); //改造定时器
				mPauseTimer = true;
				if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
				{
					if (bMatched)
					{
						CThostFtdcDepthMarketDataField stLeg1Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg1Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(pOrder->ExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg1Tick.BidPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL);
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else
				{
					m_eStrategy_Step = Strategy_CloseCondition;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
			else if (m_eStrategy_Step == Strategy_CloseLeg2_Waiting || m_eStrategy_Step == Strategy_CloseLeg2_Canceling)
			{
				//KillTimer(m_hDlg, 2); //改造定时器
				mPauseTimer = true;
				if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
				{
					if (bMatched)
					{
						if (m_strLeg1CloseOrderTag != "" && m_strLeg2CloseOrderTag != "")
						{
							CThostFtdcOrderField stLeg1Order;
							CThostFtdcOrderField stLeg2Order;
							double dAveragePriceLeg1;
							double dAveragePriceLeg2;
							bool bMatchedLeg1;
							bool bMatchedLeg2;
							if (GetOrderByTag(m_strLeg1CloseOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
								GetOrderByTag(m_strLeg2CloseOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
							{
								sprintf_s(szLog, "平仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
								cerr << szLog << endl;
							}
						}
						m_eStrategy_Step = Strategy_OpenCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else
				{
					CThostFtdcDepthMarketDataField stLeg1Tick;
					m_pDataBuffer->GetTick(m_strLeg2, stLeg1Tick);
					m_strCurrentOrderRef = ORDER_REF;
					m_nCurrentOrderFrontID = FRONT_ID;
					m_nCurrentOrderSessionID = SESSION_ID;
					ReqOrderInsert(pOrder->ExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg1Tick.BidPrice1, 1, false);
					//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
					mPauseTimer = false;
					m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
		}
		LeaveCriticalSection(&m_lockStrategyStep);
	}
}

void CSimpleStrategy::OnRtnTrade(CThostFtdcTradeField* pTrade)
{
	char szLog[1024];
	memset(szLog, 0, 1024);
	bool bMatched = false;
	double dAveragePrice;
	bMatched = PushNewTrade(*pTrade, dAveragePrice);
	if (bMatched)
	{
		CThostFtdcOrderField stOrder;
		if (GetOrder(pTrade->OrderSysID, stOrder, dAveragePrice, bMatched))
		{
			EnterCriticalSection(&m_lockStrategyStep);
			if (!IsCancelableOrder(stOrder.OrderStatus))
			{
				if (m_eStrategy_Step == Strategy_OpenLeg1_Waiting || m_eStrategy_Step == Strategy_OpenLeg1_Canceling)
				{
					//KillTimer(m_hDlg, 2);//改造定时器
					mPauseTimer = true;
					if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
					{
						CThostFtdcDepthMarketDataField stLeg2Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(stOrder.ExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL);
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
					else
					{
						m_eStrategy_Step = Strategy_OpenCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else if (m_eStrategy_Step == Strategy_OpenLeg2_Waiting || m_eStrategy_Step == Strategy_OpenLeg2_Canceling)
				{
					//KillTimer(m_hDlg, 2); //改造定时器
					mPauseTimer = true;
					if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
					{
						if (m_strLeg1OpenOrderTag != "" && m_strLeg2OpenOrderTag != "")
						{
							CThostFtdcOrderField stLeg1Order;
							CThostFtdcOrderField stLeg2Order;
							double dAveragePriceLeg1;
							double dAveragePriceLeg2;
							bool bMatchedLeg1;
							bool bMatchedLeg2;
							if (GetOrderByTag(m_strLeg1OpenOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
								GetOrderByTag(m_strLeg2OpenOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
							{
								sprintf_s(szLog, "开仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
								cerr << szLog << endl;
							}
						}
						m_eStrategy_Step = Strategy_CloseCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
					else
					{
						CThostFtdcDepthMarketDataField stLeg2Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(stOrder.ExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL);
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else if (m_eStrategy_Step == Strategy_CloseLeg1_Waiting || m_eStrategy_Step == Strategy_CloseLeg1_Canceling)
				{
					//KillTimer(m_hDlg, 2); //改造定时器
					mPauseTimer = true;
					if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
					{
						CThostFtdcDepthMarketDataField stLeg1Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg1Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(stOrder.ExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg1Tick.BidPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL);
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
					else
					{
						m_eStrategy_Step = Strategy_CloseCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
				else if (m_eStrategy_Step == Strategy_CloseLeg2_Waiting || m_eStrategy_Step == Strategy_CloseLeg2_Canceling)
				{
					//KillTimer(m_hDlg, 2); //改造定时器
					mPauseTimer = true;
					if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
					{
						if (m_strLeg1CloseOrderTag != "" && m_strLeg2CloseOrderTag != "")
						{
							CThostFtdcOrderField stLeg1Order;
							CThostFtdcOrderField stLeg2Order;
							double dAveragePriceLeg1;
							double dAveragePriceLeg2;
							bool bMatchedLeg1;
							bool bMatchedLeg2;
							if (GetOrderByTag(m_strLeg1CloseOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
								GetOrderByTag(m_strLeg2CloseOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
							{
								sprintf_s(szLog, "平仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
								cerr << szLog << endl;
							}
						}
						m_eStrategy_Step = Strategy_OpenCondition;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
					else
					{
						CThostFtdcDepthMarketDataField stLeg1Tick;
						m_pDataBuffer->GetTick(m_strLeg2, stLeg1Tick);
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						ReqOrderInsert(stOrder.ExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg1Tick.BidPrice1, 1, false);
						//SetTimer(m_hDlg, 2, 10000, NULL);
						mPauseTimer = false;
						m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
						cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					}
				}
			}
			LeaveCriticalSection(&m_lockStrategyStep);
		}
	}
}
//改造定时器
void CSimpleStrategy::Timer::OnTimer(int interval) //尽量避免全局指针，使用成员函数参数传递外层类的指针更安全和明确，可修改
{
	while (true)
	{
		if (!g_pSimpleStrategy->mPauseTimer) {
			Sleep(interval);
			g_pSimpleStrategy->mPauseTimer = true; //暂停定时器
			g_pSimpleStrategy->CheckCurrentOrder();
		}
	}
}
//需要显式调用SetCondition完成
void CSimpleStrategy::SetCondition(double dBuyOpen, double dBuyClose, double dSellOpen, double dSellClose)
{
	m_dBuyOpen = dBuyOpen;
	m_dBuyClose = dBuyClose;
	m_dSellOpen = dSellOpen;
	m_dSellClose = dSellClose;
}

void CSimpleStrategy::PushInstrument(CThostFtdcInstrumentField stInst)
{
	EnterCriticalSection(&m_lockStrategyStep);
	m_mapInstruments[stInst.InstrumentID] = stInst;
	LeaveCriticalSection(&m_lockStrategyStep);
}

void CSimpleStrategy::SetConnectionInfo(TThostFtdcFrontIDType nFrontID, TThostFtdcSessionIDType nSessionID, TThostFtdcOrderRefType szOrderRef)
{
	FRONT_ID = nFrontID;
	SESSION_ID = nSessionID;
	strcpy_s(ORDER_REF, szOrderRef);
}

Stratey_Step CSimpleStrategy::GetStrateyStep()
{
	return m_eStrategy_Step;
}

bool CSimpleStrategy::PushNewOrder(CThostFtdcOrderField stNewOrder, double& dAveragePrice)
{
	bool bMatched = false;
	dAveragePrice = 0;
	char szOrderTag[256];
	memset(szOrderTag, 0, 256);
	sprintf_s(szOrderTag, "%d|%d|%s", stNewOrder.FrontID, stNewOrder.SessionID, stNewOrder.OrderRef);
	map<string, StructOrderTrades>::iterator iter;
	vector<CThostFtdcTradeField>::iterator iter3;
	EnterCriticalSection(&m_lockStrategyStep);
	bool bNewOrderSysID = PushNewOrderSysID(stNewOrder);

	if (bNewOrderSysID)
	{
		for (iter3 = m_vctReservedTrades.begin(); iter3 != m_vctReservedTrades.end();)
		{
			if (strcmp(stNewOrder.OrderSysID, iter3->OrderSysID) == 0)
			{
				bMatched = PushNewTrade(*iter3, dAveragePrice);
				iter3 = m_vctReservedTrades.erase(iter3);
			}
			else
			{
				iter3++;
			}
		}
	}
	iter = m_mapOrderTrades.find(szOrderTag);
	if (iter != m_mapOrderTrades.end())
	{
		if (IsNewOrder(&iter->second.stOrder, &stNewOrder))
		{
			memcpy(&iter->second.stOrder, &stNewOrder, sizeof(CThostFtdcOrderField));
			bMatched = iter->second.bMatched = CheckOrderTradesMatched(stNewOrder, iter->second.vctTrades);
			if (bMatched && iter->second.vctTrades.size() > 0)
			{
				int nTradeVolume = 0;
				iter->second.dAveragePrice = GetAveragePrice(iter->second.vctTrades, nTradeVolume);
				dAveragePrice = iter->second.dAveragePrice;
			}
		}
	}
	else
	{
		StructOrderTrades stOrderTrades;
		stOrderTrades.bMatched = false;
		stOrderTrades.dAveragePrice = 0;
		memcpy(&stOrderTrades.stOrder, &stNewOrder, sizeof(CThostFtdcOrderField));
		m_mapOrderTrades.insert(make_pair(szOrderTag, stOrderTrades));
	}

	LeaveCriticalSection(&m_lockStrategyStep);
	return bMatched;
}

bool CSimpleStrategy::PushNewTrade(CThostFtdcTradeField stNewTrade, double& dAveragePrice)
{
	if (strlen(stNewTrade.OrderSysID) == 0)
		return false;
	bool bMatched = false;
	bool bNewTrade = false;
	map<string, StructOrderTrades>::iterator iter;
	map<string, string>::iterator iter2;
	vector<CThostFtdcTradeField>::iterator iter3;
	EnterCriticalSection(&m_lockStrategyStep);
	iter2 = m_mapOrderSysIDTag.find(stNewTrade.OrderSysID);
	if (iter2 != m_mapOrderSysIDTag.end())
	{
		iter = m_mapOrderTrades.find(iter2->second);
		if (iter != m_mapOrderTrades.end())
		{
			bNewTrade = true;
			int nVolumeTraded = 0;
			for (int i = 0; i < iter->second.vctTrades.size(); i++)
			{
				if (strcmp(stNewTrade.TradeID, iter->second.vctTrades[i].TradeID) == 0)
				{
					bNewTrade = false;
					break;
				}
			}
			if (bNewTrade)
			{
				iter->second.vctTrades.push_back(stNewTrade);
				bMatched = CheckOrderTradesMatched(iter->second.stOrder, iter->second.vctTrades);
				iter->second.bMatched = bMatched;
				if (bMatched)
				{
					iter->second.dAveragePrice = GetAveragePrice(iter->second.vctTrades, iter->second.stOrder.VolumeTraded);
				}
			}
		}
		else
		{
			bool bReservedFound = false;
			for (iter3 = m_vctReservedTrades.begin(); iter3 != m_vctReservedTrades.end(); iter3++)
			{
				if (strcmp(stNewTrade.TradeID, iter3->TradeID) == 0)
				{
					bReservedFound = true;
					break;
				}
			}
			if (!bReservedFound)
			{
				m_vctReservedTrades.push_back(stNewTrade);
			}
		}
	}
	else
	{
		bool bReservedFound = false;
		for (iter3 = m_vctReservedTrades.begin(); iter3 != m_vctReservedTrades.end(); iter3++)
		{
			if (strcmp(stNewTrade.TradeID, iter3->TradeID) == 0)
			{
				bReservedFound = true;
				break;
			}
		}
		if (!bReservedFound)
		{
			m_vctReservedTrades.push_back(stNewTrade);
		}
	}
	LeaveCriticalSection(&m_lockStrategyStep);
	return bMatched;
}

bool CSimpleStrategy::PushNewOrderSysID(CThostFtdcOrderField stNewOrder)
{
	if (strlen(stNewOrder.OrderSysID) == 0)
		return false;
	bool bNewSysID = false;
	char szOrderTag[256];
	memset(szOrderTag, 0, 256);
	sprintf_s(szOrderTag, "%d|%d|%s", stNewOrder.FrontID, stNewOrder.SessionID, stNewOrder.OrderRef);
	map<string, string>::iterator iter;
	EnterCriticalSection(&m_lockStrategyStep);
	iter = m_mapOrderSysIDTag.find(stNewOrder.OrderSysID);
	if (iter == m_mapOrderSysIDTag.end())
	{
		bNewSysID = true;
		m_mapOrderSysIDTag[stNewOrder.OrderSysID] = szOrderTag;
	}
	LeaveCriticalSection(&m_lockStrategyStep);
	return bNewSysID;
}

bool CSimpleStrategy::CheckOrderTradesMatched(CThostFtdcOrderField stOrder, vector<CThostFtdcTradeField> vctTrades)
{
	bool bMatched = false;
	int nVolumeTraded = 0;
	GetAveragePrice(vctTrades, nVolumeTraded);
	return nVolumeTraded == stOrder.VolumeTraded;
}

double CSimpleStrategy::GetAveragePrice(vector<CThostFtdcTradeField> vctTrades, int& nTradedVolume)
{
	nTradedVolume = 0;
	double dAveragePrice = 0;
	double dTradeAmount = 0;
	for (int i = 0; i < vctTrades.size(); i++)
	{
		nTradedVolume += vctTrades[i].Volume;
		dTradeAmount += vctTrades[i].Volume * vctTrades[i].Price;
	}
	if (nTradedVolume > 0)
		dAveragePrice = dTradeAmount / nTradedVolume;
	return dAveragePrice;
}

bool CSimpleStrategy::GetOrder(string strOrderSysID, CThostFtdcOrderField& stOrder, double& dAveragePrice, bool& bMatched)
{
	string strOrderTag = "";
	map<string, string>::iterator iter2;
	EnterCriticalSection(&m_lockStrategyStep);
	iter2 = m_mapOrderSysIDTag.find(strOrderSysID);
	if (iter2 != m_mapOrderSysIDTag.end())
	{
		strOrderTag = iter2->second;
	}
	LeaveCriticalSection(&m_lockStrategyStep);
	if (strOrderTag == "")
		return false;
	return GetOrderByTag(strOrderTag, stOrder, dAveragePrice, bMatched);
}

bool CSimpleStrategy::GetOrderByTag(string strOrderTag, CThostFtdcOrderField& stOrder, double& dAveragePrice, bool& bMatched)
{
	bool bFound = false;
	map<string, StructOrderTrades>::iterator iter;
	vector<CThostFtdcTradeField>::iterator iter3;

	EnterCriticalSection(&m_lockStrategyStep);
	iter = m_mapOrderTrades.find(strOrderTag);
	if (iter != m_mapOrderTrades.end())
	{
		bFound = true;
		memcpy(&stOrder, &iter->second.stOrder, sizeof(CThostFtdcOrderField));
		dAveragePrice = iter->second.dAveragePrice;
		bMatched = iter->second.bMatched;
	}
	LeaveCriticalSection(&m_lockStrategyStep);
	return bFound;
}

bool CSimpleStrategy::IsCancelableOrder(TThostFtdcOrderStatusType OrderStatus)
{
	if (OrderStatus == THOST_FTDC_OST_Canceled || OrderStatus == THOST_FTDC_OST_AllTraded)
		return false;
	else
		return true;
}

bool CSimpleStrategy::IsNewOrder(CThostFtdcOrderField* pCurrentOrder, CThostFtdcOrderField* pNewOrder)
{
	if (!pCurrentOrder || !pNewOrder)
		return false;
	if (pCurrentOrder->FrontID != pNewOrder->FrontID ||
		pCurrentOrder->SessionID != pNewOrder->SessionID ||
		strcmp(pCurrentOrder->OrderRef, pNewOrder->OrderRef) != 0)
	{
		return false;
	}
	if (!IsCancelableOrder(pCurrentOrder->OrderStatus))
		return false;
	if (!IsCancelableOrder(pNewOrder->OrderStatus))
		return true;
	if (pNewOrder->VolumeTraded > pCurrentOrder->VolumeTraded)
		return true;
	if (pNewOrder->VolumeTraded < pCurrentOrder->VolumeTraded)
		return false;
	if (pNewOrder->OrderStatus == THOST_FTDC_OST_PartTradedNotQueueing &&
		pCurrentOrder->OrderStatus == THOST_FTDC_OST_PartTradedQueueing)
		return true;
	if (pNewOrder->OrderStatus == THOST_FTDC_OST_NoTradeNotQueueing &&
		pCurrentOrder->OrderStatus == THOST_FTDC_OST_NoTradeQueueing)
		return true;
	if (pNewOrder->OrderStatus != THOST_FTDC_OST_Unknown &&
		pCurrentOrder->OrderStatus == THOST_FTDC_OST_Unknown)
		return true;
	return false;
}

bool CSimpleStrategy::UpdateOrderStatus(CThostFtdcOrderField* pNewOrder)
{
	if (!pNewOrder)
		return false;

	bool bIsNewOrder;
	EnterCriticalSection(&m_lockStrategyStep);
	bIsNewOrder = IsNewOrder(&m_stCurrentOrder, pNewOrder);
	if (bIsNewOrder)
	{
		memcpy(&m_stCurrentOrder, pNewOrder, sizeof(m_stCurrentOrder));
	}
	LeaveCriticalSection(&m_lockStrategyStep);
	return bIsNewOrder;
}

void CSimpleStrategy::ReqOrderInsert(string strExchangeID, string strInstrumentID, char cOffect, char cDirection, double dLimitPrice, int nVolume, bool bLeg1)
{
	cerr << "----------开始下单--------------" << endl;
	CThostFtdcInputOrderField req;
	memset(&req, 0, sizeof(req));
	EnterCriticalSection(&m_lockStrategyStep);
	memset(&m_stCurrentOrder, 0, sizeof(m_stCurrentOrder));
	m_stCurrentOrder.FrontID = FRONT_ID;
	m_stCurrentOrder.SessionID = SESSION_ID;
	///经纪公司代码
	strcpy_s(req.BrokerID, BROKER_ID);
	strcpy_s(m_stCurrentOrder.BrokerID, BROKER_ID);
	///投资者代码
	strcpy_s(req.InvestorID, INVESTOR_ID); \
		strcpy_s(m_stCurrentOrder.InvestorID, INVESTOR_ID);
	///交易所代码
	strcpy_s(req.ExchangeID, strExchangeID.c_str());
	strcpy_s(m_stCurrentOrder.ExchangeID, strExchangeID.c_str());
	///合约代码
	strcpy_s(req.InstrumentID, strInstrumentID.c_str());
	strcpy_s(m_stCurrentOrder.InstrumentID, strInstrumentID.c_str());
	///报单引用
	strcpy_s(req.OrderRef, ORDER_REF);
	strcpy_s(m_stCurrentOrder.OrderRef, ORDER_REF);
	int iNextOrderRef = atoi(ORDER_REF);
	iNextOrderRef++;
	sprintf_s(ORDER_REF, "%d", iNextOrderRef);
	req.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
	m_stCurrentOrder.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
	///买卖方向: 
	req.Direction = cDirection;
	m_stCurrentOrder.Direction = cDirection;
	///组合开平标志: 开仓
	req.CombOffsetFlag[0] = cOffect;
	m_stCurrentOrder.CombOffsetFlag[0] = cOffect;
	///组合投机套保标志
	req.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
	m_stCurrentOrder.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
	///价格
	req.LimitPrice = dLimitPrice;
	m_stCurrentOrder.LimitPrice = dLimitPrice;
	///数量: 1
	req.VolumeTotalOriginal = nVolume;
	m_stCurrentOrder.VolumeTotalOriginal = nVolume;
	///有效期类型: 当日有效
	req.TimeCondition = THOST_FTDC_TC_GFD;
	m_stCurrentOrder.TimeCondition = THOST_FTDC_TC_GFD;
	req.VolumeCondition = THOST_FTDC_VC_AV;
	m_stCurrentOrder.VolumeCondition = THOST_FTDC_VC_AV;
	///最小成交量: 1
	req.MinVolume = 1;
	m_stCurrentOrder.MinVolume = 1;
	///触发条件: 立即
	req.ContingentCondition = THOST_FTDC_CC_Immediately;
	m_stCurrentOrder.ContingentCondition = THOST_FTDC_CC_Immediately;
	req.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
	m_stCurrentOrder.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
	///自动挂起标志: 否
	req.IsAutoSuspend = 0;
	m_stCurrentOrder.IsAutoSuspend = 0;
	req.UserForceClose = 0;
	m_stCurrentOrder.UserForceClose = 0;
	m_stCurrentOrder.OrderStatus = THOST_FTDC_OST_Unknown;
	LeaveCriticalSection(&m_lockStrategyStep);

	char szOrderTag[256];
	memset(szOrderTag, 0, 256);
	sprintf_s(szOrderTag, "%d|%d|%s", m_stCurrentOrder.FrontID, m_stCurrentOrder.SessionID, m_stCurrentOrder.OrderRef);
	if (bLeg1 && cOffect == THOST_FTDC_OF_Open)
		m_strLeg1OpenOrderTag = szOrderTag;
	if (bLeg1 && cOffect == THOST_FTDC_OF_Close)
		m_strLeg1CloseOrderTag = szOrderTag;
	if (!bLeg1 && cOffect == THOST_FTDC_OF_Open)
		m_strLeg2OpenOrderTag = szOrderTag;
	if (!bLeg1 && cOffect == THOST_FTDC_OF_Close)
		m_strLeg2CloseOrderTag = szOrderTag;

	int iResult = m_pTDUserApi->ReqOrderInsert(&req, ++iRequestID);
	if (iResult != 0) {
		cerr << "下单请求发送失败，错误码: " << iResult << endl;
	}
	else {
		cerr << "下单请求已发送，等待回报..." << endl;
	}
}

void CSimpleStrategy::ReqOrderAction(string strExchangeID, string strInstrumentID, int nFront, int nSessionID, string strOrderRef)
{
	CThostFtdcInputOrderActionField req;
	memset(&req, 0, sizeof(req));
	///经纪公司代码
	strcpy_s(req.BrokerID, BROKER_ID);
	///投资者代码
	strcpy_s(req.InvestorID, INVESTOR_ID);
	strcpy_s(req.OrderRef, strOrderRef.c_str());
	req.FrontID = nFront;
	///会话编号
	req.SessionID = nSessionID;
	req.ActionFlag = THOST_FTDC_AF_Delete;
	strcpy_s(req.InstrumentID, strInstrumentID.c_str());
	///交易所代码
	strcpy_s(req.ExchangeID, strExchangeID.c_str());
	int iResult = m_pTDUserApi->ReqOrderAction(&req, ++iRequestID);
}

void CSimpleStrategy::CheckCurrentOrder()//没问题
{
	EnterCriticalSection(&m_lockStrategyStep);
	if (m_eStrategy_Step == Strategy_OpenLeg1_Waiting)
	{
		if (IsCancelableOrder(m_stCurrentOrder.OrderStatus))
		{
			m_eStrategy_Step = Strategy_OpenLeg1_Canceling;
			cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			ReqOrderAction(m_stCurrentOrder.ExchangeID, m_stCurrentOrder.InstrumentID, m_stCurrentOrder.FrontID, m_stCurrentOrder.SessionID, m_stCurrentOrder.OrderRef);
		}
		else
		{
			CThostFtdcOrderField stOrder;
			double dAveragePrice = 0;
			bool bMatched = false;
			GetOrder(m_stCurrentOrder.OrderSysID, stOrder, dAveragePrice, bMatched);
			if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded && bMatched)
			{
				m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				CThostFtdcDepthMarketDataField stLeg2Tick;
				string strExchangeID;
				map<string, CThostFtdcInstrumentField>::iterator iterInst;
				iterInst = m_mapInstruments.find(m_strLeg2);
				if (iterInst != m_mapInstruments.end())
				{
					strExchangeID = iterInst->second.ExchangeID;
				}
				if (m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick))
				{
					m_strCurrentOrderRef = ORDER_REF;
					m_nCurrentOrderFrontID = FRONT_ID;
					m_nCurrentOrderSessionID = SESSION_ID;
					ReqOrderInsert(strExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1 + 1, 1);
					//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
					mPauseTimer = false;
				}
			}
			else
			{
				m_eStrategy_Step = Strategy_OpenCondition;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			}
		}
	}
	else if (m_eStrategy_Step == Strategy_OpenLeg2_Waiting)
	{
		if (IsCancelableOrder(m_stCurrentOrder.OrderStatus))
		{
			m_eStrategy_Step = Strategy_OpenLeg2_Canceling;
			cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			ReqOrderAction(m_stCurrentOrder.ExchangeID, m_stCurrentOrder.InstrumentID, m_stCurrentOrder.FrontID, m_stCurrentOrder.SessionID, m_stCurrentOrder.OrderRef);
		}
		else
		{
			CThostFtdcOrderField stOrder;
			double dAveragePrice = 0;
			bool bMatched = false;
			GetOrder(m_stCurrentOrder.OrderSysID, stOrder, dAveragePrice, bMatched);
			if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
			{
				if (bMatched)
				{
					if (m_strLeg1OpenOrderTag != "" && m_strLeg2OpenOrderTag != "")
					{
						CThostFtdcOrderField stLeg1Order;
						CThostFtdcOrderField stLeg2Order;
						double dAveragePriceLeg1;
						double dAveragePriceLeg2;
						bool bMatchedLeg1;
						bool bMatchedLeg2;
						if (GetOrderByTag(m_strLeg1OpenOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
							GetOrderByTag(m_strLeg2OpenOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
						{
							char szLog[1000];
							sprintf_s(szLog, "开仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
							cerr << szLog << endl;
						}
					}
					m_eStrategy_Step = Strategy_CloseCondition;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
			else
			{
				cerr << "Leg2重新开仓" << endl;
				m_eStrategy_Step = Strategy_OpenLeg2_Waiting;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				CThostFtdcDepthMarketDataField stLeg2Tick;
				string strExchangeID;
				map<string, CThostFtdcInstrumentField>::iterator iterInst;
				iterInst = m_mapInstruments.find(m_strLeg2);
				if (iterInst != m_mapInstruments.end())
				{
					strExchangeID = iterInst->second.ExchangeID;
				}

				if (m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick))
				{
					m_strCurrentOrderRef = ORDER_REF;
					m_nCurrentOrderFrontID = FRONT_ID;
					m_nCurrentOrderSessionID = SESSION_ID;
					ReqOrderInsert(strExchangeID, m_strLeg2, THOST_FTDC_OF_Open, THOST_FTDC_D_Buy, stLeg2Tick.AskPrice1 + 1, 1);
					//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
					mPauseTimer = false;
				}
			}
		}
	}
	else if (m_eStrategy_Step == Strategy_CloseLeg1_Waiting)
	{
		if (IsCancelableOrder(m_stCurrentOrder.OrderStatus))
		{
			m_eStrategy_Step = Strategy_CloseLeg1_Canceling;
			cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			ReqOrderAction(m_stCurrentOrder.ExchangeID, m_stCurrentOrder.InstrumentID, m_stCurrentOrder.FrontID, m_stCurrentOrder.SessionID, m_stCurrentOrder.OrderRef);
		}
		else
		{
			CThostFtdcOrderField stOrder;
			double dAveragePrice = 0;
			bool bMatched = false;
			GetOrder(m_stCurrentOrder.OrderSysID, stOrder, dAveragePrice, bMatched);
			if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
			{
				if (bMatched)
				{
					m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
					CThostFtdcDepthMarketDataField stLeg2Tick;
					string strExchangeID;
					map<string, CThostFtdcInstrumentField>::iterator iterInst;
					iterInst = m_mapInstruments.find(m_strLeg2);
					if (iterInst != m_mapInstruments.end())
					{
						strExchangeID = iterInst->second.ExchangeID;
					}

					if (m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick))
					{
						m_strCurrentOrderRef = ORDER_REF;
						m_nCurrentOrderFrontID = FRONT_ID;
						m_nCurrentOrderSessionID = SESSION_ID;
						cerr << "leg2平仓" << endl;
						ReqOrderInsert(strExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg2Tick.BidPrice1 - 1, 1);
						//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
						mPauseTimer = false;
					}
				}
			}
			else
			{
				m_eStrategy_Step = Strategy_CloseCondition;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			}
		}
	}
	else if (m_eStrategy_Step == Strategy_CloseLeg2_Waiting)
	{
		if (IsCancelableOrder(m_stCurrentOrder.OrderStatus))
		{
			m_eStrategy_Step = Strategy_CloseLeg2_Canceling;
			cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
			ReqOrderAction(m_stCurrentOrder.ExchangeID, m_stCurrentOrder.InstrumentID, m_stCurrentOrder.FrontID, m_stCurrentOrder.SessionID, m_stCurrentOrder.OrderRef);
		}
		else
		{
			CThostFtdcOrderField stOrder;
			double dAveragePrice = 0;
			bool bMatched = false;
			GetOrder(m_stCurrentOrder.OrderSysID, stOrder, dAveragePrice, bMatched);
			if (m_stCurrentOrder.OrderStatus == THOST_FTDC_OST_AllTraded)
			{
				if (bMatched)
				{
					if (m_strLeg1CloseOrderTag != "" && m_strLeg2CloseOrderTag != "")
					{
						CThostFtdcOrderField stLeg1Order;
						CThostFtdcOrderField stLeg2Order;
						double dAveragePriceLeg1;
						double dAveragePriceLeg2;
						bool bMatchedLeg1;
						bool bMatchedLeg2;
						if (GetOrderByTag(m_strLeg1CloseOrderTag, stLeg1Order, dAveragePriceLeg1, bMatchedLeg1) &&
							GetOrderByTag(m_strLeg2CloseOrderTag, stLeg2Order, dAveragePriceLeg2, bMatchedLeg2))
						{
							char szLog[1000];
							sprintf_s(szLog, "平仓基差：%.2f", dAveragePriceLeg1 - dAveragePriceLeg2);
							cerr << szLog << endl;
						}
					}
					m_eStrategy_Step = Strategy_OpenCondition;
					cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				}
			}
			else
			{
				cerr << "Leg2重新平仓" << endl;
				m_eStrategy_Step = Strategy_CloseLeg2_Waiting;
				cerr << "策略状态切换为：" + Strategy_Step_Name[m_eStrategy_Step] << endl;
				CThostFtdcDepthMarketDataField stLeg2Tick;
				string strExchangeID;
				map<string, CThostFtdcInstrumentField>::iterator iterInst;
				iterInst = m_mapInstruments.find(m_strLeg2);
				if (iterInst != m_mapInstruments.end())
				{
					strExchangeID = iterInst->second.ExchangeID;
				}

				if (m_pDataBuffer->GetTick(m_strLeg2, stLeg2Tick))
				{
					m_strCurrentOrderRef = ORDER_REF;
					m_nCurrentOrderFrontID = FRONT_ID;
					m_nCurrentOrderSessionID = SESSION_ID;
					ReqOrderInsert(strExchangeID, m_strLeg2, THOST_FTDC_OF_Close, THOST_FTDC_D_Sell, stLeg2Tick.BidPrice1 - 1, 1);
					//SetTimer(m_hDlg, 2, 10000, NULL); //改造定时器
					mPauseTimer = false;
				}
			}
		}
	}
	LeaveCriticalSection(&m_lockStrategyStep);
}

void CSimpleStrategy::SetMeanWindow(int w) { m_meanWindow = w; }
int CSimpleStrategy::GetMeanWindow() const { return m_meanWindow; }
