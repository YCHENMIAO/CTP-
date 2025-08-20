#pragma once
#include"MdSpi.h"
#include"TdSpi.h"
#include<iostream>
#include"Strategy.h"
#include "Indicator.h"
#include<map>
#include<vector>
#include<mutex>
#include<string>
#include<cstring>
#include "DataBuffer.h"

extern CThostFtdcTraderApi* pTDUserApi;
extern CThostFtdcMdApi* pMDUserApi;
extern CSimpleStrategy* g_pSimpleStrategy;
extern CDataBuffer* g_pDataBuffer;
extern CIndicator* g_pCIndicator;

extern TThostFtdcBrokerIDType	BROKER_ID;
extern TThostFtdcInvestorIDType INVESTOR_ID;
extern TThostFtdcPasswordType	PASSWORD;
extern TThostFtdcFrontIDType	FRONT_ID;	//前置编号
extern TThostFtdcSessionIDType	SESSION_ID;	//会话编号
extern TThostFtdcOrderRefType	ORDER_REF;	//报单引用

//extern const char* ppInstrumentID[];
extern int iInstrumentID;

extern int iRequestID; // 请求编号

extern vector<pair<string, string>> g_vctIFSpreads;
extern map<string, CThostFtdcInstrumentField> g_mapInstruments;
extern map<string, string> accountConfig_map;
extern std::map<std::string, std::string> contracts_map;
extern vector<CandleData> candles;  // 声明candletick外部变量

using namespace std;

MdSpi::MdSpi(CThostFtdcMdApi* mdapi) :mdapi(mdapi)
{

}

MdSpi::~MdSpi()
{
	// 释放所有对象的内存空间
	if (loginField)
		delete loginField;

}

void MdSpi::OnFrontConnected()
{
	cerr << "--->>> " << __FUNCTION__ << endl;
	cerr << "MD已连接" << endl;
	///用户登录请求
	ReqUserLogin();
}

void MdSpi::OnFrontDisconnected(int nReason)
{
	cerr << "MD已断开" << endl;
	cerr << "--->>> " << __FUNCTION__ << endl;
	cerr << "--->>> Reason = " << nReason << endl;
}

void MdSpi::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast)
{
	cerr << "--->>> " << __FUNCTION__ << endl;
	IsErrorRspInfo(pRspInfo);
}

void MdSpi::ReqUserLogin()
{
	CThostFtdcReqUserLoginField req;
	pMDUserApi = mdapi;
	memset(&req, 0, sizeof(req));
	strcpy_s(req.BrokerID, BROKER_ID);
	strcpy_s(req.UserID, INVESTOR_ID);
	strcpy_s(req.Password, PASSWORD);
	int iResult = pMDUserApi->ReqUserLogin(&req, ++iRequestID);
	cerr << "--->>> 发送用户登录请求: " << ((iResult == 0) ? "成功" : "失败") << endl;
}

void MdSpi::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast)
{
	cerr << "--->>> " << __FUNCTION__ << endl;
	if (bIsLast && !IsErrorRspInfo(pRspInfo))
	{
		cerr << "MD已登录" << endl;
		///获取当前交易日
		cerr << "--->>> 获取当前交易日 = " << pMDUserApi->GetTradingDay() << endl;
		char tdFront[50];
		strcpy_s(tdFront, sizeof(tdFront), accountConfig_map["TradeFront"].c_str());
		/*std::string instIdList;
		// 遍历 map，获取第二列的合约信息并拼接
		for (const auto& entry : contracts_map) {
			if (!instIdList.empty()) {
				instIdList += ","; // 用逗号分隔
			}
			instIdList += entry.second;  // 拼接合约字符串
		}
		// 将拼接好的字符串转为 C 风格的字符数组
		char* instIdListArray = new char[instIdList.size() + 1]; // 加1是为了最后的 '\0'
		strcpy_s(instIdListArray, instIdList.size() + 1, instIdList.c_str());
		SubscribeMarketData(instIdListArray);//char数组
		*/
		char instIdList[] = "sc2510,sc2511"; //编译的时候就直接赋值了，不推荐
		SubscribeMarketData(instIdList);
		if (pTDUserApi != nullptr) {
			pTDUserApi->RegisterFront(tdFront);
			pTDUserApi->Init();//初始化交易线程
		}
		else {
			cerr << "警告：交易API尚未初始化" << endl;
		}
	}
	else if (bIsLast && IsErrorRspInfo(pRspInfo))
	{
		string strLog;
		strLog = string("MD登录失败：") + pRspInfo->ErrorMsg;
		cerr << strLog << endl;
	}
}

void MdSpi::OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast)
{
	cerr << __FUNCTION__ << endl;
	if (bIsLast && !IsErrorRspInfo(pRspInfo))
	{
		cerr << "MD订阅成功" << endl;
	}
	else if (bIsLast && IsErrorRspInfo(pRspInfo))
	{
		string strLog;
		strLog = string("MD订阅失败：") + pRspInfo->ErrorMsg;
		cerr << strLog << endl;
	}
}

void MdSpi::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast)
{
	cerr << __FUNCTION__ << endl;
}

void MdSpi::SubscribeMarketData(char* instIdList)
{
	std::vector<char*> list;
	//char* strtok_s(char* str, const char* delimiters, char** context);
	char* context = nullptr;
	const char delimiters[] = " ,.";  // 分隔符包括空格、逗号、句号
	char* token = strtok_s(instIdList, delimiters, &context);
	while (token != nullptr) {
		std::cout << token << std::endl;
		list.push_back(token);
		token = strtok_s(nullptr, delimiters, &context);  // 继续获取下一个标记
	}
	unsigned int len = list.size();
	char** ppInstrument = new char* [len];
	for (unsigned int i = 0; i < len; i++)
	{
		ppInstrument[i] = list[i];
	}
	int nRet = mdapi->SubscribeMarketData(ppInstrument, len);
	cerr << "订阅期货合约行情：" << ((nRet == 0) ? "成功" : "失败") << endl;

	delete[] ppInstrument;
}

void MdSpi::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData)
{
	cerr << "收到行情数据：" << endl;
	printf("%s.%03d %s %.3f\n", pDepthMarketData->UpdateTime, pDepthMarketData->UpdateMillisec, pDepthMarketData->InstrumentID, pDepthMarketData->LastPrice);
	map<string, CThostFtdcDepthMarketDataField>::iterator iter, iter2;
	map<string, CThostFtdcInstrumentField>::iterator iterInst;
	string strInstrumentID = pDepthMarketData->InstrumentID;
	string strExchangeID;
	iterInst = g_mapInstruments.find(strInstrumentID);
	if (iterInst != g_mapInstruments.end())
	{
		strExchangeID = iterInst->second.ExchangeID;
	}
	g_pDataBuffer->PushNewTick(*pDepthMarketData);

	// 收集30tick均值并打印
	double dMean = 0.0;
	if (g_pCIndicator->CollectMeanPrice(g_pDataBuffer, strInstrumentID, dMean,30))
	{
		printf("%s Tick均值: %.3f\n", strInstrumentID.c_str(), dMean);
	}

	// 收集30个Tick(半分)的最大最小值并填充candles向量用于绘制蜡烛 //
	double dMax, dMin, lastPrice;
	string lastInstrumentID, lastUpdateTime;
	if (g_pDataBuffer->CollectMaxMinAndLastInfo(strInstrumentID, 10000, dMax, dMin, lastInstrumentID, lastUpdateTime, lastPrice))
	{
		CandleData newCandle;
		newCandle.time = lastUpdateTime;
		newCandle.open = dMin;  // 这里可以根据需要调整，比如用第一个tick的价格作为open
		newCandle.high = dMax;
		newCandle.low = dMin;
		newCandle.close = lastPrice;
		// 按合约写入candlesMap
		extern map<string, vector<CandleData>> candlesMap;  // 声明外部变量
		auto& vec = candlesMap[strInstrumentID];
		vec.push_back(newCandle);

		// 限制每个合约的蜡烛数，避免内存无限增长
		if (vec.size() > 30000)  // 最多保留1000个蜡烛图数据
		{
			vec.erase(vec.begin());
		}

		printf("%s 半分蜡烛图数据: 时间=%s, 最高=%.3f, 最低=%.3f, 收盘=%.3f\n",
			strInstrumentID.c_str(), lastUpdateTime.c_str(), dMax, dMin, lastPrice);
	}


	for (int i = 0; i < g_vctIFSpreads.size(); i++)
	{
		if (g_vctIFSpreads[i].first == strInstrumentID || g_vctIFSpreads[i].second == strInstrumentID)
		{
			double dSpreadLL, dSpreadBB, dSpreadBA, dSpreadAB, dSpreadAA;
			if (g_pDataBuffer->GetSpread(g_vctIFSpreads[i].first, g_vctIFSpreads[i].second, dSpreadLL, dSpreadBB, dSpreadBA, dSpreadAB, dSpreadAA))
			{
				printf("Bid-Bid=%.1f\tAsk-Ask=%.1f\nBid-Ask=%.1f\tAsk-Bid=%.1f\n", dSpreadBB, dSpreadAA, dSpreadBA, dSpreadAB);
			}
		}
	}
	g_pSimpleStrategy->OnRtnDepthMarketData(pDepthMarketData);
}

void MdSpi::OnHeartBeatWarning(int nTimeLapse)
{
	cerr << "--->>> " << __FUNCTION__ << endl;
	cerr << "--->>> nTimerLapse = " << nTimeLapse << endl;
}

bool MdSpi::IsErrorRspInfo(CThostFtdcRspInfoField* pRspInfo)
{
	// 如果ErrorID != 0, 说明收到了错误的响应
	bool bResult = ((pRspInfo) && (pRspInfo->ErrorID != 0));
	if (bResult)
		cerr << "--->>> ErrorID=" << pRspInfo->ErrorID << ", ErrorMsg=" << pRspInfo->ErrorMsg << endl;
	return bResult;
}

